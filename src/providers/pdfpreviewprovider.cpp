#include "providers/pdfpreviewprovider.h"

#include <QQuickTextureFactory>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

#include <poppler-qt6.h>

namespace {

struct PdfRequest {
    QString path;
    int page = 0;
};

PdfRequest parseRequest(const QString &id)
{
    PdfRequest request;
    const int queryIndex = id.indexOf('?');
    const QString encodedPath = queryIndex >= 0 ? id.left(queryIndex) : id;
    request.path = QUrl::fromPercentEncoding(encodedPath.toUtf8());

    if (queryIndex >= 0) {
        QUrlQuery query;
        query.setQuery(id.mid(queryIndex + 1));
        request.page = query.queryItemValue("page").toInt();
    }

    return request;
}

double dpiForRequest(const QSizeF &pageSizePoints, const QSize &requestedSize)
{
    if (!pageSizePoints.isValid())
        return 120.0;

    double scale = 1.0;
    if (requestedSize.width() > 0 && requestedSize.height() > 0) {
        const double xScale = requestedSize.width() / pageSizePoints.width();
        const double yScale = requestedSize.height() / pageSizePoints.height();
        scale = std::min(xScale, yScale);
    } else if (requestedSize.width() > 0) {
        scale = requestedSize.width() / pageSizePoints.width();
    } else if (requestedSize.height() > 0) {
        scale = requestedSize.height() / pageSizePoints.height();
    }

    scale = std::max(0.2, scale);
    return 72.0 * scale;
}

}

PdfPreviewResponse::PdfPreviewResponse(const QString &id, const QSize &requestedSize)
    : m_id(id)
    , m_requestedSize(requestedSize)
{
    setAutoDelete(false);
    QThreadPool::globalInstance()->start(this);
}

void PdfPreviewResponse::run()
{
    const PdfRequest request = parseRequest(m_id);
    if (request.path.isEmpty()) {
        emit finished();
        return;
    }

    std::unique_ptr<Poppler::Document> document = Poppler::Document::load(request.path);
    if (!document || request.page < 0 || request.page >= document->numPages()) {
        emit finished();
        return;
    }

    std::unique_ptr<Poppler::Page> page = document->page(request.page);
    if (!page) {
        emit finished();
        return;
    }

    const double dpi = dpiForRequest(page->pageSizeF(), m_requestedSize);
    m_image = page->renderToImage(dpi, dpi);
    emit finished();
}

QQuickTextureFactory *PdfPreviewResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QQuickImageResponse *PdfPreviewProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    return new PdfPreviewResponse(id, requestedSize);
}
