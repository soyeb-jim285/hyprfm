#include "providers/pdfpreviewprovider.h"
#include "services/filesystemtasks.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QProcess>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDir>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <mutex>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace {

struct PdfRequest {
    QString path;
    int page = 0;
};

PdfRequest parseRequest(const QString &id)
{
    PdfRequest request;
    const int queryIndex = id.indexOf('?');
    const QString encodedPath = queryIndex >= 0 ? id.left(queryIndex) : id;
    request.path = QUrl::fromPercentEncoding(encodedPath.toUtf8());

    if (queryIndex >= 0) {
        QUrlQuery query;
        query.setQuery(id.mid(queryIndex + 1));
        request.page = query.queryItemValue("page").toInt();
    }

    return request;
}

QByteArray runPdfCommand(const QString &executable, const QStringList &arguments,
                         int timeout, const std::atomic_bool &cancelled)
{
    if (cancelled.load())
        return {};

    QProcess proc;
#ifdef Q_OS_LINUX
    const pid_t parentPid = ::getpid();
    proc.setChildProcessModifier([parentPid] {
        // Also stop a renderer if HyprFM exits before its worker can cancel it.
        if (::prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) == 0 && ::getppid() != parentPid)
            ::_exit(1);
    });
#endif
    proc.start(executable, arguments);
    QDeadlineTimer deadline(timeout);
    while (proc.state() != QProcess::NotRunning && !cancelled.load() && !deadline.hasExpired())
        proc.waitForFinished(static_cast<int>(qMin<qint64>(100, deadline.remainingTime())));

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        proc.waitForFinished(-1);
        return {};
    }
    if (cancelled.load() || proc.error() == QProcess::FailedToStart
        || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return proc.readAllStandardOutput();
}

// pdfinfo prints "Page size:   595.28 x 841.89 pts (A4)" on one line.
// Measured ~17 ms, and it used to run on every single page change even
// though the answer is a property of the document, not the page.
QSizeF pageSizeUncached(const QString &path, const std::atomic_bool &cancelled)
{
    const auto output = runPdfCommand(QStringLiteral("pdfinfo"), {path}, 5000, cancelled);
    if (output.isEmpty())
        return {};

    const QRegularExpression re(
        QStringLiteral(R"(Page size:\s*([0-9.]+)\s*x\s*([0-9.]+)\s*pts)"));
    const auto m = re.match(QString::fromUtf8(output));
    if (!m.hasMatch())
        return {};
    return QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble());
}

// ponytail: unbounded map, but it holds one QSizeF per PDF previewed this
// session. Add an LRU cap if someone ever previews thousands of documents.
// Same answer as pdfinfo, without the ~17 ms process: it is a property of the
// document, so it survives on disk beside the rendered pages. The rendered
// page's cache key contains the dpi, which is derived from this size, so this
// lookup has to happen before the page cache can be consulted at all - which
// is why a warm page cache alone still cost a pdfinfo run per process.
QSizeF pageSizeFromDisk(const QString &key);
void pageSizeToDisk(const QString &key, const QSizeF &size);

QSizeF pageSizePoints(const QString &path, const std::atomic_bool &cancelled)
{
    const QString key = path + QLatin1Char('\0')
        + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());

    static auto *mutex = new QMutex;
    static auto *cache = new QHash<QString, QSizeF>;

    {
        QMutexLocker locker(mutex);
        const auto it = cache->constFind(key);
        if (it != cache->constEnd())
            return *it;
    }

    QSizeF size = pageSizeFromDisk(key);
    if (!size.isValid()) {
        size = pageSizeUncached(path, cancelled);
        if (size.isValid())
            pageSizeToDisk(key, size);
    }

    if (!cancelled.load()) {
        QMutexLocker locker(mutex);
        cache->insert(key, size);
    }
    return size;
}

double dpiForRequest(const QSizeF &pageSizePts, const QSize &requestedSize)
{
    if (!pageSizePts.isValid() || pageSizePts.isEmpty())
        return 120.0;

    double scale = 1.0;
    if (requestedSize.width() > 0 && requestedSize.height() > 0) {
        const double xScale = requestedSize.width() / pageSizePts.width();
        const double yScale = requestedSize.height() / pageSizePts.height();
        scale = std::min(xScale, yScale);
    } else if (requestedSize.width() > 0) {
        scale = requestedSize.width() / pageSizePts.width();
    } else if (requestedSize.height() > 0) {
        scale = requestedSize.height() / pageSizePts.height();
    }

    scale = std::max(0.2, scale);
    return 72.0 * scale;
}


