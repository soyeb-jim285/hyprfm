#include "thumbnailprovider.h"
#include "iconprovider.h"
#include "services/cloudmounts.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThreadPool>
#include <QBuffer>
#include <QImageReader>
#include <QImage>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPainter>
#include <QQuickTextureFactory>
#include <QProcess>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>

#undef signals
#include <gio/gio.h>
#define signals Q_SIGNALS

static bool isTrashUri(const QString &path)
{
    return QUrl(path).scheme() == "trash";
}

// QML encodes the path (encodeURIComponent), so a '?' or '#' in a file or
// folder name cannot end the path early; the first '?' starts the query.
static QString sourcePathFromId(QString id)
{
    const int queryIndex = id.indexOf(QLatin1Char('?'));
    if (queryIndex >= 0)
        id.truncate(queryIndex);
    return QUrl::fromPercentEncoding(id.toUtf8());
}

static bool runningInFlatpak()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

static QByteArray readTrashUriData(const QString &uri)
{
    QProcess proc;
    const QString uriArg = QUrl(uri).toString(QUrl::FullyEncoded);
    // Inside Flatpak the sandboxed gio sees its own (empty) trash; route
    // through `flatpak-spawn --host gio cat` so we read from the host's
    // real trash where the file actually lives.
    if (runningInFlatpak()) {
        proc.start(QStringLiteral("flatpak-spawn"),
                   {QStringLiteral("--host"), QStringLiteral("gio"),
                    QStringLiteral("cat"), uriArg});
    } else {
        proc.start(QStringLiteral("gio"), {QStringLiteral("cat"), uriArg});
    }
    // ponytail: 64 MB cap, same loop as PreviewService::readBoundedOutput;
    // share it if a third copy ever appears.
    constexpr qint64 kMaxBytes = 64 * 1024 * 1024;
    if (!proc.waitForStarted(2000))
        return {};
    QByteArray data;
    while (proc.state() != QProcess::NotRunning) {
        if (!proc.waitForReadyRead(100))
            proc.waitForFinished(100);
        data += proc.readAllStandardOutput();
        if (data.size() > kMaxBytes) {
            proc.kill();
            proc.waitForFinished(1000);
            return {};
        }
    }
    data += proc.readAllStandardOutput();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return data;
}

static QMimeType mimeForLocation(const QString &path)
{
    static QMimeDatabase mimeDb;
    const QUrl url(path);
    if (url.scheme() == QLatin1String("file") || url.scheme().isEmpty())
        return mimeDb.mimeTypeForFile(url.scheme().isEmpty() ? path : url.toLocalFile());
    return mimeDb.mimeTypeForFile(url.fileName(), QMimeDatabase::MatchExtension);
}

static bool isVideoFile(const QString &path)
{
    // Use QMimeDatabase so we don't misclassify ambiguous extensions like
    // .ts (which is both MPEG transport stream *and* TypeScript). For
    // local files this content-sniffs when the glob is ambiguous; for
    // trash:// URIs we just match by name.
    const QMimeType mime = mimeForLocation(path);
    return mime.isValid() && mime.name().startsWith(QLatin1String("video/"));
}

// QImageReader + qsvg plugin technically handles SVG, but the plugin relies
// on the `<svg width/height>` attrs to return a non-zero intrinsic size.
// Lots of SVGs in the wild ship only `viewBox`, so the reader returns a
// 0×0 QSize and read() gives us an invalid image. Render through
// QSvgRenderer directly instead: always crisp, always at the requested
// target size.
static bool isSvgFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz"))
        return true;
    const QMimeType mime = mimeForLocation(path);
    return mime.isValid() && mime.name() == QLatin1String("image/svg+xml");
}

// ---------------------------------------------------------------------------
// Freedesktop thumbnail cache (https://specifications.freedesktop.org/
// thumbnail-spec/thumbnail-spec-latest.html)
//
// Cached PNGs live under ~/.cache/thumbnails/<bucket>/<md5(uri)>.png with
// `Thumb::URI` and `Thumb::MTime` tEXt entries. A hit is only considered
// valid when the stored mtime matches the current source mtime — any edit
// to the file invalidates the thumbnail automatically.
// ---------------------------------------------------------------------------

static QString thumbnailBucket(const QSize &size)
{
    const int m = std::max(size.width(), size.height());
    if (m <= 128)  return QStringLiteral("normal");
    if (m <= 256)  return QStringLiteral("large");
    if (m <= 512)  return QStringLiteral("x-large");
    return QStringLiteral("xx-large");
}

