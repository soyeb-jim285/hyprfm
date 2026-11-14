#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QString>
#include <QSize>
#include <QRunnable>
#include <QImage>

class IconProvider;

class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT
public:
    // icons resolves theme-name folder icons (.directory Icon=name); may be null.
    ThumbnailResponse(const QString &id, const QSize &requestedSize,
                      IconProvider *icons = nullptr);

    void run() override;
    QQuickTextureFactory *textureFactory() const override;
    // A null image with no error reads as "loaded, nothing to draw" in QML
    // and leaves a blank tile; report it so Image goes to Error and the
    // delegate falls back to the icon.
    QString errorString() const override;

private:
    void generateImageThumbnail();
    void generateSvgThumbnail();
    void generateVideoThumbnail();

    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
    IconProvider *m_icons;
};

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    explicit ThumbnailProvider(IconProvider *icons = nullptr) : m_icons(icons) {}
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

private:
    IconProvider *m_icons;
};