// Rendered-page cache.
//
// QML asks for the same page more than once: an Image's sourceSize is part
// of Qt's own pixmap-cache key, and it changes as the item lays out, so the
// first paint routinely renders a page twice. The prefetch of the adjacent
// pages doubles that again. Keying on the quantised dpi we actually pass to
// pdftoppm collapses all of it to a single render.
//
// ponytail: cost is the decoded image size, capped at 64 MB -- roughly 20
// A4 pages at 150 dpi, comfortably more than the prefetch window needs.
QString renderKey(const QString &path, int page, int dpi)
{
    return path + QLatin1Char('\0')
        + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch())
        + QLatin1Char('\0') + QString::number(page)
        + QLatin1Char('@') + QString::number(dpi);
}

QMutex &renderCacheMutex()
{
    static auto *mutex = new QMutex;
    return *mutex;
}

QCache<QString, QImage> &renderCache()
{
    static auto *cache = new QCache<QString, QImage>(64 * 1024 * 1024);
    return *cache;
}

// Pages that could not be rendered: a file that is not really a PDF, or one
// poppler refuses. Otherwise, scrolling back would run pdftoppm again.
QSet<QString> &failedRenders()
{
    static auto *failed = new QSet<QString>;
    return *failed;
}

// The in-memory cache dies with the process and is capped at 64 MB, so a
// folder of PDFs re-renders on every launch and on every scroll back. The
// rendered JPEG is a few tens of KB; keeping it on disk turns a ~50 ms
// pdftoppm run into a file read.
// poppler's own tolerance: the %PDF- header may sit anywhere in the first
// kilobyte, after a BOM or stray bytes.
bool looksLikePdf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(1024).contains("%PDF-");
}

QString pageCacheDir()
{
    // Not cached in a static: it is only consulted on a cache miss or a write,
    // and resolving it each time keeps it honest when XDG_CACHE_HOME changes.
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/hyprfm/pdf-pages");
}

QString diskCachePath(const QString &key)
{
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return pageCacheDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

QSizeF pageSizeFromDisk(const QString &key)
{
    QFile file(diskCachePath(key) + QStringLiteral(".size"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> parts = file.read(64).trimmed().split(' ');
    if (parts.size() != 2)
        return {};
    return QSizeF(parts.at(0).toDouble(), parts.at(1).toDouble());
}

void pageSizeToDisk(const QString &key, const QSizeF &size)
{
    if (!QDir().mkpath(pageCacheDir()))
        return;
    QSaveFile file(diskCachePath(key) + QStringLiteral(".size"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QByteArray::number(size.width()) + ' ' + QByteArray::number(size.height()));
    file.commit();
}

// ponytail: one sweep per process, oldest first, no LRU bookkeeping. 128 MB is
// a few thousand pages; if someone ever browses more than that in one session
// the newest ones simply stay in the memory cache.
void pruneDiskCacheOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        QFileInfoList files = QDir(pageCacheDir()).entryInfoList({QStringLiteral("*.jpg")},
                                                                 QDir::Files, QDir::Time);
        qint64 total = 0;
        for (const QFileInfo &info : std::as_const(files))
            total += info.size();
        while (total > 128 * 1024 * 1024 && !files.isEmpty()) {
            const QFileInfo oldest = files.takeLast();
            total -= oldest.size();
            QFile::remove(oldest.absoluteFilePath());
        }
    });
}

QThreadPool *pdfPool(const QString &path)
{
    const QByteArray mount = MountTable().forPath(path);
    static QMutex mutex;
    static auto *pools = new QHash<QByteArray, QThreadPool *>;
    QMutexLocker locker(&mutex);
    auto *&pool = (*pools)[mount];
    if (!pool) {
        // Serialise duplicate renders for a mount, while keeping other mounts
        // independent. Qt waits for its global pool on exit, so keep these
        // pools and the caches they use alive until the process ends.
        pool = new QThreadPool;
        pool->setMaxThreadCount(1);
    }
    return pool;
}

}

