#include "providers/pdfpreviewprovider.h"

#include <QDateTime>
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
#include <QSet>
#include <mutex>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

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

// pdfinfo prints "Page size:   595.28 x 841.89 pts (A4)" on one line.
// Measured ~17 ms, and it used to run on every single page change even
// though the answer is a property of the document, not the page.
QSizeF pageSizeUncached(const QString &path)
{
    QProcess proc;
    proc.start(QStringLiteral("pdfinfo"), {path});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(
        QStringLiteral(R"(Page size:\s*([0-9.]+)\s*x\s*([0-9.]+)\s*pts)"));
    const auto m = re.match(out);
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

QSizeF pageSizePoints(const QString &path)
{
    const QString key = path + QLatin1Char('\0')
        + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());

    static QMutex mutex;
    static QHash<QString, QSizeF> cache;

    {
        QMutexLocker locker(&mutex);
        const auto it = cache.constFind(key);
        if (it != cache.constEnd())
            return *it;
    }

    // Deliberately computed outside the lock: two threads racing on a cold
    // cache just both run pdfinfo and store the same answer, which is far
    // cheaper than serialising every render behind one mutex.
    QSizeF size = pageSizeFromDisk(key);
    if (!size.isValid()) {
        size = pageSizeUncached(path);
        if (size.isValid())
            pageSizeToDisk(key, size);
    }

    QMutexLocker locker(&mutex);
    cache.insert(key, size);
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
    static QMutex mutex;
    return mutex;
}

QCache<QString, QImage> &renderCache()
{
    static QCache<QString, QImage> cache(64 * 1024 * 1024);
    return cache;
}

// Pages that could not be rendered: a file that is not really a PDF, or one
// poppler refuses. Without this, every pass over a directory of them spawns
// pdftoppm again for each one, and they are serialised behind renderLock().
QSet<QString> &failedRenders()
{
    static QSet<QString> failed;
    return failed;
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

// Serialises the actual pdftoppm runs. Without it the cache never helps on
// first paint: QML issues the duplicate requests concurrently, so both miss
// the cache and both render. Holding this across the subprocess is safe --
// it is only ever taken by pool threads, never by the GUI thread.
QMutex &renderLock()
{
    static QMutex mutex;
    return mutex;
}

}

PdfPreviewResponse::PdfPreviewResponse(const QString &id, const QSize &requestedSize)
    : m_id(id)
    , m_requestedSize(requestedSize)
{
    setAutoDelete(false);
    QThreadPool::globalInstance()->start(this);
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

void PdfPreviewResponse::run()
{
    const PdfRequest request = parseRequest(m_id);
    if (request.path.isEmpty() || !QFileInfo::exists(request.path)) {
        emit finished();
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        emit finished();
        return;
    }

    // poppler scans the first kilobyte for the header and renders anything it
    // finds one in, so this rejects only what it would reject anyway - but it
    // does so with a 1 KB read instead of a process spawn. A directory of
    // files that merely end in .pdf (MIME detection falls back to the
    // extension when the content says nothing) used to start pdftoppm for
    // every one of them, serialised behind renderLock().
    if (!looksLikePdf(request.path)) {
        emit finished();
        return;
    }

    const QSizeF sizePts = pageSizePoints(request.path);
    const int dpi = static_cast<int>(dpiForRequest(sizePts, m_requestedSize) + 0.5);
    const QString key = renderKey(request.path, request.page, dpi);

    if (tryCache(key)) {
        emit finished();
        return;
    }
    if (tryDiskCache(key)) {
        emit finished();
        return;
    }

    QMutexLocker renderLocker(&renderLock());

    // Re-check: another thread may have rendered this exact page while we
    // were queued behind it. This is the check that actually collapses the
    // duplicate first-paint requests.
    if (tryCache(key)) {
        emit finished();
        return;
    }
    {
        QMutexLocker locker(&renderCacheMutex());
        if (failedRenders().contains(key)) {
            emit finished();
            return;
        }
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
    QProcess proc;
    proc.start(QStringLiteral("pdftoppm"), {
        QStringLiteral("-jpeg"),
        QStringLiteral("-jpegopt"), QStringLiteral("quality=90"),
        QStringLiteral("-singlefile"),
        QStringLiteral("-f"), QString::number(request.page + 1),
        QStringLiteral("-l"), QString::number(request.page + 1),
        QStringLiteral("-r"), QString::number(dpi),
        request.path,
    });

    const bool ran = proc.waitForFinished(15000) && proc.exitCode() == 0;
    const QByteArray jpeg = ran ? proc.readAllStandardOutput() : QByteArray();
    if (jpeg.isEmpty()) {
        QMutexLocker locker(&renderCacheMutex());
        failedRenders().insert(key);
        emit finished();
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

    emit finished();
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