static QString thumbnailCacheDir(const QSize &size)
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericCacheLocation);
    return base + QStringLiteral("/thumbnails/") + thumbnailBucket(size);
}

static QString thumbnailCachePath(const QString &absLocalPath, const QSize &size)
{
    const QString uri = QUrl::fromLocalFile(absLocalPath)
                            .toString(QUrl::FullyEncoded);
    const QByteArray hash = QCryptographicHash::hash(
        uri.toUtf8(), QCryptographicHash::Md5).toHex();
    return thumbnailCacheDir(size) + QLatin1Char('/')
        + QString::fromLatin1(hash) + QStringLiteral(".png");
}

static bool loadCachedThumbnail(const QString &sourcePath,
                                const QString &cachePath,
                                QImage *out)
{
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists())
        return false;
    if (!QFile::exists(cachePath))
        return false;

    QImage img;
    if (!img.load(cachePath, "PNG") || img.isNull())
        return false;

    const QString storedMtime = img.text(QStringLiteral("Thumb::MTime"));
    if (storedMtime.isEmpty())
        return false;
    if (storedMtime.toLongLong() != srcInfo.lastModified().toSecsSinceEpoch())
        return false;

    *out = img;
    return true;
}

static void saveCachedThumbnail(const QString &sourcePath,
                                const QString &cachePath,
                                const QImage &image)
{
    if (image.isNull())
        return;
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists())
        return;

    QDir().mkpath(QFileInfo(cachePath).absolutePath());

    QImage tagged = image;
    tagged.setText(QStringLiteral("Thumb::URI"),
        QUrl::fromLocalFile(sourcePath).toString(QUrl::FullyEncoded));
    tagged.setText(QStringLiteral("Thumb::MTime"),
        QString::number(srcInfo.lastModified().toSecsSinceEpoch()));
    tagged.setText(QStringLiteral("Software"), QStringLiteral("HyprFM"));

    // QSaveFile ensures we don't end up with a half-written PNG if we race
    // with another process writing the same thumbnail.
    QSaveFile saver(cachePath);
    if (!saver.open(QIODevice::WriteOnly))
        return;
    if (!tagged.save(&saver, "PNG")) {
        saver.cancelWriting();
        return;
    }
    saver.commit();
}

// Don't cache thumbnails of files that already live inside the cache dir
// itself, or items coming from gio-backed URIs (trash:// / remote) where we
// can't cheaply resolve a canonical local path.
static bool isCacheableLocalPath(const QString &path)
{
    if (path.isEmpty())
        return false;
    if (isTrashUri(path))
        return false;
    const QUrl url(path);
    if (!url.scheme().isEmpty() && url.scheme() != QLatin1String("file"))
        return false;

    static const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/thumbnails/");
    return !path.startsWith(cacheRoot);
}

static QThreadPool *thumbnailPool()
{
    static QThreadPool *pool = []() {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    return pool;
}

// ---------------------------------------------------------------------------
// Custom folder icons (#68): the picture a folder shows instead of the theme
// folder icon. First existing hit wins:
//   1. <dir>/.directory [Desktop Entry] Icon= (KDE/Dolphin): ./relative,
//      absolute, file:// URI, or a theme icon name
//   2. GIO metadata::custom-icon (Nautilus, Nemo, Thunar)
//   3. cover.jpg / cover.png / folder.jpg / folder.png
// Empty when none applies, so the view keeps the theme icon.
// ---------------------------------------------------------------------------

static QString directoryFileIcon(const QString &dir)
{
    // Regular files only: a FIFO named .directory would block a pool thread
    // forever on open(). Real ones are a few lines; skip anything large.
    const QString path = dir + QStringLiteral("/.directory");
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > 64 * 1024)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    bool inDesktopEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('[')))
            inDesktopEntry = line == QLatin1String("[Desktop Entry]");
        else if (inDesktopEntry && line.startsWith(QLatin1String("Icon=")))
            return line.mid(5).trimmed();
    }
    return {};
}

