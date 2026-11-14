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
    QString m_iconTheme;   // "theme=" in the id, for .directory Icon=<name>
    bool m_folderWithoutIcon = false;   // plain folder: empty, but not an error
};

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
};
