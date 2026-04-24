#include "models/devicemodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QUrl>
#undef signals
#include <gio/gio.h>
#define signals Q_SIGNALS
#include <algorithm>
#include <memory>

static const QStringList kVirtualTypes = {
    "tmpfs", "devtmpfs", "proc", "sysfs",
    "cgroup", "cgroup2", "overlay", "squashfs",
    "devpts", "hugetlbfs", "mqueue", "pstore",
    "securityfs", "fusectl", "debugfs", "tracefs",
    "bpf", "autofs", "ramfs", "efivarfs",
};

DeviceModel::DeviceModel(QObject *parent, bool deferInitialRefresh)
    : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(600);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DeviceModel::refresh);

    setupGioMonitor();
    setupUDisks2();
    if (deferInitialRefresh)
        scheduleRefresh();
    else
        refresh();
}

DeviceModel::~DeviceModel()
{
    clearDevices();
    if (m_volumeMonitor) {
        g_signal_handlers_disconnect_by_data(m_volumeMonitor, this);
        g_object_unref(m_volumeMonitor);
        m_volumeMonitor = nullptr;
    }
}

void DeviceModel::clearDevices()
{
    for (const DeviceEntry &device : std::as_const(m_devices)) {
        if (device.gioVolume)
            g_object_unref(device.gioVolume);
        if (device.gioMount)
            g_object_unref(device.gioMount);
    }
    m_devices.clear();
}

bool DeviceModel::isVirtual(const QString &fsType)
{
    return kVirtualTypes.contains(fsType.toLower());
}

void DeviceModel::setupUDisks2()
{
    QDBusConnection sys = QDBusConnection::systemBus();
    if (!sys.isConnected()) {
        qWarning() << "DeviceModel: cannot connect to system D-Bus; auto-refresh disabled";
        return;
    }

    sys.connect(
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded",
        this, SLOT(scheduleRefresh())
    );

    sys.connect(
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved",
        this, SLOT(scheduleRefresh())
    );
}

namespace {

void onGioMonitorChanged(GVolumeMonitor *, gpointer, gpointer userData)
{
    auto *model = static_cast<DeviceModel *>(userData);
    if (!model)
        return;
    QMetaObject::invokeMethod(model, &DeviceModel::scheduleRefresh, Qt::QueuedConnection);
}

} // namespace

void DeviceModel::setupGioMonitor()
{
    m_volumeMonitor = g_volume_monitor_get();
    if (!m_volumeMonitor)
        return;

    g_signal_connect(m_volumeMonitor, "drive-connected",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "drive-disconnected",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "drive-changed",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "volume-added",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "volume-removed",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "volume-changed",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "mount-added",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "mount-removed",
                     G_CALLBACK(onGioMonitorChanged), this);
    g_signal_connect(m_volumeMonitor, "mount-changed",
                     G_CALLBACK(onGioMonitorChanged), this);
}

