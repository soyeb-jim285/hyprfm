#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QString>
#include <QSize>
#include <QRunnable>
#include <QImage>

class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT
public:
    ThumbnailResponse(const QString &id, const QSize &requestedSize);

    void run() override;
    QQuickTextureFactory *textureFactory() const override;

private:
    void generateImageThumbnail();
    void generateSvgThumbnail();
    void generateVideoThumbnail();

    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
};

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
};
