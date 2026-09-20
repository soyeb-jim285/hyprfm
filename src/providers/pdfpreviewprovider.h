#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QImage>
#include <QSize>
#include <QRunnable>
#include <QString>

class PdfPreviewResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT

public:
    PdfPreviewResponse(const QString &id, const QSize &requestedSize);

    void run() override;
    QQuickTextureFactory *textureFactory() const override;

private:
    // Returns true and fills m_image when this page is already rendered.
    bool tryCache(const QString &key);
    // Same, from the on-disk cache of rendered pages; fills the memory cache
    // on the way through.
    bool tryDiskCache(const QString &key);
    void writeDiskCache(const QString &key, const QByteArray &jpeg);

    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
};

class PdfPreviewProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
};
