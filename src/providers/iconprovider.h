#pragma once

#include <QQuickImageProvider>
#include <algorithm>
#include <QSvgRenderer>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QCache>
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

    void clearCache()
    {
        QMutexLocker locker(&m_cacheMutex);
        m_cache.clear();
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

        // The theme's own Inherits= chain first (MacTahoe -> hicolor), so a
        // file icon it lacks comes from the theme it builds on; then the
        // common full themes, so a theme with few mimetype icons still gets
        // coloured ones instead of a blank.
        QStringList fallbacks;
        QStringList pending = {themeName};
        while (!pending.isEmpty()) {
            const QString theme = pending.takeFirst();
            for (const auto &dir : m_searchDirs) {
                // Only the Inherits= line: QSettings would parse the whole
                // file, and Papirus lists thousands of directories in it.
                QFile index(dir + "/" + theme + "/index.theme");
                if (!index.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString inherits;
                while (!index.atEnd()) {
                    const QByteArray line = index.readLine().trimmed();
                    if (line.startsWith("Inherits=")) {
                        inherits = QString::fromUtf8(line.mid(9));
                        break;
                    }
                    if (line.startsWith("[") && line != "[Icon Theme]")
                        break;   // Inherits lives in [Icon Theme], which comes first
                }
                for (QString parent : inherits.split(QLatin1Char(','))) {
                    parent = parent.trimmed();
                    if (!parent.isEmpty() && parent != themeName && !fallbacks.contains(parent)) {
                        fallbacks.append(parent);
                        pending.append(parent);
                    }
                }
                break;
            }
        }
        for (const QString theme : {"breeze", "Papirus", "Adwaita", "hicolor"}) {
            if (!fallbacks.contains(theme))
                fallbacks.append(theme);
        }
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

        // Parse optional params: "icon-name?theme=Name&color=#rrggbb".
        // The theme comes from the URL, not only from setPrimaryTheme(): on a
        // config reload QML re-requests icons before the configChanged slot
        // that switches the provider runs, so trusting the current theme
        // served the old theme's icons under the new URLs.
        QString iconName = id;
        QColor tintColor;
        QString requestedTheme;
        QString revision;
        int qmark = id.indexOf('?');
        if (qmark >= 0) {
            iconName = id.left(qmark);
            QString params = id.mid(qmark + 1);
            for (const auto &param : params.split('&')) {
                if (param.startsWith("color="))
                    tintColor = QColor(param.mid(6));
                else if (param.startsWith("theme="))
                    requestedTheme = param.mid(6);
                else if (param.startsWith("r="))
                    revision = param.mid(2);
            }
        }
        if (!requestedTheme.isEmpty())
            setPrimaryTheme(requestedTheme);

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
                + tintKey + QLatin1Char('\x1f')
                + revision;
            if (const QImage *cached = m_cache.object(cacheKey)) {
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

        // Prefer icons that actually ship with the primary theme (e.g.
        // MacTahoe's shellscript.svg) over inherited fallback icons.
        if (svgPath.isEmpty() && !isSymbolic) {
            QStringList aliases;
            if (iconName.startsWith(QStringLiteral("text-x-"))) {
                aliases << iconName.mid(7);
                aliases << QStringLiteral("application-x-") + iconName.mid(7);
            } else if (iconName.startsWith(QStringLiteral("application-x-"))) {
                aliases << iconName.mid(14);
                aliases << QStringLiteral("text-x-") + iconName.mid(14);
            } else if (iconName.startsWith(QStringLiteral("application-"))) {
                aliases << iconName.mid(12);
            } else if (iconName.startsWith(QStringLiteral("text-"))) {
                aliases << iconName.mid(5);
            }
            aliases << QStringLiteral("gnome-mime-") + iconName;
            for (const QString &alias : aliases) {
                if (alias.isEmpty() || alias == iconName)
                    continue;
                svgPath = findIconIn(alias, sz, primaryDirs);
                if (!svgPath.isEmpty())
                    break;
            }
        }

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

    // Path of a theme icon in the primary theme or its fallbacks; empty when
    // none has it (no generic-file substitute). Safe from worker threads.
    QString findIconPath(const QString &name, int size) const
    {
        QStringList primaryDirs, fallbackDirs;
        {
            QMutexLocker locker(&m_cacheMutex);
            primaryDirs = m_primaryDirs;
            fallbackDirs = m_fallbackDirs;
        }
        const QString path = findIconIn(name, size, primaryDirs);
        return path.isEmpty() ? findIconIn(name, size, fallbackDirs) : path;
    }

private:
    void remember(const QString &key, const QImage &image)
    {
        QMutexLocker locker(&m_cacheMutex);
        m_cache.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes()));
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

    // Size directories to try, best first: the exact size, then the closest
    // larger one (a 64px Breeze folder downscaled looks right), then the
    // largest smaller one. Trying "24" before "64" upscaled Breeze's
    // simplified small icons into blurry outlines.
    static QStringList sizeCandidates(int size)
    {
        QList<int> sizes = {16, 22, 24, 32, 48, 64, 96, 128, 256, 512};
        if (!sizes.contains(size))
            sizes.append(size);
        std::sort(sizes.begin(), sizes.end(), [size](int a, int b) {
            const bool aUp = a >= size, bUp = b >= size;
            if (aUp != bUp)
                return aUp;
            return aUp ? a < b : a > b;
        });
        QStringList out;
        for (int s : sizes)
            out << QString::number(s);
        return out;
    }

    QString findIconIn(const QString &name, int size, const QStringList &themeDirs) const
    {
        static const QStringList categories = {
            "actions", "places", "mimetypes", "mimes", "devices",
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

            // Pattern 1b: category/scalable/name.svg (Breeze, MacTahoe, ...)
            for (const auto &cat : categories) {
                for (const auto &ext : extensions) {
                    QString path = themeDir + "/" + cat + "/scalable/" + name + ext;
                    if (QFile::exists(path))
                        return path;
                }
            }

            // Pattern 2: symbolic/category/name.svg (Adwaita symbolic)
            for (const auto &cat : categories) {
                QString path = themeDir + "/symbolic/" + cat + "/" + name + ".svg";
                if (QFile::exists(path))
                    return path;
            }

            // Pattern 2b: category/symbolic/name.svg (Breeze, MacTahoe, ...)
            for (const auto &cat : categories) {
                QString path = themeDir + "/" + cat + "/symbolic/" + name + ".svg";
                if (QFile::exists(path))
                    return path;
            }

            // Pattern 3: category/size/name.ext (Breeze: actions/24/name.svg)
            const QStringList numSizes = sizeCandidates(size);
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
            QStringList sqSizes;
            for (const auto &sz : numSizes)
                sqSizes << sz + "x" + sz;
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
    // Bounded by bytes, not entries: the grid asks for icons at the size it
    // draws them, up to 640px (1.6 MB each), and every size the window
    // passes through while it settles. 512 entries at that size was
    // hundreds of MB that were never freed. ponytail: 24 MB, about fifteen
    // 640px icons; QML's own pixmap cache keeps what is on screen anyway.
    QCache<QString, QImage> m_cache{24 * 1024 * 1024};
    mutable QMutex m_cacheMutex;
};
