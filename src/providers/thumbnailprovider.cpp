#include "thumbnailprovider.h"

#include <QThreadPool>
#include <QImageReader>
#include <QImage>
#include <QQuickTextureFactory>
#include <QProcess>
#include <QTemporaryFile>
#include <QFileInfo>

static const QStringList kVideoExtensions = {
    "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "3gp", "ts",
};

static bool isVideoFile(const QString &path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    return kVideoExtensions.contains(ext);
}

// ---------------------------------------------------------------------------
// ThumbnailResponse
// ---------------------------------------------------------------------------

ThumbnailResponse::ThumbnailResponse(const QString &id, const QSize &requestedSize)
    : m_id(id)
    , m_requestedSize(requestedSize)
{
    setAutoDelete(false);
    QThreadPool::globalInstance()->start(this);
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
    QImageReader reader(m_id);
    reader.setAutoTransform(true);

    QSize targetSize = m_requestedSize.isValid() ? m_requestedSize : QSize(80, 80);

    // Only scale down, never up
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
