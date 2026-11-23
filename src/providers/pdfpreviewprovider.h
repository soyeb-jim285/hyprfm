#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QImage>
#include <QSize>
#include <QRunnable>
#include <QString>
#include <atomic>

class QThreadPool;

class PdfPreviewResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT

public:
    PdfPreviewResponse(const QString &id, const QSize &requestedSize);

    void cancel() override;
    void run() override;
    QQuickTextureFactory *textureFactory() const override;

private:
    // Emits finished() on this object's own thread, never from run(). On a
    // cache hit run() is done in microseconds, so a direct emit could land
    // before the caller of requestImageResponse() has connected to it, and
    // the image would never arrive.
    void finish();
    // Returns true and fills m_image when this page is already rendered.
    bool tryCache(const QString &key);
    // Same, from the on-disk cache of rendered pages; fills the memory cache
    // on the way through.
    bool tryDiskCache(const QString &key);
    void writeDiskCache(const QString &key, const QByteArray &jpeg);

    QString m_path;
    int m_page = 0;
    QSize m_requestedSize;
    QImage m_image;
    QThreadPool *m_pool = nullptr;
    std::atomic_bool m_cancelled{false};
};

class PdfPreviewProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
};
