#include "markdownpreview.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

#ifdef HYPRFM_MARKDOWN_MATH
#include "outlinesvg.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QtMath>
#include <jkqtmathtext/jkqtmathtext.h>
#endif

namespace {

QString md4cHtml(const QString &markdown)
{
    static const QString md2html = QStandardPaths::findExecutable(QStringLiteral("md2html"));
    if (!md2html.isEmpty()) {
        QProcess proc;
        proc.start(md2html, {QStringLiteral("--github")});
        if (proc.waitForStarted(2000)) {
            proc.write(markdown.toUtf8());
            proc.closeWriteChannel();
            if (proc.waitForFinished(5000) && proc.exitStatus() == QProcess::NormalExit
                && proc.exitCode() == 0) {
                return QString::fromUtf8(proc.readAllStandardOutput());
            }
            if (proc.state() != QProcess::NotRunning) {
                proc.kill();
                proc.waitForFinished();
            }
        }
    }

    return {};
}

// md2html escapes code verbatim. Decode a single layer so that entities within
// the SVG itself, such as &amp; in a label, stay intact.
QString unescapeCode(QString code)
{
    return code.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
}

QString svgDataUrl(const QByteArray &svg, const QSize &size, qreal xHeight = 0, qreal descent = 0)
{
    return QStringLiteral("data:image/svg+xml;base64,%1#hyprfm-svg,%2,%3,%4,%5")
        .arg(QString::fromLatin1(svg.toBase64())).arg(size.width()).arg(size.height())
        .arg(xHeight).arg(descent);
}

QString imageTag(const QString &source, const QString &description)
{
    return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(source, description.toHtmlEscaped());
}

QString diagramUrl(const QString &source)
{
    if (source.size() > 128 * 1024)
        return {};

    // Permit self-contained SVG geometry. Qt's SVG loader also understands
    // embedded images and references, which a Markdown preview must not load.
    static const QSet<QString> elements{
        QStringLiteral("svg"), QStringLiteral("title"), QStringLiteral("desc"),
        QStringLiteral("defs"), QStringLiteral("g"), QStringLiteral("path"),
        QStringLiteral("rect"), QStringLiteral("circle"), QStringLiteral("ellipse"),
        QStringLiteral("line"), QStringLiteral("polyline"), QStringLiteral("polygon"),
        QStringLiteral("text"), QStringLiteral("tspan"), QStringLiteral("use"),
        QStringLiteral("symbol"), QStringLiteral("clipPath"), QStringLiteral("mask"),
        QStringLiteral("linearGradient"), QStringLiteral("radialGradient"), QStringLiteral("stop"),
        QStringLiteral("marker"), QStringLiteral("pattern")
    };
    static const QRegularExpression externalUrl(
        QStringLiteral(R"(url\s*\(\s*['"]?\s*[^#'"\s])"),
        QRegularExpression::CaseInsensitiveOption);
    QXmlStreamReader xml(source.trimmed());
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isDTD() || xml.isEntityReference() || xml.isProcessingInstruction())
            return {};
        if (!xml.isStartElement())
            continue;
        if (!elements.contains(xml.name().toString()))
            return {};
        for (const auto &attribute : xml.attributes()) {
            if ((attribute.name() == QLatin1String("href") && !attribute.value().startsWith(u'#'))
                || externalUrl.matchView(attribute.value()).hasMatch()) {
                return {};
            }
        }
    }
    if (xml.hasError())
        return {};

    const QByteArray svg = source.trimmed().toUtf8();
    QSvgRenderer renderer(svg);
    const QSize size = renderer.defaultSize();
    if (!renderer.isValid() || size.isEmpty() || size.width() > 4096 || size.height() > 4096)
        return {};
    return svgDataUrl(svg, size);
}

struct Equation
{
    QString source;
    QString original;
    bool display;
};

