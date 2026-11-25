#include "previewservice.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFontMetricsF>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QUrl>

namespace {

// Leave an inline object in the text flow for selection and wrapping. The
// actual SVG is drawn by a Qt Quick item at that object's position.
const QString placeholder = QStringLiteral(
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"
    "AAAAC0lEQVQI12NgAAIAAAUAAeImBZsAAAAASUVORK5CYII=#hyprfm-");

class SvgFiles : public QObject
{
public:
    explicit SvgFiles(QObject *parent) : QObject(parent)
    {
        setObjectName(QStringLiteral("hyprfmMarkdownSvgFiles"));
    }

    QTemporaryDir dir;
};

struct PlacedImage
{
    int position;
    QTextImageFormat format;
    QUrl url;
};

}

QVariantMap PreviewService::prepareMarkdownGraphics(QObject *document, const QString &html) const
{
    if (!document)
        return {{QStringLiteral("html"), html}};

    delete document->findChild<QObject *>(QStringLiteral("hyprfmMarkdownSvgFiles"),
                                          Qt::FindDirectChildrenOnly);
    static const QRegularExpression svg(QStringLiteral(
        R"re(\bsrc="data:image/svg\+xml;base64,([A-Za-z0-9+/=]+)#hyprfm-svg,(\d+),(\d+),([0-9.]+),([0-9.]+)")re"));

    SvgFiles *files = nullptr;
    QString prepared;
    QVariantList images;
    qsizetype offset = 0;
    auto tags = svg.globalMatch(html);
    while (tags.hasNext()) {
        const auto tag = tags.next();
        if (images.size() >= 256 || tag.capturedLength(1) > 512 * 1024)
            continue;
        const QSize size(tag.captured(2).toInt(), tag.captured(3).toInt());
        if (size.isEmpty() || size.width() > 8192 || size.height() > 4096)
            continue;
        if (!files)
            files = new SvgFiles(document);
        if (!files->dir.isValid())
            break;
        QFile file(files->dir.filePath(QString::number(images.size()) + QStringLiteral(".svg")));
        const QByteArray bytes = QByteArray::fromBase64(tag.captured(1).toLatin1());
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            continue;

        prepared += QStringView(html).mid(offset, tag.capturedStart() - offset);
        prepared += QStringLiteral("src=\"%1%2\"").arg(placeholder).arg(images.size());
        offset = tag.capturedEnd();
        images.append(QVariantMap{
            {QStringLiteral("url"), QUrl::fromLocalFile(file.fileName())},
            {QStringLiteral("size"), size},
            {QStringLiteral("xHeight"), tag.captured(4).toDouble()},
            {QStringLiteral("descent"), tag.captured(5).toDouble()}
        });
    }
    prepared += QStringView(html).mid(offset);
    return {{QStringLiteral("html"), prepared}, {QStringLiteral("images"), images}};
}

QVariantList PreviewService::layoutMarkdownGraphics(QObject *document, const QVariantList &images,
                                                    qreal zoom, qreal width) const
{
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(document);
    if (!quickDocument || images.isEmpty())
        return {};
    QTextDocument *doc = quickDocument->textDocument();
    if (!doc)
        return {};

    const QFont textFont = doc->defaultFont();
    const QFontMetricsF metrics(textFont);
    QList<PlacedImage> placed;
    for (auto block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.charFormat().isImageFormat())
                continue;
            auto format = fragment.charFormat().toImageFormat();
            if (!format.name().startsWith(placeholder))
                continue;
            bool ok = false;
            const int index = QStringView(format.name()).mid(placeholder.size()).toInt(&ok);
            if (!ok || index < 0 || index >= images.size())
                continue;
            const auto image = images.at(index).toMap();
            const QSize size = image.value(QStringLiteral("size")).toSize();
            const qreal xHeight = image.value(QStringLiteral("xHeight")).toReal();
            qreal normalScale = xHeight > 0 ? metrics.xHeight() / (xHeight * zoom) : 1;
            if (xHeight == 0) {
                // Preserve explicit dimensions on linked Markdown artwork.
                // Once laid out, Width and Height include preview zoom.
                constexpr int originalScale = QTextFormat::UserProperty;
                if (!format.hasProperty(originalScale)) {
                    const qreal w = format.width() > 0 ? format.width() / size.width() : 0;
                    const qreal h = format.height() > 0 ? format.height() / size.height() : 0;
                    const qreal requested = w > 0 && h > 0 ? qMin(w, h) : qMax(w, h);
                    format.setProperty(originalScale, requested > 0 ? requested : 1);
                }
                normalScale = format.doubleProperty(originalScale);
            }
            // Fit at 100%. Above that size the preview can scroll sideways.
            const qreal scale = qMin(normalScale, qMax<qreal>(1, width) / size.width()) * zoom;
            format.setWidth(size.width() * scale);
            format.setHeight(size.height() * scale);

            const qreal descent = image.value(QStringLiteral("descent")).toReal() * scale;
            if (descent > 0) {
                // Qt aligns image objects using the font descent of their
                // character format. Match that descent to the SVG baseline;
                // the object's private font does not affect adjacent text.
                QFont alignmentFont = textFont;
                if (textFont.pointSizeF() > 0 && metrics.descent() > 0)
                    alignmentFont.setPointSizeF(qMax<qreal>(1, textFont.pointSizeF() * descent / metrics.descent()));
                format.setFont(alignmentFont);
                format.setVerticalAlignment(QTextCharFormat::AlignBaseline);
            }
            placed.append({fragment.position(), format, image.value(QStringLiteral("url")).toUrl()});
        }
    }

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (const auto &image : placed) {
        cursor.setPosition(image.position);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (cursor.charFormat() != image.format)
            cursor.setCharFormat(image.format);
    }
    cursor.endEditBlock();

    auto *layout = doc->documentLayout();
    layout->documentSize();
    QVariantList rectangles;
    for (const auto &image : placed) {
        const QTextBlock block = doc->findBlock(image.position);
        const int positionInBlock = image.position - block.position();
        const QTextLine line = block.layout()->lineForTextPosition(positionInBlock);
        if (!line.isValid())
            continue;
        const QRectF blockRect = layout->blockBoundingRect(block);
        const qreal descent = image.format.verticalAlignment() == QTextCharFormat::AlignBaseline
            ? QFontMetricsF(image.format.font()).descent() : 0;
        const qreal x = blockRect.left() + line.cursorToX(positionInBlock);
        const qreal y = blockRect.top() + line.y() + line.ascent() - image.format.height() + descent;
        rectangles.append(QVariantMap{
            {QStringLiteral("url"), image.url},
            {QStringLiteral("rect"), QRectF(x, y, image.format.width(), image.format.height())}
        });
    }
    return rectangles;
}
