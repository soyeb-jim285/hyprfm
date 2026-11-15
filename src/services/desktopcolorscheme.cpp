#include "services/desktopcolorscheme.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>

namespace DesktopColorScheme {

bool themeNameIsDark(const QString &name)
{
    return name.contains(QLatin1String("dark"), Qt::CaseInsensitive);
}

std::optional<bool> darkFromSettingsFiles(const QString &configHome)
{
    // GTK: an explicit prefer-dark wins; otherwise the theme's name.
    for (const char *dir : {"gtk-4.0", "gtk-3.0"}) {
        const QString path = configHome + QLatin1Char('/') + QLatin1String(dir)
                             + QLatin1String("/settings.ini");
        if (!QFileInfo::exists(path))
            continue;
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("Settings"));
        const QString prefer = ini.value(QStringLiteral("gtk-application-prefer-dark-theme")).toString();
        if (prefer == QLatin1String("1") || prefer.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            return true;
        const QString theme = ini.value(QStringLiteral("gtk-theme-name")).toString();
        if (!theme.isEmpty())
            return themeNameIsDark(theme);
    }

    // KDE writes the scheme's name ("BreezeDark", "BreezeLight").
    const QString kdeglobals = configHome + QLatin1String("/kdeglobals");
    if (QFileInfo::exists(kdeglobals)) {
        QSettings kde(kdeglobals, QSettings::IniFormat);
        // QSettings reads an INI [General] section as the top level.
        const QString scheme = kde.value(QStringLiteral("ColorScheme")).toString();
        if (!scheme.isEmpty())
            return themeNameIsDark(scheme);
    }
    return std::nullopt;
}

// The portal answers Read with a variant wrapping a variant.
static QVariant readPortalSetting(QDBusInterface &portal, const QString &ns, const QString &key)
{
    const QDBusReply<QDBusVariant> reply = portal.call(QStringLiteral("Read"), ns, key);
    if (!reply.isValid())
        return {};
    QVariant value = reply.value().variant();
    if (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

bool prefersDark()
{
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Settings"),
                          QDBusConnection::sessionBus());
    const bool havePortal = QDBusConnection::sessionBus().isConnected() && portal.isValid();
    // A desktop that is slow to answer must not hold up the window.
    portal.setTimeout(200);

    // 0 no preference, 1 dark, 2 light. Desktop shells write this when the
    // user flips light/dark.
    if (havePortal) {
        bool ok = false;
        const uint scheme = readPortalSetting(portal, QStringLiteral("org.freedesktop.appearance"),
                                              QStringLiteral("color-scheme")).toUInt(&ok);
        if (ok && scheme != 0)
            return scheme == 1;
    }

    // Unknown under platform themes that do not forward it (qt6ct, Kvantum).
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return true;
    case Qt::ColorScheme::Light:
        return false;
    default:
        break;
    }

    // A dark GTK theme picked in nwg-look or similar sets no color-scheme.
    if (havePortal) {
        const QString gtkTheme = readPortalSetting(portal, QStringLiteral("org.gnome.desktop.interface"),
                                                   QStringLiteral("gtk-theme")).toString();
        if (!gtkTheme.isEmpty())
            return themeNameIsDark(gtkTheme);
    }

    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return darkFromSettingsFiles(configHome).value_or(false);
}

} // namespace DesktopColorScheme
