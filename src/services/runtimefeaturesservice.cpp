#include "services/runtimefeaturesservice.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QStandardPaths>

RuntimeFeaturesService::RuntimeFeaturesService(QObject *parent)
    : QObject(parent)
{
}

bool RuntimeFeaturesService::ffmpegAvailable() const
{
    return hasExecutable(QStringLiteral("ffmpeg"));
}

bool RuntimeFeaturesService::batAvailable() const
{
    return hasExecutable(QStringLiteral("bat")) || hasExecutable(QStringLiteral("batcat"));
}

bool RuntimeFeaturesService::udisksctlAvailable() const
{
    // Mount/unmount goes over DBus directly (DeviceModel calls
    // org.freedesktop.UDisks2.Filesystem.Mount/Unmount), so what matters
    // is whether the UDisks2 service is reachable on the system bus, not
    // whether the udisksctl CLI is on PATH.
    auto *iface = QDBusConnection::systemBus().interface();
    if (!iface)
        return false;
    return iface->isServiceRegistered(QStringLiteral("org.freedesktop.UDisks2"));
}

bool RuntimeFeaturesService::wlClipboardAvailable() const
{
    return hasExecutable(QStringLiteral("wl-copy")) && hasExecutable(QStringLiteral("wl-paste"));
}

bool RuntimeFeaturesService::gitAvailable() const
{
    return hasExecutable(QStringLiteral("git"));
}

QString RuntimeFeaturesService::installHint(const QString &feature) const
{
    if (feature == QStringLiteral("videoPreview"))
        return QStringLiteral("Install ffmpeg to enable video poster previews.");
    if (feature == QStringLiteral("pdfPreview"))
        return QStringLiteral("Install poppler-qt6 and rebuild HyprFM to enable PDF previews.");
    if (feature == QStringLiteral("remoteAccess"))
        return QStringLiteral("Install gvfs to browse remote filesystems through Connect to Server.");
    if (feature == QStringLiteral("smbRemoteAccess"))
        return QStringLiteral("Install gvfs-smb to browse SMB/CIFS shares.");
    if (feature == QStringLiteral("deviceMount"))
        return QStringLiteral("Install udisks2 to mount and unmount devices from the sidebar.");
    if (feature == QStringLiteral("clipboardImage"))
        return QStringLiteral("Install wl-clipboard to paste images and copy paths through Wayland.");
    if (feature == QStringLiteral("textHighlight"))
        return QStringLiteral("Install bat for syntax-highlighted text previews.");
    return {};
}

bool RuntimeFeaturesService::hasExecutable(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}
