#include "thumbnailprovider.h"

#include <QFile>
#include <QThreadPool>
#include <QBuffer>
#include <QImageReader>
#include <QImage>
#include <QMimeDatabase>
#include <QMimeType>
#include <QQuickTextureFactory>
#include <QProcess>
#include <QFileInfo>
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

static bool isVideoFile(const QString &path)
{
    // Use QMimeDatabase so we don't misclassify ambiguous extensions like
    // .ts (which is both MPEG transport stream *and* TypeScript). For
    // local files this content-sniffs when the glob is ambiguous; for
    // trash:// URIs we just match by name.
    static QMimeDatabase mimeDb;
    const QUrl url(path);
    QMimeType mime;
    if (url.scheme() == QLatin1String("file") || url.scheme().isEmpty())
        mime = mimeDb.mimeTypeForFile(url.scheme().isEmpty() ? path : url.toLocalFile());
    else
        mime = mimeDb.mimeTypeForFile(url.fileName(), QMimeDatabase::MatchExtension);
    return mime.isValid() && mime.name().startsWith(QLatin1String("video/"));
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
    if (isVideoFile(m_id)) {
        generateVideoThumbnail();
    } else {
        generateImageThumbnail();
    }
    emit finished();
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
