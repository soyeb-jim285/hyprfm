#pragma once

#include <QString>
#include <optional>

// Whether the desktop wants dark or light, for picking the first theme while
// config.toml has no "theme". Asked once at startup.
namespace DesktopColorScheme {

// Which way the GTK and KDE settings files lean: true dark, false light,
// nullopt when they say nothing. configHome is $XDG_CONFIG_HOME.
// Reads gtk-3.0/settings.ini, gtk-4.0/settings.ini and kdeglobals.
std::optional<bool> darkFromSettingsFiles(const QString &configHome);

// Same test for a GTK or KDE theme name ("Adwaita-dark", "BreezeDark").
bool themeNameIsDark(const QString &name);

// In order: the XDG portal's color-scheme, Qt's colorScheme(), the GTK theme
// the portal reports, the settings files above. Nothing found means light:
// the portal spec reads "no preference" as the application default, and the
// AppImage catalog flagged a dark window on a desktop that set nothing.
bool prefersDark();

} // namespace DesktopColorScheme
