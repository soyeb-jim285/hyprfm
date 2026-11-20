#pragma once

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <cmath>

// Strata's SVGs stay unchanged on disk. Resolve palette variables and stroke
// weights at render time, using the same rules for views, chrome and drags.
namespace StrataIcons {
inline QJsonObject json(const QString &file)
{
    QFile source(":/HyprFM/icons/strata/" + file);
    return source.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(source.readAll()).object() : QJsonObject{};
}

inline QString fileIcon(const QString &filename)
{
    static const QJsonObject associations = json("associations.json");
    const QString name = QFileInfo(filename).fileName();
    const QString lower = name.toLower();
    const auto names = associations.value("names").toObject();
    if (names.contains(lower))
        return names.value(lower).toString();
    for (const auto &value : associations.value("patterns").toArray()) {
        const auto pattern = value.toArray();
        if (name.startsWith(pattern[0].toString()) && name.endsWith(pattern[1].toString()))
            return pattern[2].toString();
    }
    const auto extensions = associations.value("extensions").toObject();
    for (int dot = lower.indexOf('.'); dot >= 0; dot = lower.indexOf('.', dot + 1)) {
        const QString suffix = lower.mid(dot + 1);
        if (extensions.contains(suffix))
            return extensions.value(suffix).toString();
    }
    return {};
}

inline QString resolve(const QString &icon, const QString &filename)
{
    if (icon.startsWith("strata-")) return icon;
    if (icon == "folder" || icon.startsWith("folder-") || icon.contains("directory")) return "strata-folder";
    if (!filename.isEmpty()) {
        const QString associated = fileIcon(filename);
        if (!associated.isEmpty()) return associated;
    }
    if (icon.startsWith("image-")) return "strata-image";
    if (icon.startsWith("video-")) return "strata-video";
    if (icon.startsWith("audio-")) return "strata-audio-lines";
    if (icon.contains("pdf")) return "strata-file-text";
    if (icon.contains("archive") || icon.contains("compressed") || icon.contains("zip")) return "strata-file-archive";
    if (icon.contains("spreadsheet")) return "strata-file-spreadsheet";
    if (icon.contains("presentation")) return "strata-presentation";
    if (icon.contains("executable") || icon.contains("package")) return "strata-box";
    if (icon.startsWith("text-") || icon.startsWith("application-") || !filename.isEmpty()) return "strata-file-text";
    return {}; // Application logos and desktop icons still use the system pack.
}

inline QString scaleStrokes(const QString &source, double factor)
{
    static const QRegularExpression width("stroke-width=\"([0-9.]+)\"");
    QString result;
    qsizetype offset = 0;
    auto matches = width.globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += source.mid(offset, match.capturedStart() - offset);
        result += "stroke-width=\"" + QString::number(match.captured(1).toDouble() * factor) + "\"";
        offset = match.capturedEnd();
    }
    result += source.mid(offset);
    if (!source.left(source.indexOf('>')).contains("stroke-width="))
        result.replace("<svg ", "<svg stroke-width=\"" + QString::number(factor) + "\" ");
    return result;
}

inline QImage render(const QString &name, int pixels, const QColor &accent,
                     const QColor &background, bool themeColors, int logicalSize, bool grid, double strokeScale = 1.0)
{
    const bool catppuccin = name.startsWith("strata-ctp-");
    QFile file(":/HyprFM/icons/strata/" + QString(catppuccin ? "catppuccin/" : "actions/") + name + ".svg");
    if (!file.open(QIODevice::ReadOnly)) return {};
    QString source = QString::fromUtf8(file.readAll());
    const QString color = accent.name();
    const double weight = strokeScale * (grid ? 0.5 : 1.0) * std::pow(64.0 / qMax(64, logicalSize), 0.35);
    if (catppuccin) {
        static const QJsonObject palettes = json("palettes.json");
        const bool dark = background.red() * 299 + background.green() * 587 + background.blue() * 114 < 128000;
        const auto palette = palettes.value(dark ? "mocha" : "latte").toObject();
        for (auto it = palette.begin(); it != palette.end(); ++it)
            source.replace("var(--vscode-ctp-" + it.key() + ")", themeColors ? color : it.value().toString());
        if (themeColors)
            source.replace("fill=\"none\"", "fill=\"" + color + "\" fill-opacity=\"0.15\"");
        source = scaleStrokes(source, weight * (grid ? 4.0 / 3.0 : 1.0));
    } else {
        for (const QString &original : {QString("#8bc9eb"), QString("#22d3ee"), QString("#2e3436")})
            source.replace(original, color);
        if (name == "strata-folder")
            source.replace("fill=\"none\"", "fill=\"" + color + "\" fill-opacity=\"0.15\"");
        source.replace("stroke-width=\"2\"", "stroke-width=\"" + QString::number(2 * weight) + "\"");
        source.replace("stroke-width=\"2.7\"", "stroke-width=\"" + QString::number(2.7 * weight) + "\"");
    }
    QSvgRenderer renderer(source.toUtf8());
    if (!renderer.isValid()) return {};
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    if (!catppuccin || themeColors) painter.setOpacity(accent.alphaF());
    renderer.render(&painter);
    painter.end();
    return image;
}
}
