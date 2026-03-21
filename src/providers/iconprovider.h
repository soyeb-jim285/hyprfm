#pragma once

#include <QQuickImageProvider>
#include <QSvgRenderer>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QDebug>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image)
    {
        // Find icon theme directories
        QStringList themes = {"Adwaita", "hicolor"};
        QStringList searchDirs = {"/usr/share/icons", "/usr/local/share/icons"};

        // Also check XDG_DATA_DIRS
        QString xdgDirs = qEnvironmentVariable("XDG_DATA_DIRS", "/usr/share:/usr/local/share");
        for (const auto &dir : xdgDirs.split(':'))
            searchDirs.append(dir + "/icons");

        for (const auto &theme : themes) {
            for (const auto &dir : searchDirs) {
                QString path = dir + "/" + theme;
                if (QDir(path).exists())
                    m_themeDirs.append(path);
            }
        }
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        int sz = (requestedSize.width() > 0) ? requestedSize.width() : 48;
        QSize iconSize(sz, sz);

        // Try to find the icon file
        QString svgPath = findIcon(id, sz);
        if (svgPath.isEmpty() && id != "text-x-generic")
            svgPath = findIcon("text-x-generic", sz);

        QImage img(iconSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        if (!svgPath.isEmpty()) {
            if (svgPath.endsWith(".svg") || svgPath.endsWith(".svgz")) {
                QSvgRenderer renderer(svgPath);
                if (renderer.isValid()) {
                    QPainter painter(&img);
                    renderer.render(&painter);
                    painter.end();
                }
            } else {
                // PNG or other raster format
                QImage loaded(svgPath);
                if (!loaded.isNull()) {
                    img = loaded.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
            }
        }

        if (size)
            *size = iconSize;
        return img;
    }

private:
    QString findIcon(const QString &name, int size) const
    {
        // Categories to search in order
        static const QStringList categories = {
            "places", "mimetypes", "devices", "actions",
            "status", "apps", "categories", "emblems"
        };
        static const QStringList extensions = {".svg", ".png"};

        for (const auto &themeDir : m_themeDirs) {
            // Try scalable first (SVG)
            for (const auto &cat : categories) {
                QString path = themeDir + "/scalable/" + cat + "/" + name + ".svg";
                if (QFile::exists(path))
                    return path;
            }

            // Try sized directories (prefer closest match)
            QStringList sizes = {
                QString::number(size) + "x" + QString::number(size),
                "48x48", "32x32", "24x24", "16x16", "64x64", "96x96", "256x256"
            };
            for (const auto &sz : sizes) {
                for (const auto &cat : categories) {
                    for (const auto &ext : extensions) {
                        QString path = themeDir + "/" + sz + "/" + cat + "/" + name + ext;
                        if (QFile::exists(path))
                            return path;
                    }
                }
            }
        }

        return {};
    }

    QStringList m_themeDirs;
};