static QString gioCustomIcon(const QString &dir)
{
    GFile *file = g_file_new_for_path(QFile::encodeName(dir).constData());
    GFileInfo *info = g_file_query_info(file, "metadata::custom-icon",
                                        G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    QString uri;
    if (info) {
        if (const char *value = g_file_info_get_attribute_string(info, "metadata::custom-icon"))
            uri = QString::fromUtf8(value);
        g_object_unref(info);
    }
    g_object_unref(file);
    return uri;
}

// Theme lookups for .directory Icon=<name>, one per theme, never deleted:
// pool jobs can still be running while the app exits, so they must not use
// the engine-owned IconProvider (it is freed with the engine).
static QString themeIconPath(const QString &theme, const QString &name, int size)
{
    // Heap-allocated and never destroyed, so no static destructor runs
    // under a job at exit. Bounded: one entry per icon theme the user picks.
    static QBasicMutex mutex;
    static auto *lookups = new QHash<QString, IconProvider *>;
    IconProvider *lookup;
    {
        QMutexLocker locker(&mutex);
        lookup = lookups->value(theme);
        if (!lookup) {
            lookup = new IconProvider(theme);
            lookups->insert(theme, lookup);
        }
    }
    return lookup->findIconPath(name, size);
}

// An icon file the folder points at may live elsewhere; keep the same rule
// the views apply to folders and skip rclone mounts (string check: like the
// views' gate, a symlink can still lead into one).
static bool usableIconFile(const QString &path)
{
    return !path.isEmpty() && !isCloudMountPath(QDir::cleanPath(path))
        && QFileInfo(path).isFile();
}

static QString folderIconPath(const QString &dir, const QString &theme, int size)
{
    const QString icon = directoryFileIcon(dir);
    if (!icon.isEmpty()) {
        QString path;
        if (icon.startsWith(QLatin1String("./")))
            path = dir + icon.mid(1);
        else if (icon.startsWith(QLatin1Char('/')))
            path = icon;
        else if (icon.startsWith(QLatin1String("file:")))
            path = QUrl(icon).toLocalFile();
        else if (!theme.isEmpty() && !icon.contains(QLatin1Char('/')))
            path = themeIconPath(theme, icon, size);
        if (usableIconFile(path))
            return path;
    }

    const QString uri = gioCustomIcon(dir);
    if (!uri.isEmpty()) {
        const QString path = QUrl(uri).toLocalFile();
        if (usableIconFile(path))
            return path;
    }

    for (const char *name : {"cover.jpg", "cover.png", "folder.jpg", "folder.png"}) {
        const QString path = dir + QLatin1Char('/') + QLatin1String(name);
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

// ---------------------------------------------------------------------------
// ThumbnailResponse
// ---------------------------------------------------------------------------

ThumbnailResponse::ThumbnailResponse(const QString &id, const QSize &requestedSize)
    : m_id(sourcePathFromId(id))
    , m_requestedSize(requestedSize)
{
    const int query = id.indexOf(QLatin1Char('?'));
    if (query >= 0) {
        for (const QString &param : id.mid(query + 1).split(QLatin1Char('&'))) {
            if (param.startsWith(QLatin1String("theme=")))
                m_iconTheme = param.mid(6);
        }
    }
    setAutoDelete(false);
    thumbnailPool()->start(this);
}

void ThumbnailResponse::run()
{
    const QSize targetSize = m_requestedSize.isValid()
        ? m_requestedSize : QSize(128, 128);

    // A folder stands in for its custom icon file; from here on the normal
    // pipeline (disk cache keyed on that file, SVG/image render) handles it.
    const QUrl idUrl(m_id);
    if ((idUrl.scheme().isEmpty() || idUrl.isLocalFile()) && QFileInfo(m_id).isDir()) {
        m_id = folderIconPath(m_id, m_iconTheme, std::max(targetSize.width(), targetSize.height()));
        if (m_id.isEmpty()) {
            m_folderWithoutIcon = true;
            emit finished();
            return;
        }
    }

    QString cachePath;
    const bool cacheable = isCacheableLocalPath(m_id);
    if (cacheable) {
        cachePath = thumbnailCachePath(m_id, targetSize);
        if (loadCachedThumbnail(m_id, cachePath, &m_image)) {
            emit finished();
            return;
        }
    }

    if (isVideoFile(m_id))
        generateVideoThumbnail();
    else if (isSvgFile(m_id))
        generateSvgThumbnail();
    else
        generateImageThumbnail();

    if (cacheable && !m_image.isNull())
        saveCachedThumbnail(m_id, cachePath, m_image);

    emit finished();
}

// SVG bytes for QSvgRenderer with a hard size cap. QSvgRenderer inflates
// .svgz itself with no limit: a 2 MB file inflated to 2 GB and took 2 GB of
// RAM per pool thread. Inflate here, in chunks, and give up past the cap; a
// gzip inside the gzip is refused, or QSvgRenderer would inflate that too.
static constexpr qsizetype kMaxSvgBytes = 32 * 1024 * 1024;

static QByteArray cappedSvgBytes(const QByteArray &raw)
{
    if (raw.size() > kMaxSvgBytes)
        return {};
    if (!raw.startsWith("\x1f\x8b"))
        return raw;

    GInputStream *source = g_memory_input_stream_new_from_data(raw.constData(), raw.size(), nullptr);
    GZlibDecompressor *gunzip = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    GInputStream *stream = g_converter_input_stream_new(source, G_CONVERTER(gunzip));
    QByteArray out;
    char chunk[64 * 1024];
    bool ok = true;
    for (;;) {
        const gssize n = g_input_stream_read(stream, chunk, sizeof chunk, nullptr, nullptr);
        if (n == 0)
            break;
        if (n < 0 || out.size() + n > kMaxSvgBytes) {
            ok = false;
            break;
        }
        out.append(chunk, n);
    }
    g_object_unref(stream);
    g_object_unref(gunzip);
    g_object_unref(source);
    if (!ok || out.startsWith("\x1f\x8b"))
        return {};
    return out;
}

void ThumbnailResponse::generateSvgThumbnail()
{
    const QSize targetSize = m_requestedSize.isValid() ? m_requestedSize
                                                       : QSize(128, 128);

    QSvgRenderer renderer;
    bool loaded = false;
    if (isTrashUri(m_id)) {
        const QByteArray data = cappedSvgBytes(readTrashUriData(m_id));
        if (data.isEmpty())
            return;
        loaded = renderer.load(data);
    } else {
        QFile file(m_id);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray data = cappedSvgBytes(file.read(kMaxSvgBytes + 1));
        if (data.isEmpty())
            return;
        loaded = renderer.load(data);
    }
    if (!loaded || !renderer.isValid())
        return;

    QSize native = renderer.defaultSize();
    if (native.isEmpty())
        native = targetSize;
    const QSize out = native.scaled(targetSize, Qt::KeepAspectRatio);
    if (out.isEmpty())
        return;

    QImage img(out, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p);
    p.end();
    m_image = img;
}

void ThumbnailResponse::generateImageThumbnail()
{
    QSize targetSize = m_requestedSize.isValid() ? m_requestedSize : QSize(80, 80);

    QImageReader reader;
    QBuffer buffer;
    QByteArray imageData;
    if (isTrashUri(m_id)) {
        imageData = readTrashUriData(m_id);
        if (imageData.isEmpty())
            return;

        buffer.setData(imageData);
        if (!buffer.open(QIODevice::ReadOnly))
            return;
        reader.setDevice(&buffer);
    } else {
        reader.setFileName(m_id);
    }

    reader.setAutoTransform(true);

    QSize imageSize = reader.size();
    if (imageSize.isValid() && (imageSize.width() > targetSize.width() ||
                                imageSize.height() > targetSize.height())) {
        reader.setScaledSize(imageSize.scaled(targetSize, Qt::KeepAspectRatio));
    }

    m_image = reader.read();
}

void ThumbnailResponse::generateVideoThumbnail()
{
    QSize targetSize = m_requestedSize.isValid() ? m_requestedSize : QSize(80, 80);

    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpFile.fileTemplate() + ".png");
    if (!tmpFile.open())
        return;
    QString tmpPath = tmpFile.fileName();
    tmpFile.close();

    QProcess proc;
    proc.start("ffmpeg", {
        "-v", "error",
        "-nostdin",
        "-threads", "1",
        "-ss", "1",            // seek to 1 second
        "-i", m_id,
        "-vframes", "1",       // extract 1 frame
        "-vf", QString("scale=%1:%2:force_original_aspect_ratio=decrease")
                   .arg(targetSize.width()).arg(targetSize.height()),
        "-y", tmpPath
    });

    if (!proc.waitForFinished(5000))
        return;

    if (proc.exitCode() == 0)
        m_image.load(tmpPath);

    QFile::remove(tmpPath);
}

QQuickTextureFactory *ThumbnailResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString ThumbnailResponse::errorString() const
{
    // A plain folder ends Ready with a zero-size image instead: the views
    // already show the theme icon for that, and Qt logs every error.
    if (m_image.isNull() && !m_folderWithoutIcon)
        return QStringLiteral("No thumbnail for %1").arg(m_id);
    return {};
}

// ---------------------------------------------------------------------------
// ThumbnailProvider
// ---------------------------------------------------------------------------

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                              const QSize &requestedSize)
{
    return new ThumbnailResponse(id, requestedSize);
}
