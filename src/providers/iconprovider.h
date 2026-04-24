#pragma once

#include <QQuickImageProvider>
#include <QSvgRenderer>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

class IconProvider : public QQuickImageProvider
{
public:
    explicit IconProvider(const QString &primaryTheme = "Adwaita")
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
        rebuildSearchDirs();
        setPrimaryTheme(primaryTheme);
    }

    void setPrimaryTheme(const QString &primaryTheme)
    {
        const QString themeName = primaryTheme.trimmed().isEmpty() ? QStringLiteral("Adwaita")
                                                                    : primaryTheme.trimmed();
        QMutexLocker locker(&m_cacheMutex);
        if (m_primaryTheme == themeName && !m_primaryDirs.isEmpty())
            return;

        m_primaryTheme = themeName;
        m_primaryDirs.clear();
        m_fallbackDirs.clear();
        m_cache.clear();

        for (const auto &dir : m_searchDirs) {
            const QString path = dir + "/" + themeName;
            if (QDir(path).exists() && !m_primaryDirs.contains(path))
                m_primaryDirs.append(path);
        }

        const QStringList fallbacks = {"breeze", "Papirus", "Adwaita", "hicolor"};
        for (const auto &theme : fallbacks) {
            if (theme == themeName)
                continue;

            for (const auto &dir : m_searchDirs) {
                const QString path = dir + "/" + theme;
                if (QDir(path).exists() && !m_fallbackDirs.contains(path))
                    m_fallbackDirs.append(path);
            }
        }
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        int sz = (requestedSize.width() > 0) ? requestedSize.width() : 48;
        QSize iconSize(sz, sz);

        // Parse optional color: "icon-name?color=#rrggbb"
        QString iconName = id;
        QColor tintColor;
        int qmark = id.indexOf('?');
        if (qmark >= 0) {
            iconName = id.left(qmark);
            QString params = id.mid(qmark + 1);
            for (const auto &param : params.split('&')) {
                if (param.startsWith("color="))
                    tintColor = QColor(param.mid(6));
            }
        }

        bool isSymbolic = iconName.endsWith("-symbolic");

        const QString tintKey = tintColor.isValid() ? tintColor.name(QColor::HexArgb) : QString();
        QStringList primaryDirs;
        QStringList fallbackDirs;
        QString cacheKey;
        {
            QMutexLocker locker(&m_cacheMutex);
            cacheKey = m_primaryTheme + QLatin1Char('\x1f')
                + iconName + QLatin1Char('\x1f')
                + QString::number(sz) + QLatin1Char('\x1f')
                + tintKey;
            const auto cached = m_cache.constFind(cacheKey);
            if (cached != m_cache.constEnd()) {
                if (size)
                    *size = cached->size();
                return *cached;
            }

            primaryDirs = m_primaryDirs;
            fallbackDirs = m_fallbackDirs;
        }

        // For symbolic (UI) icons: ONLY search primary theme
        // If not found → return empty so QML uses PathSvg fallback
        // For file/mime icons: search primary first, then fallbacks
        QString svgPath = findIconIn(iconName, sz, primaryDirs);

        if (svgPath.isEmpty()) {
            if (isSymbolic) {
                // Not in primary theme → return empty for PathSvg fallback
                QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
                empty.fill(Qt::transparent);
                if (size) *size = QSize(0, 0);
                remember(cacheKey, empty);
                return empty;
            }
            // File icon: try fallback themes
            svgPath = findIconIn(iconName, sz, fallbackDirs);
            if (svgPath.isEmpty())
                svgPath = findIconIn("text-x-generic", sz, primaryDirs);
            if (svgPath.isEmpty())
                svgPath = findIconIn("text-x-generic", sz, fallbackDirs);
            if (svgPath.isEmpty()) {
                if (size) *size = iconSize;
                QImage empty(iconSize, QImage::Format_ARGB32_Premultiplied);
                empty.fill(Qt::transparent);
                remember(cacheKey, empty);
                return empty;
            }
        }

        QImage img(iconSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        if (svgPath.endsWith(".svg") || svgPath.endsWith(".svgz")) {
            QSvgRenderer renderer(svgPath);
            if (renderer.isValid()) {
                QPainter painter(&img);
                renderer.render(&painter);
                painter.end();
            }
        } else {
            QImage loaded(svgPath);
            if (!loaded.isNull())
                img = loaded.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        // Tint: replace pixel colors with tintColor, preserving alpha
        if (tintColor.isValid()) {
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    int a = qAlpha(line[x]);
                    if (a > 0)
                        line[x] = qRgba(tintColor.red(), tintColor.green(), tintColor.blue(), a);
                }
            }
        }

        if (size)
            *size = iconSize;
        remember(cacheKey, img);
        return img;
    }

private:
    void remember(const QString &key, const QImage &image)
    {
        QMutexLocker locker(&m_cacheMutex);
        if (m_cache.size() > 512)
            m_cache.clear();
        m_cache.insert(key, image);
    }

    void rebuildSearchDirs()
    {
        m_searchDirs.clear();

        const QString home = QDir::homePath();
        m_searchDirs.append(home + "/.icons");
        m_searchDirs.append(home + "/.local/share/icons");
        m_searchDirs.append("/usr/share/icons");
        m_searchDirs.append("/usr/local/share/icons");

        const QString xdgDirs = qEnvironmentVariable("XDG_DATA_DIRS", "/usr/share:/usr/local/share");
        for (const auto &dir : xdgDirs.split(':')) {
            const QString iconDir = dir + "/icons";
            if (!m_searchDirs.contains(iconDir))
                m_searchDirs.append(iconDir);
        }
    }

    QString findIconIn(const QString &name, int size, const QStringList &themeDirs) const
    {
        static const QStringList categories = {
            "actions", "places", "mimetypes", "devices",
            "status", "apps", "categories", "emblems"
        };
        static const QStringList extensions = {".svg", ".png"};

        for (const auto &themeDir : themeDirs) {
            // Pattern 1: scalable/category/name.svg (hicolor, Adwaita)
            for (const auto &cat : categories) {
                QString path = themeDir + "/scalable/" + cat + "/" + name + ".svg";
                if (QFile::exists(path))
                    return path;
            }

            // Pattern 2: symbolic/category/name.svg (Adwaita symbolic)
            for (const auto &cat : categories) {
                QString path = themeDir + "/symbolic/" + cat + "/" + name + ".svg";
                if (QFile::exists(path))
                    return path;
            }

            // Pattern 3: category/size/name.ext (Breeze: actions/24/name.svg)
            QStringList numSizes = {
                QString::number(size), "24", "32", "48", "16", "22", "64", "96", "256"
            };
            for (const auto &cat : categories) {
                for (const auto &sz : numSizes) {
                    for (const auto &ext : extensions) {
                        QString path = themeDir + "/" + cat + "/" + sz + "/" + name + ext;
                        if (QFile::exists(path))
                            return path;
                    }
                }
            }

            // Pattern 4: sizexsize/category/name.ext (hicolor: 24x24/actions/name.svg)
            QStringList sqSizes = {
                QString::number(size) + "x" + QString::number(size),
                "48x48", "32x32", "24x24", "16x16", "64x64", "96x96", "256x256"
            };
            for (const auto &sz : sqSizes) {
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

    QString m_primaryTheme;
    QStringList m_searchDirs;
    QStringList m_primaryDirs;
    QStringList m_fallbackDirs;
    QHash<QString, QImage> m_cache;
    QMutex m_cacheMutex;
};