PdfPreviewResponse::PdfPreviewResponse(const QString &id, const QSize &requestedSize)
    : m_requestedSize(requestedSize)
{
    const auto request = parseRequest(id);
    m_path = request.path;
    m_page = request.page;
    m_pool = pdfPool(m_path);
    setAutoDelete(false);
    m_pool->start(this);
}

void PdfPreviewResponse::cancel()
{
    m_cancelled.store(true);
    if (m_pool->tryTake(this))
        finish();
}

bool PdfPreviewResponse::tryCache(const QString &key)
{
    QMutexLocker locker(&renderCacheMutex());
    if (const QImage *cached = renderCache().object(key)) {
        m_image = *cached;
        return true;
    }
    return false;
}

bool PdfPreviewResponse::tryDiskCache(const QString &key)
{
    QFile file(diskCachePath(key));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray jpeg = file.readAll();
    if (jpeg.isEmpty() || !m_image.loadFromData(jpeg, "JPEG"))
        return false;
    QMutexLocker locker(&renderCacheMutex());
    renderCache().insert(key, new QImage(m_image),
                         static_cast<qsizetype>(m_image.sizeInBytes()));
    return true;
}

void PdfPreviewResponse::writeDiskCache(const QString &key, const QByteArray &jpeg)
{
    if (!QDir().mkpath(pageCacheDir()))
        return;
    QSaveFile file(diskCachePath(key));
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (file.write(jpeg) == jpeg.size())
        file.commit();
    pruneDiskCacheOnce();
}

void PdfPreviewResponse::finish()
{
    QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
}

void PdfPreviewResponse::run()
{
    const auto onExit = qScopeGuard([this] { finish(); });
    if (m_cancelled.load() || m_path.isEmpty() || !QFileInfo(m_path).isFile())
        return;

    if (m_cancelled.load() || QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty())
        return;

    // poppler scans the first kilobyte for the header and renders anything it
    // finds one in, so this rejects only what it would reject anyway - but it
    // does so with a 1 KB read instead of a process spawn.
    if (!looksLikePdf(m_path) || m_cancelled.load())
        return;

    const QSizeF sizePts = pageSizePoints(m_path, m_cancelled);
    if (m_cancelled.load())
        return;
    const int dpi = static_cast<int>(dpiForRequest(sizePts, m_requestedSize) + 0.5);
    const QString key = renderKey(m_path, m_page, dpi);

    if (tryCache(key) || tryDiskCache(key))
        return;

    {
        QMutexLocker locker(&renderCacheMutex());
        if (failedRenders().contains(key))
            return;
    }

    // pdftoppm writes to stdout only when no output-prefix argument is
    // given. Passing "-" as the prefix (a common assumption) makes recent
    // poppler write a file named "--1.png" in the current working
    // directory instead — silently producing no stdout output.
    // -png spends ~87% of its wall time in zlib, not in rendering: 570 ms
    // vs 70 ms for -jpeg on the same page at the same dpi. The pages are
    // photographic-quality raster either way, and at quality=90 the mean
    // per-pixel difference from the PNG is 0.42/255 — invisible in a
    // preview pane, 8x faster to produce.
    const QByteArray jpeg = runPdfCommand(QStringLiteral("pdftoppm"), {
        QStringLiteral("-jpeg"),
        QStringLiteral("-jpegopt"), QStringLiteral("quality=90"),
        QStringLiteral("-singlefile"),
        QStringLiteral("-f"), QString::number(m_page + 1),
        QStringLiteral("-l"), QString::number(m_page + 1),
        QStringLiteral("-r"), QString::number(dpi),
        m_path,
    }, 15000, m_cancelled);

    if (m_cancelled.load())
        return;
    if (jpeg.isEmpty()) {
        QMutexLocker locker(&renderCacheMutex());
        failedRenders().insert(key);
        return;
    }

    m_image.loadFromData(jpeg, "JPEG");

    if (!m_image.isNull()) {
        const qsizetype cost = m_image.sizeInBytes();
        {
            QMutexLocker locker(&renderCacheMutex());
            renderCache().insert(key, new QImage(m_image), static_cast<qsizetype>(cost));
        }
        writeDiskCache(key, jpeg);
    }
}

QQuickTextureFactory *PdfPreviewResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QQuickImageResponse *PdfPreviewProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    return new PdfPreviewResponse(id, requestedSize);
}
