#include "thumbnailprovider.h"

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
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>

static bool isTrashUri(const QString &path)
{
    return QUrl(path).scheme() == "trash";
}

static QString sourcePathFromId(QString id)
{
    const int queryIndex = id.indexOf(QLatin1Char('?'));
    if (queryIndex >= 0)
        id.truncate(queryIndex);
    return id;
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
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0)
        return {};

    return proc.readAllStandardOutput();
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
// ThumbnailResponse
// ---------------------------------------------------------------------------

ThumbnailResponse::ThumbnailResponse(const QString &id, const QSize &requestedSize)
    : m_id(sourcePathFromId(id))
    , m_requestedSize(requestedSize)
{
    setAutoDelete(false);
    thumbnailPool()->start(this);
}

void ThumbnailResponse::run()
{
    const QSize targetSize = m_requestedSize.isValid()
        ? m_requestedSize : QSize(128, 128);

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

void ThumbnailResponse::generateSvgThumbnail()
{
    const QSize targetSize = m_requestedSize.isValid() ? m_requestedSize
                                                       : QSize(128, 128);

    QSvgRenderer renderer;
    bool loaded = false;
    if (isTrashUri(m_id)) {
        const QByteArray data = readTrashUriData(m_id);
        if (data.isEmpty())
            return;
        loaded = renderer.load(data);
    } else {
        loaded = renderer.load(m_id);
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

// ---------------------------------------------------------------------------
// ThumbnailProvider
// ---------------------------------------------------------------------------

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                              const QSize &requestedSize)
{
    return new ThumbnailResponse(id, requestedSize);
}
