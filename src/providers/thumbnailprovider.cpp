#include "thumbnailprovider.h"

#include <QThreadPool>
#include <QImageReader>
#include <QImage>
#include <QQuickTextureFactory>

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
    emit finished();
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