namespace {

// Strip the trailing NUL that UDisks2 includes in `ay` byte arrays
// (Device, Symlinks, MountPoints elements).
QString cstrFromBytes(const QByteArray &bytes)
{
    QByteArray copy = bytes;
    if (copy.endsWith('\0'))
        copy.chop(1);
    return QString::fromUtf8(copy);
}

QStringList parseMountPoints(const QVariant &value)
{
    QStringList out;
    // MountPoints is `aay` — a list of NUL-terminated byte arrays.
    const QDBusArgument arg = value.value<QDBusArgument>();
    if (arg.currentType() == QDBusArgument::ArrayType) {
        arg.beginArray();
        while (!arg.atEnd()) {
            QByteArray mp;
            arg >> mp;
            out << cstrFromBytes(mp);
        }
        arg.endArray();
    }
    return out;
}

QString qStringFromGChar(gchar *value)
{
    const QString out = value ? QString::fromUtf8(value) : QString();
    g_free(value);
    return out;
}

QString uriFromGFile(GFile *file)
{
    if (!file)
        return {};
    return qStringFromGChar(g_file_get_uri(file));
}

QString schemeFromUri(const QString &uri)
{
    return QUrl(uri).scheme().toLower();
}

QString authorityFromUri(const QString &uri)
{
    const int schemeSep = uri.indexOf(QStringLiteral("://"));
    if (schemeSep < 0)
        return {};

    const int authorityStart = schemeSep + 3;
    int authorityEnd = uri.size();
    const int pathStart = uri.indexOf(QLatin1Char('/'), authorityStart);
    const int queryStart = uri.indexOf(QLatin1Char('?'), authorityStart);
    const int fragmentStart = uri.indexOf(QLatin1Char('#'), authorityStart);
    for (const int marker : {pathStart, queryStart, fragmentStart}) {
        if (marker >= 0)
            authorityEnd = std::min(authorityEnd, marker);
    }

    return uri.mid(authorityStart, authorityEnd - authorityStart);
}

QString hostFromAuthority(const QString &authority)
{
    const int atIndex = authority.lastIndexOf(QLatin1Char('@'));
    const QString hostAndPort = atIndex >= 0 ? authority.mid(atIndex + 1) : authority;
    const int colonIndex = hostAndPort.indexOf(QLatin1Char(':'));
    return colonIndex >= 0 ? hostAndPort.left(colonIndex) : hostAndPort;
}

QString buildAfcUri(const QString &udid, int port = -1)
{
    if (udid.isEmpty())
        return {};
    return port > 0
        ? QStringLiteral("afc://%1:%2/").arg(udid, QString::number(port))
        : QStringLiteral("afc://%1/").arg(udid);
}

QString hyphenateUdid(const QString &condensed)
{
    if (condensed.size() <= 8)
        return condensed;
    return condensed.left(8) + QLatin1Char('-') + condensed.mid(8);
}

bool afcBackendAvailable()
{
    return QFileInfo::exists(QStringLiteral("/usr/share/gvfs/mounts/afc.mount"))
        || QFileInfo::exists(QStringLiteral("/usr/lib/gvfs/gvfsd-afc"))
        || QFileInfo::exists(QStringLiteral("/usr/libexec/gvfsd-afc"))
        || QFileInfo::exists(QStringLiteral("/run/host/usr/share/gvfs/mounts/afc.mount"))
        || QFileInfo::exists(QStringLiteral("/run/host/usr/lib/gvfs/gvfsd-afc"))
        || QFileInfo::exists(QStringLiteral("/run/host/usr/libexec/gvfsd-afc"));
}

QString mobileUdidFromUri(const QString &uri)
{
    const QString scheme = schemeFromUri(uri);
    if (scheme == QLatin1String("afc"))
        return hostFromAuthority(authorityFromUri(uri));

    if (scheme != QLatin1String("gphoto2"))
        return {};

    static const QRegularExpression suffixRe(QStringLiteral("([0-9A-Fa-f]{12,})$"));
    const auto match = suffixRe.match(authorityFromUri(uri));
    if (!match.hasMatch())
        return {};
    return hyphenateUdid(match.captured(1).toUpper());
}

bool isMobileDeviceScheme(const QString &scheme)
{
    static const QSet<QString> schemes = {
        QStringLiteral("afc"),
        QStringLiteral("gphoto2"),
    };
    return schemes.contains(scheme);
}

bool isRemovableDrive(GDrive *drive)
{
    return drive && (g_drive_is_removable(drive) || g_drive_is_media_removable(drive));
}

QString fallbackGioDeviceKey(const QString &prefix, const QString &name,
                             const QString &uuid, quintptr pointerValue)
{
    QString key = prefix;
    if (!uuid.isEmpty())
        key += QStringLiteral(":") + uuid;
    else if (!name.isEmpty())
        key += QStringLiteral(":") + name;
    else
        key += QStringLiteral(":0x") + QString::number(pointerValue, 16);
    return key;
}

QString friendlyGioActionError(const QString &action, const QString &label,
                               const QString &err)
{
    const QString lower = err.toLower();
    if (action == QLatin1String("mount")
        && (lower.contains(QStringLiteral("pair"))
            || lower.contains(QStringLiteral("trust"))
            || lower.contains(QStringLiteral("lockdown")))) {
        return DeviceModel::tr(
            "Could not mount %1: unlock the iPhone or iPad, tap Trust on the device, and try again.")
            .arg(label);
    }
    return DeviceModel::tr("Could not %1 %2: %3").arg(action, label, err);
}

QString runGioMountCommand(const QStringList &arguments, QString *error)
{
    QProcess proc;
    proc.start(QStringLiteral("gio"), arguments);
    if (!proc.waitForFinished(20000)) {
        if (error)
            *error = DeviceModel::tr("gio mount timed out");
        return {};
    }

    if (proc.exitCode() != 0) {
        if (error) {
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            if (error->isEmpty())
                *error = DeviceModel::tr("gio mount failed");
        }
        return {};
    }

    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

struct GioOperationContext {
    QPointer<DeviceModel> model;
    QString label;
};

struct DriveInfo {
    bool removable = false;
    QString connectionBus;
};

struct BlockInfo {
    QString device;          // /dev/sdXY
    QString idLabel;
    QString idType;          // filesystem type, e.g. "ext4"
    bool hintIgnore = false;
    QString drivePath;       // object path of parent drive
    qint64 size = 0;
    bool hasFilesystem = false;
    QStringList mountPoints;
    QString partitionType;   // GPT GUID / MBR type, lowercased
};

void onGioVolumeMounted(GObject *sourceObject, GAsyncResult *result, gpointer userData)
{
    std::unique_ptr<GioOperationContext> context(
        static_cast<GioOperationContext *>(userData));
    if (!context || !context->model)
        return;

    auto *model = context->model.data();
    GError *error = nullptr;
    if (!g_volume_mount_finish(G_VOLUME(sourceObject), result, &error)) {
        const QString err = error ? QString::fromUtf8(error->message).trimmed()
                                  : DeviceModel::tr("Unknown error");
        if (error)
            g_error_free(error);
        emit model->mountError(friendlyGioActionError(QStringLiteral("mount"),
                                                      context->label, err));
        return;
    }

    QString mountPoint;
    GMount *mount = g_volume_get_mount(G_VOLUME(sourceObject));
    if (mount) {
        GFile *location = g_mount_get_default_location(mount);
        if (!location)
            location = g_mount_get_root(mount);
        mountPoint = uriFromGFile(location);
        if (location)
            g_object_unref(location);
        g_object_unref(mount);
    }

    if (mountPoint.isEmpty()) {
        GFile *activationRoot = g_volume_get_activation_root(G_VOLUME(sourceObject));
        mountPoint = uriFromGFile(activationRoot);
        if (activationRoot)
            g_object_unref(activationRoot);
    }

    model->refresh();
    emit model->deviceMounted(mountPoint);
}

void onGioMountUnmounted(GObject *sourceObject, GAsyncResult *result, gpointer userData)
{
    std::unique_ptr<GioOperationContext> context(
        static_cast<GioOperationContext *>(userData));
    if (!context || !context->model)
        return;

    auto *model = context->model.data();
    GError *error = nullptr;
    if (!g_mount_unmount_with_operation_finish(G_MOUNT(sourceObject), result, &error)) {
        const QString err = error ? QString::fromUtf8(error->message).trimmed()
                                  : DeviceModel::tr("Unknown error");
        if (error)
            g_error_free(error);
        emit model->mountError(friendlyGioActionError(QStringLiteral("unmount"),
                                                      context->label, err));
        return;
    }

    model->refresh();
}

} // namespace

void DeviceModel::scheduleRefresh()
{
    m_refreshTimer.start();
}

void DeviceModel::refresh()
{
    m_refreshTimer.stop();
    beginResetModel();
    clearDevices();

    // Ask UDisks2 for everything it knows about. Returns a{oa{sa{sv}}}:
    //   { object_path → { interface_name → { property_name → value } } }
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        QStringLiteral("/org/freedesktop/UDisks2"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));

    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
    QHash<QString, DriveInfo> drives;
    QHash<QString, BlockInfo> blocks;

    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "DeviceModel: UDisks2 GetManagedObjects failed:"
                   << reply.errorMessage();
    } else {
        const QDBusArgument outer = reply.arguments().constFirst().value<QDBusArgument>();
        outer.beginMap();
        while (!outer.atEnd()) {
            outer.beginMapEntry();
            QDBusObjectPath objectPath;
            outer >> objectPath;

            // Inner map: { interface_name → { property_name → value } }
            outer.beginMap();
            while (!outer.atEnd()) {
                outer.beginMapEntry();
                QString interfaceName;
                QVariantMap properties;
                outer >> interfaceName >> properties;
                outer.endMapEntry();

                const QString path = objectPath.path();

                if (interfaceName == QLatin1String("org.freedesktop.UDisks2.Drive")) {
                    DriveInfo &d = drives[path];
                    d.removable = properties.value(QStringLiteral("Removable")).toBool();
                    d.connectionBus = properties.value(QStringLiteral("ConnectionBus")).toString();
                } else if (interfaceName == QLatin1String("org.freedesktop.UDisks2.Block")) {
                    BlockInfo &b = blocks[path];
                    b.device = cstrFromBytes(properties.value(QStringLiteral("Device")).toByteArray());
                    b.idLabel = properties.value(QStringLiteral("IdLabel")).toString();
                    b.idType = properties.value(QStringLiteral("IdType")).toString();
                    b.hintIgnore = properties.value(QStringLiteral("HintIgnore")).toBool();
                    b.size = properties.value(QStringLiteral("Size")).toLongLong();
                    b.drivePath = properties.value(QStringLiteral("Drive"))
                                      .value<QDBusObjectPath>().path();
                } else if (interfaceName == QLatin1String("org.freedesktop.UDisks2.Filesystem")) {
                    BlockInfo &b = blocks[path];
                    b.hasFilesystem = true;
                    b.mountPoints = parseMountPoints(
                        properties.value(QStringLiteral("MountPoints")));
                } else if (interfaceName == QLatin1String("org.freedesktop.UDisks2.Partition")) {
                    BlockInfo &b = blocks[path];
                    b.partitionType = properties.value(QStringLiteral("Type"))
                                          .toString().toLower();
                }
            }
            outer.endMap();
            outer.endMapEntry();
        }
        outer.endMap();
    }

    static const QString kEfiGuid = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a098cb80e7");

    for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
        const BlockInfo &b = it.value();

        if (b.hintIgnore)
            continue;
        if (b.idType.isEmpty() || b.idType == QLatin1String("swap"))
            continue;
        if (isVirtual(b.idType))
            continue;
        if (b.device.startsWith(QLatin1String("/dev/loop")) ||
            b.device.startsWith(QLatin1String("/dev/zram")))
            continue;
        if (b.partitionType == kEfiGuid)
            continue;

        const bool mounted = !b.mountPoints.isEmpty();
        const QString mountPoint = mounted ? b.mountPoints.constFirst() : QString();

        if (mounted && (mountPoint.startsWith(QLatin1String("/boot")) ||
                        mountPoint.startsWith(QLatin1String("/snap")) ||
                        mountPoint.startsWith(QLatin1String("/nix")) ||
                        mountPoint.startsWith(QLatin1String("/efi"))))
            continue;

        const DriveInfo drive = drives.value(b.drivePath);
        const bool removable = drive.removable
            || drive.connectionBus == QLatin1String("usb")
            || drive.connectionBus == QLatin1String("sdio");

        QString name;
        if (!b.idLabel.isEmpty())
            name = b.idLabel;
        else if (mounted && mountPoint == QLatin1String("/"))
            name = QStringLiteral("/");
        else if (mounted)
            name = mountPoint.section(QLatin1Char('/'), -1);
        else
            name = b.device.section(QLatin1Char('/'), -1);

        qint64 total = 0;
        qint64 free = 0;
        int usage = 0;
        if (mounted) {
            // Inside a Flatpak the sandbox's `/` is a tmpfs runtime
            // overlay, not the host root. With --filesystem=host the host
            // file system is exposed under /run/host as individual bind
            // mounts (/run/host/usr, /run/host/etc, /run/host/var, etc.).
            // Note: /run/host itself is the tmpfs, so we cannot just stat
            // it — we have to stat one of the sub-mounts that lives on
            // the host root partition.
            //
            // Strategy: try the mount point directly first (works for
            // /home, /tmp, /opt, /media, /mnt, /run/media which are bind
            // mounted at the same path). If that fails or gives bogus
            // numbers and we're in Flatpak, fall back to a host-side
            // probe. For "/" specifically, /run/host/usr is the safest
            // bet — every distro has /usr on the root partition.
            const bool inFlatpak = QFile::exists(QStringLiteral("/.flatpak-info"));
            QStorageInfo storage(mountPoint);
            if (inFlatpak && (!storage.isValid() || storage.bytesTotal() == 0
                              || storage.device() == QByteArrayLiteral("tmpfs"))) {
                QStringList probes;
                if (mountPoint == QLatin1String("/")) {
                    probes << QStringLiteral("/run/host/usr")
                           << QStringLiteral("/run/host/etc");
                } else {
                    probes << QStringLiteral("/run/host") + mountPoint;
                }
                for (const QString &probe : std::as_const(probes)) {
                    if (!QFileInfo(probe).exists())
                        continue;
                    QStorageInfo s(probe);
                    if (s.isValid() && s.bytesTotal() > 0
                        && s.device() != QByteArrayLiteral("tmpfs")) {
                        storage = s;
                        break;
                    }
                }
            }
            if (storage.isValid()) {
                total = storage.bytesTotal();
                free = storage.bytesAvailable();
                if (total > 0)
                    usage = static_cast<int>((total - free) * 100 / total);
            }
        } else {
            total = b.size;
        }

        m_devices.append({name, b.device, mountPoint, b.idType.toLower(),
                          total, free, usage, removable, mounted,
                          QString(), DeviceBackend::UDisks2, nullptr, nullptr});
    }

    GVolumeMonitor *volumeMonitor = m_volumeMonitor ? m_volumeMonitor : g_volume_monitor_get();
    if (volumeMonitor) {
        QList<DeviceEntry> gioDevices;
        QSet<QString> seenMobileDevices;
        QSet<QString> afcRootUris;
        QHash<QString, QString> preferredNamesByUdid;
        const bool canUseAfc = afcBackendAvailable();

        GList *mounts = g_volume_monitor_get_mounts(volumeMonitor);
        for (GList *it = mounts; it; it = it->next) {
            GMount *mount = G_MOUNT(it->data);
            if (!mount || g_mount_is_shadowed(mount))
                continue;

            GFile *location = g_mount_get_default_location(mount);
            if (!location)
                location = g_mount_get_root(mount);

            const QString uri = uriFromGFile(location);
            const QString scheme = schemeFromUri(uri);
            if (!isMobileDeviceScheme(scheme)) {
                if (location)
                    g_object_unref(location);
                continue;
            }

            QString name = qStringFromGChar(g_mount_get_name(mount));
            const QString udid = mobileUdidFromUri(uri);
            if (!udid.isEmpty() && !name.isEmpty())
                preferredNamesByUdid.insert(udid, name);

            if (scheme == QLatin1String("afc") && !udid.isEmpty()
                && !authorityFromUri(uri).contains(QLatin1String(":3"))) {
                afcRootUris.insert(buildAfcUri(udid));
            }

            const QString key = !uri.isEmpty()
                ? uri
                : fallbackGioDeviceKey(QStringLiteral("gio-mount"), name,
                                       qStringFromGChar(g_mount_get_uuid(mount)),
                                       reinterpret_cast<quintptr>(mount));
            if (seenMobileDevices.contains(key)) {
                if (location)
                    g_object_unref(location);
                continue;
            }
            seenMobileDevices.insert(key);

            qint64 total = 0;
            qint64 free = 0;
            int usage = 0;

            GDrive *drive = g_mount_get_drive(mount);
            const bool removable = isRemovableDrive(drive) || uri.startsWith(QLatin1String("afc://"));
            if (drive)
                g_object_unref(drive);

            if (location)
                g_object_unref(location);

            if (name.isEmpty())
                name = QStringLiteral("Mobile Device");

            gioDevices.append({name, key, uri, scheme,
                               total, free, usage, removable, true,
                               QString(), DeviceBackend::Gio, nullptr,
                               G_MOUNT(g_object_ref(mount))});
        }
        g_list_free_full(mounts, g_object_unref);

        GList *volumes = g_volume_monitor_get_volumes(volumeMonitor);
        for (GList *it = volumes; it; it = it->next) {
            GVolume *volume = G_VOLUME(it->data);
            if (!volume)
                continue;

            GMount *mounted = g_volume_get_mount(volume);
            if (mounted) {
                g_object_unref(mounted);
                continue;
            }

            GFile *activationRoot = g_volume_get_activation_root(volume);
            const QString uri = uriFromGFile(activationRoot);
            const QString scheme = schemeFromUri(uri);
            const QString udid = mobileUdidFromUri(uri);
            const QString volumeClass = qStringFromGChar(
                g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_CLASS));
            const QString unixDevice = qStringFromGChar(
                g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE));

            const bool looksLikeRemoteMobileDevice = isMobileDeviceScheme(scheme)
                || (unixDevice.isEmpty() && volumeClass == QLatin1String("device"));
            if (!looksLikeRemoteMobileDevice) {
                if (activationRoot)
                    g_object_unref(activationRoot);
                continue;
            }

            QString name = qStringFromGChar(g_volume_get_name(volume));
            if (!udid.isEmpty() && !name.isEmpty())
                preferredNamesByUdid.insert(udid, name);

            const QString key = !uri.isEmpty()
                ? uri
                : fallbackGioDeviceKey(QStringLiteral("gio-volume"), name,
                                       qStringFromGChar(g_volume_get_uuid(volume)),
                                       reinterpret_cast<quintptr>(volume));
            if (seenMobileDevices.contains(key)) {
                if (activationRoot)
                    g_object_unref(activationRoot);
                continue;
            }
            seenMobileDevices.insert(key);

            if (activationRoot)
                g_object_unref(activationRoot);

            if (name.isEmpty())
                name = QStringLiteral("Mobile Device");

            gioDevices.append({name, key, QString(), scheme,
                               0, 0, 0, true, false,
                               QString(), DeviceBackend::Gio,
                               G_VOLUME(g_object_ref(volume)), nullptr});
        }
        g_list_free_full(volumes, g_object_unref);

        for (auto it = preferredNamesByUdid.constBegin(); canUseAfc && it != preferredNamesByUdid.constEnd(); ++it) {
            const QString udid = it.key();
            const QString rootUri = buildAfcUri(udid);
            if (rootUri.isEmpty() || seenMobileDevices.contains(rootUri))
                continue;

            seenMobileDevices.insert(rootUri);
            afcRootUris.insert(rootUri);

            QString name = it.value().trimmed();
            if (name.isEmpty())
                name = QStringLiteral("iPhone");

            gioDevices.append({name, rootUri, QString(), QStringLiteral("afc"),
                               0, 0, 0, true, false,
                               buildAfcUri(udid, 3), DeviceBackend::Gio, nullptr, nullptr});
        }

        for (const DeviceEntry &device : std::as_const(gioDevices)) {
            const QString mobileUri = device.mountPoint.isEmpty() ? device.devicePath : device.mountPoint;
            const QString udid = mobileUdidFromUri(mobileUri);
            if (schemeFromUri(mobileUri) == QLatin1String("afc")
                && authorityFromUri(mobileUri).contains(QLatin1String(":3"))
                && !udid.isEmpty() && afcRootUris.contains(buildAfcUri(udid))) {
                continue;
            }
            if (schemeFromUri(mobileUri) == QLatin1String("gphoto2")
                && canUseAfc && !udid.isEmpty() && afcRootUris.contains(buildAfcUri(udid))) {
                continue;
            }
            m_devices.append(device);
        }

        if (volumeMonitor != m_volumeMonitor)
            g_object_unref(volumeMonitor);
    }

    std::sort(m_devices.begin(), m_devices.end(),
              [](const DeviceEntry &a, const DeviceEntry &b) {
                  if (a.removable != b.removable)
                      return a.removable;
                  return a.devicePath < b.devicePath;
              });

    endResetModel();
}