QString equationImage(const Equation &equation, bool lightTheme)
{
#ifdef HYPRFM_MARKDOWN_MATH
    // Bound work on the preview thread, including malformed or partial input.
    if (equation.source.size() > 4096 || equation.source.count(u'{') > 256)
        return {};

    JKQTMathText math;
    math.useXITS();
    // Match Sioyek's note typography. The 10 pt optical master has genuine
    // italic letterforms. The math face supplies operators and symbols.
    const auto encoding = JKQTMathTextFontEncoding::MTFEUnicode;
    static const auto families = QFontDatabase::families();
    const QString latinMath = QStringLiteral("Latin Modern Math");
    if (families.contains(latinMath)) {
        math.setFontRomanAndMath(latinMath, encoding);
        math.setFallbackFontSymbols(latinMath, encoding);
        if (families.contains(QStringLiteral("LM Roman 10")))
            math.setFontMathRoman(QStringLiteral("LM Roman 10"), encoding);
    }
    if (families.contains(QStringLiteral("TeX Gyre Chorus")))
        math.setFontCaligraphic(QStringLiteral("TeX Gyre Chorus"), encoding);

    constexpr int em = 48;
    math.setFontSizePixels(equation.display ? 60 : em);
    math.setFontColor(lightTheme ? QColor(Qt::black) : QColor(Qt::white));
    if (!math.parse(equation.source.trimmed(), JKQTMathText::DefaultParser,
                    JKQTMathText::StartWithMathMode) || !math.getErrorList().isEmpty()) {
        return {};
    }

    QImage metrics(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter measuring(&metrics);
    const auto dimensions = math.getSizeDetail(measuring);
    constexpr int padding = 2;
    const QSize size(qCeil(dimensions.width) + 2 * padding,
                     qCeil(dimensions.getAscent() + dimensions.getDescent()) + 2 * padding);
    if (size.isEmpty() || size.width() > 8192 || size.height() > 3072)
        return {};

    QByteArray svg;
    QBuffer buffer(&svg);
    buffer.open(QIODevice::WriteOnly);
    OutlineSvg generator(&buffer, size);
    QPainter painter(&generator);
    math.draw(painter, padding, padding + dimensions.getAscent());
    painter.end();

    QFont font(math.getFontMathRoman());
    font.setPixelSize(em);
    const qreal xHeight = QFontMetricsF(font, &metrics).xHeight();
    const qreal descent = equation.display ? 0 : size.height() - padding - dimensions.getAscent();
    const QString image = imageTag(svgDataUrl(svg, size, xHeight, descent), equation.original);
    return equation.display ? QStringLiteral("<br>%1<br>").arg(image) : image;
#else
    Q_UNUSED(equation);
    Q_UNUSED(lightTheme);
    return {};
#endif
}

class MarkdownGraphics
{
public:
    explicit MarkdownGraphics(bool lightTheme)
        : m_lightTheme(lightTheme)
    {
    }

    QString toHtml(const QString &markdown)
    {
        const QString protectedText = protectEquations(markdown);
        const QString html = md4cHtml(protectedText);
        return render(html.isEmpty() ? qtHtml(protectedText) : html);
    }

private:
    QString qtHtml(const QString &markdown) const
    {
        QTextDocument doc;
        doc.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
        // Qt exports inline code as styled spans. Restore code literals using
        // the imported character format before that distinction is lost.
        QList<QPair<QTextCursor, QString>> codeSpans;
        if (!m_equations.isEmpty()) {
            for (auto block = doc.begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    if (!fragment.charFormat().fontFixedPitch() || !fragment.text().contains(m_prefix))
                        continue;
                    QTextCursor cursor(&doc);
                    cursor.setPosition(fragment.position());
                    cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                    codeSpans.append({cursor, restoreEquations(fragment.text(), false)});
                }
            }
        }
        for (auto &[cursor, text] : codeSpans)
            cursor.insertText(text);

        // The body tag pins a font and size that would override preview zoom.
        const QString html = doc.toHtml();
        const qsizetype body = html.indexOf(QLatin1String("<body"));
        const qsizetype start = body < 0 ? -1 : html.indexOf(QLatin1Char('>'), body);
        const qsizetype end = html.lastIndexOf(QLatin1String("</body>"));
        return start < 0 || end < start ? html : html.mid(start + 1, end - start - 1);
    }

    QString protectEquations(const QString &markdown)
    {
#ifndef HYPRFM_MARKDOWN_MATH
        return markdown;
#else
        // Keep TeX punctuation intact through Markdown parsing. The tokens
        // are resolved only in rendered text, with literal source restored in
        // code and HTML tags. Display spans cannot consume a blank line.
        static const QRegularExpression math(QStringLiteral(
            R"((?<![\\$])\$\$((?:(?!\n[ \t\r]*\n)[\s\S])+?)(?<!\\)\$\$(?!\$))"
            R"(|(?<!\\)\\\[((?:(?!\n[ \t\r]*\n)[\s\S])+?)(?<!\\)\\\])"
            R"(|(?<!\\)\\\(([^\n]+?)(?<!\\)\\\))"
            R"(|(?<![\\$])\$(?![\s$])((?:\\.|[^$\n\\])+?)(?<!\s)\$(?![\d$]))"));
        QString protectedText;
        qsizetype pos = 0;
        auto matches = math.globalMatch(markdown);
        while (matches.hasNext() && m_equations.size() < 128) {
            const auto match = matches.next();
            protectedText += QStringView(markdown).mid(pos, match.capturedStart() - pos);
            protectedText += m_prefix + QString::number(m_equations.size()) + u'Z';
            const int capture = match.lastCapturedIndex();
            m_equations.append({match.captured(capture), match.captured(), capture <= 2});
            pos = match.capturedEnd();
        }
        return protectedText + QStringView(markdown).mid(pos);
#endif
    }

    QString render(const QString &html) const
    {
        // Match complete code blocks before their contents so a fenced example
        // stays literal. md2html supplies the language even inside lists and
        // quotes, without us having to parse Markdown block indentation.
        const QRegularExpression markup(QStringLiteral(
            R"re(<!--.*?-->|<pre\b[^>]*>(?:<code class="language-(svg|math|latex)">)?(.*?)(?:</code>)?</pre>)re"
            R"(|(<svg\b[^>]*\s*/>|<svg\b[^>]*>.*?</svg\s*>)|<code\b[^>]*>.*?</code>)"
            R"(|<(?:"[^"]*"|'[^']*'|[^'">])*>|%1(\d+)Z)").arg(m_prefix),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

        QString output;
        qsizetype pos = 0;
        auto matches = markup.globalMatch(html);
        while (matches.hasNext()) {
            const auto match = matches.next();
            output += QStringView(html).mid(pos, match.capturedStart() - pos);
            if (match.hasCaptured(4)) {
                const Equation &equation = m_equations.at(match.captured(4).toInt());
                const QString image = equationImage(equation, m_lightTheme);
                output += image.isEmpty() ? equation.original.toHtmlEscaped() : image;
            } else if (match.hasCaptured(1)) {
                const QString source = unescapeCode(restoreEquations(match.captured(2)));
                QString image;
                if (match.captured(1).compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0) {
                    const QString url = diagramUrl(source);
                    if (!url.isEmpty())
                        image = imageTag(url, QStringLiteral("SVG diagram"));
                } else {
                    image = equationImage({source, source, true}, m_lightTheme);
                }
                output += image.isEmpty() ? restoreEquations(match.captured()) : image;
            } else if (match.hasCaptured(3)) {
                const QString svg = restoreEquations(match.captured(), false);
                const QString url = diagramUrl(svg);
                output += url.isEmpty() ? svg.toHtmlEscaped() : imageTag(url, QStringLiteral("SVG diagram"));
            } else {
                output += restoreEquations(match.captured());
            }
            pos = match.capturedEnd();
        }
        return output + QStringView(html).mid(pos);
    }

    QString restoreEquations(QString text, bool escape = true) const
    {
        if (m_equations.isEmpty() || !text.contains(m_prefix))
            return text;
        auto matches = m_tokens.globalMatch(text);
        qsizetype shift = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            const QString &original = m_equations.at(match.captured(1).toInt()).original;
            const QString source = escape ? original.toHtmlEscaped() : original;
            text.replace(match.capturedStart() + shift, match.capturedLength(), source);
            shift += source.size() - match.capturedLength();
        }
        return text;
    }

    const bool m_lightTheme;
    const QString m_prefix = QStringLiteral("HyprfmEquation")
        + QUuid::createUuid().toString(QUuid::Id128) + u'X';
    const QRegularExpression m_tokens{m_prefix + QStringLiteral("(\\d+)Z")};
    QList<Equation> m_equations;
};

}

