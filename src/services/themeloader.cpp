#include "services/themeloader.h"
#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"
#include <QFile>
#include <QDir>
#include <QDebug>

QMap<QString, QColor> ThemeLoader::s_defaults = {
    {"base", QColor("#1e1e2e")}, {"mantle", QColor("#181825")},
    {"crust", QColor("#11111b")}, {"surface", QColor("#313244")},
    {"overlay", QColor("#45475a")}, {"text", QColor("#cdd6f4")},
    {"subtext", QColor("#bac2de")}, {"muted", QColor("#6c7086")},
    {"accent", QColor("#89b4fa")}, {"success", QColor("#a6e3a1")},
    {"warning", QColor("#f9e2af")}, {"error", QColor("#f38ba8")},
    {"purple", QColor("#cba6f7")},
};

ThemeLoader::ThemeLoader(QObject *parent) : QObject(parent), m_colors(s_defaults) {}

void ThemeLoader::loadTheme(const QString &nameOrPath, const QString &themesDir)
{
    m_colors = s_defaults;
    QString filePath;
    if (QFile::exists(nameOrPath)) {
        filePath = nameOrPath;
    } else if (!themesDir.isEmpty()) {
        filePath = QDir(themesDir).filePath(nameOrPath + ".toml");
    }
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qWarning() << "Theme not found:" << nameOrPath;
        emit themeChanged();
        return;
    }
    try {
        auto config = toml::parse_file(filePath.toStdString());
        if (auto colors = config["colors"].as_table()) {
            for (const auto &[key, val] : *colors) {
                if (auto v = val.value<std::string>()) {
                    QString colorStr = QString::fromStdString(*v);
                    QColor c(colorStr);
                    if (c.isValid())
                        m_colors[QString::fromStdString(std::string(key))] = c;
                }
            }
        }
    } catch (const toml::parse_error &err) {
        qWarning() << "Theme parse error:" << err.what();
    }
    emit themeChanged();
}

QColor ThemeLoader::color(const QString &name) const
{
    return m_colors.value(name, s_defaults.value(name, QColor("#ff00ff")));
}
