#include "providers/pdfpreviewprovider.h"

#include <QFileInfo>
#include <QProcess>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

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

// pdfinfo prints "Page size:   595.28 x 841.89 pts (A4)" on one line.
QSizeF pageSizePoints(const QString &path)
{
    QProcess proc;
    proc.start(QStringLiteral("pdfinfo"), {path});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(
        QStringLiteral(R"(Page size:\s*([0-9.]+)\s*x\s*([0-9.]+)\s*pts)"));
    const auto m = re.match(out);
    if (!m.hasMatch())
        return {};
    return QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble());
}

double dpiForRequest(const QSizeF &pageSizePts, const QSize &requestedSize)
{
    if (!pageSizePts.isValid() || pageSizePts.isEmpty())
        return 120.0;

    double scale = 1.0;
    if (requestedSize.width() > 0 && requestedSize.height() > 0) {
        const double xScale = requestedSize.width() / pageSizePts.width();
        const double yScale = requestedSize.height() / pageSizePts.height();
        scale = std::min(xScale, yScale);
    } else if (requestedSize.width() > 0) {
        scale = requestedSize.width() / pageSizePts.width();
    } else if (requestedSize.height() > 0) {
        scale = requestedSize.height() / pageSizePts.height();
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
    if (request.path.isEmpty() || !QFileInfo::exists(request.path)) {
        emit finished();
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        emit finished();
        return;
    }

    const QSizeF sizePts = pageSizePoints(request.path);
    const double dpi = dpiForRequest(sizePts, m_requestedSize);

    // pdftoppm writes PNG to stdout when the output root is "-".
    QProcess proc;
    proc.start(QStringLiteral("pdftoppm"), {
        QStringLiteral("-png"),
        QStringLiteral("-f"), QString::number(request.page + 1),
        QStringLiteral("-l"), QString::number(request.page + 1),
        QStringLiteral("-r"), QString::number(static_cast<int>(dpi + 0.5)),
        request.path,
        QStringLiteral("-"),
    });

    if (!proc.waitForFinished(15000) || proc.exitCode() != 0) {
        emit finished();
        return;
    }

    const QByteArray png = proc.readAllStandardOutput();
    if (png.isEmpty()) {
        emit finished();
        return;
    }

    m_image.loadFromData(png, "PNG");
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