// Map a /dev/<basename> path to its UDisks2 object path. This works for
// regular partitions (sdXY, nvmeXnYpZ, mmcblkXpY). Device-mapper / LUKS
// names use a different escaping scheme that we don't try to handle here;
// for those cases the call simply errors out and we log it.
static QString udisksObjectPathFor(const QString &devicePath)
{
    return QStringLiteral("/org/freedesktop/UDisks2/block_devices/")
        + QFileInfo(devicePath).fileName();
}

// UDisks2 returns "Not authorized to perform operation" when polkit refuses
// the call — in practice this is almost always because no polkit
// authentication agent is running in the user's session (polkitd itself is
// running, but it has no UI to prompt for the password). Fixed / internal
// partitions require admin auth, so mount/unmount on those fails silently.
static bool isPolkitAuthError(const QString &err)
{
    return err.contains(QStringLiteral("Not authorized"), Qt::CaseInsensitive);
}

static QString polkitAgentHint()
{
    return DeviceModel::tr(
        "No polkit authentication agent is running. Start one in your session "
        "(e.g. hyprpolkitagent, polkit-gnome, or polkit-kde-authentication-agent-1) "
        "and try again.");
}

void DeviceModel::unmount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const DeviceEntry &dev = m_devices.at(index);
    if (!dev.mounted)
        return;

    const QString label = dev.deviceName;

    if (dev.backend == DeviceBackend::Gio) {
        if (!dev.gioMount) {
            refresh();
            return;
        }

        auto *context = new GioOperationContext{QPointer<DeviceModel>(this), label};
        g_mount_unmount_with_operation(dev.gioMount,
                                       G_MOUNT_UNMOUNT_NONE,
                                       nullptr,
                                       nullptr,
                                       onGioMountUnmounted,
                                       context);
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        udisksObjectPathFor(dev.devicePath),
        QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
        QStringLiteral("Unmount"));
    msg << QVariant::fromValue(QVariantMap{});

    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, label]() {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            const QString err = reply.error().message();
            qWarning() << "UDisks2 Unmount failed:" << err;
            QString friendly = isPolkitAuthError(err)
                ? tr("Could not unmount %1: %2").arg(label, polkitAgentHint())
                : tr("Could not unmount %1: %2").arg(label, err);
            emit mountError(friendly);
        } else {
            refresh();
        }
        watcher->deleteLater();
    });
}