QString renderMarkdownPreview(const QString &markdown, bool lightTheme, const QString &path)
{
    QString html = MarkdownGraphics(lightTheme).toHtml(markdown);
    html.replace(QStringLiteral("<table>"),
                 QStringLiteral("<table border=\"1\" cellpadding=\"4\" cellspacing=\"0\">"));

    // Qt rich text ignores HTML form controls. Keep the task status visible
    // as a glyph, inside the paragraph for loose lists.
    static const QRegularExpression checked(
        QStringLiteral(R"(<input type="checkbox" class="task-list-item-checkbox" disabled checked>(<p>)?)"));
    static const QRegularExpression unchecked(
        QStringLiteral(R"(<input type="checkbox" class="task-list-item-checkbox" disabled>(<p>)?)"));
    html.replace(checked, QStringLiteral("\\1&#9745;&nbsp;"));
    html.replace(unchecked, QStringLiteral("\\1&#9744;&nbsp;"));

    // QQuickPixmap can fetch remote images. Opening a document preview must
    // not send a network request, including for raw HTML image tags.
    static const QRegularExpression remoteImage(
        QStringLiteral(R"(<img\b[^>]*\bsrc\s*=\s*["']?\s*(?:[a-z][a-z0-9+.-]*:)?//[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(remoteImage);
    // Resolve linked SVGs on the preview worker with the same size and
    // self-contained artwork checks as embedded diagrams. Keep the original
    // image link as the fallback for files Qt's rich text already handled.
    static const QRegularExpression linkedImage(
        QStringLiteral(R"re(<img\b[^>]*?\bsrc\s*=\s*["']([^"']+)["'][^>]*>)re"),
        QRegularExpression::CaseInsensitiveOption);
    const QUrl base = QUrl::fromLocalFile(path).adjusted(QUrl::RemoveFilename);
    auto sources = linkedImage.globalMatch(html);
    QString withVectors;
    qsizetype offset = 0;
    while (sources.hasNext()) {
        const auto match = sources.next();
        const QUrl url = base.resolved(QUrl(unescapeCode(match.captured(1))));
        if (!url.isLocalFile() || !url.path().endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
            continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile() || info.size() > 128 * 1024)
            continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString source = diagramUrl(QString::fromUtf8(file.read(128 * 1024 + 1)));
        if (source.isEmpty())
            continue;
        withVectors += QStringView(html).mid(offset, match.capturedStart(1) - offset);
        withVectors += source;
        offset = match.capturedEnd(1);
    }
    return withVectors + QStringView(html).mid(offset);
}
