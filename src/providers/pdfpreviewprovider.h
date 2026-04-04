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