void DeviceModel::mount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const DeviceEntry entry = m_devices.at(index);
    const QString fsType = entry.fsType;
    const QString label = entry.deviceName;

    if (entry.backend == DeviceBackend::Gio) {
        const QString mountUri = entry.mountPoint.isEmpty() ? entry.devicePath : entry.mountPoint;

        if (!entry.gioVolume) {
            if (QUrl(mountUri).scheme().isEmpty()) {
                refresh();
                return;
            }

            auto *primary = new QProcess(this);
            connect(primary,
                    qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                    this,
                    [this, primary, mountUri, alternateUri = entry.alternateMountPoint, label]
                    (int exitCode, QProcess::ExitStatus) {
                const QString err = QString::fromUtf8(primary->readAllStandardError()).trimmed();
                primary->deleteLater();

                if (exitCode != 0) {
                    emit mountError(friendlyGioActionError(
                        QStringLiteral("mount"), label,
                        err.isEmpty() ? tr("gio mount failed") : err));
                    return;
                }

                if (!alternateUri.isEmpty() && alternateUri != mountUri) {
                    auto *secondary = new QProcess(this);
                    connect(secondary,
                            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                            this, [this, secondary, mountUri](int, QProcess::ExitStatus) {
                        secondary->deleteLater();
                        refresh();
                        emit deviceMounted(mountUri);
                    });
                    connect(secondary, &QProcess::errorOccurred,
                            this, [this, secondary, mountUri](QProcess::ProcessError) {
                        secondary->deleteLater();
                        refresh();
                        emit deviceMounted(mountUri);
                    });
                    secondary->start(QStringLiteral("gio"),
                                     {QStringLiteral("mount"), alternateUri});
                    return;
                }

                refresh();
                emit deviceMounted(mountUri);
            });
            connect(primary, &QProcess::errorOccurred, this,
                    [this, primary, label](QProcess::ProcessError) {
                const QString err = primary->errorString();
                primary->deleteLater();
                emit mountError(friendlyGioActionError(
                    QStringLiteral("mount"), label,
                    err.isEmpty() ? tr("gio mount failed") : err));
            });
            primary->start(QStringLiteral("gio"),
                           {QStringLiteral("mount"), mountUri});
            return;
        }

        auto *context = new GioOperationContext{QPointer<DeviceModel>(this), label};
        g_volume_mount(entry.gioVolume,
                       G_MOUNT_MOUNT_NONE,
                       nullptr,
                       nullptr,
                       onGioVolumeMounted,
                       context);
        return;
    }

    const QString devicePath = entry.devicePath;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        udisksObjectPathFor(devicePath),
        QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
        QStringLiteral("Mount"));
    msg << QVariant::fromValue(QVariantMap{});

    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, fsType, label]() {
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            const QString err = reply.error().message();
            qWarning() << "UDisks2 Mount failed:" << err;

            QString friendly;
            const bool helperMissing = err.contains(
                QStringLiteral("missing codepage or helper"), Qt::CaseInsensitive)
                || err.contains(QStringLiteral("wrong fs type"), Qt::CaseInsensitive);
            const bool dirtyNtfs = err.contains(
                QStringLiteral("unsafe mount"), Qt::CaseInsensitive)
                || err.contains(QStringLiteral("hibernated"), Qt::CaseInsensitive)
                || err.contains(QStringLiteral("fast-restart"), Qt::CaseInsensitive);
            const bool isNtfs = fsType == QLatin1String("ntfs")
                || fsType == QLatin1String("ntfs3");

            if (isPolkitAuthError(err)) {
                friendly = tr("Could not mount %1: %2").arg(label, polkitAgentHint());
            } else if (isNtfs && dirtyNtfs) {
                friendly = tr("Could not mount %1: the NTFS volume is in an unsafe "
                              "state (Windows Fast Startup or hibernation). Boot into "
                              "Windows and fully shut down, or disable Fast Startup.")
                               .arg(label);
            } else if (isNtfs && helperMissing) {
                friendly = tr("Could not mount %1: the NTFS mount helper is missing. "
                              "Install it on the host with 'sudo apt install ntfs-3g' "
                              "(Debian/Ubuntu) or 'sudo pacman -S ntfs-3g' (Arch).")
                               .arg(label);
            } else if (helperMissing) {
                friendly = tr("Could not mount %1: the filesystem type (%2) is not "
                              "supported by the kernel or the mount helper is missing.")
                               .arg(label, fsType.isEmpty() ? tr("unknown") : fsType);
            } else {
                friendly = tr("Could not mount %1: %2").arg(label, err);
            }
            emit mountError(friendly);
        } else {
            const QString mountPath = reply.value();
            refresh();
            emit deviceMounted(mountPath);
        }
        watcher->deleteLater();
    });
}

int DeviceModel::rowCount(const QModelIndex &) const
{
    return m_devices.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};

    const auto &d = m_devices.at(index.row());
    switch (role) {
    case DeviceNameRole:   return d.deviceName;
    case DevicePathRole:   return d.devicePath;
    case MountPointRole:   return d.mountPoint;
    case TotalSizeRole:    return d.totalSize;
    case FreeSpaceRole:    return d.freeSpace;
    case UsagePercentRole: return d.usagePercent;
    case RemovableRole:    return d.removable;
    case MountedRole:      return d.mounted;
    case BackendRole:      return d.backend == DeviceBackend::Gio
                                ? QStringLiteral("gio")
                                : QStringLiteral("udisks2");
    }
    return {};
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        {DeviceNameRole,   "deviceName"},
        {DevicePathRole,   "devicePath"},
        {MountPointRole,   "mountPoint"},
        {TotalSizeRole,    "totalSize"},
        {FreeSpaceRole,    "freeSpace"},
        {UsagePercentRole, "usagePercent"},
        {RemovableRole,    "removable"},
        {MountedRole,      "mounted"},
        {BackendRole,      "backend"},
    };
}
