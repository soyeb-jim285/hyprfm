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
#include <QStorageInfo>
#include <QDebug>
#include <algorithm>

static const QStringList kVirtualTypes = {
    "tmpfs", "devtmpfs", "proc", "sysfs",
    "cgroup", "cgroup2", "overlay", "squashfs",
    "devpts", "hugetlbfs", "mqueue", "pstore",
    "securityfs", "fusectl", "debugfs", "tracefs",
    "bpf", "autofs", "ramfs", "efivarfs",
};

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
    setupUDisks2();
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
        this, SLOT(refresh())
    );

    sys.connect(
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved",
        this, SLOT(refresh())
    );
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

} // namespace

void DeviceModel::refresh()
{
    beginResetModel();
    m_devices.clear();

    // Ask UDisks2 for everything it knows about. Returns a{oa{sa{sv}}}:
    //   { object_path → { interface_name → { property_name → value } } }
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        QStringLiteral("/org/freedesktop/UDisks2"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));

    QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "DeviceModel: UDisks2 GetManagedObjects failed:"
                   << reply.errorMessage();
        endResetModel();
        return;
    }

    QHash<QString, DriveInfo> drives;
    QHash<QString, BlockInfo> blocks;

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
                          total, free, usage, removable, mounted});
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

void DeviceModel::unmount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const DeviceEntry &dev = m_devices.at(index);
    if (!dev.mounted)
        return;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        udisksObjectPathFor(dev.devicePath),
        QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
        QStringLiteral("Unmount"));
    msg << QVariant::fromValue(QVariantMap{});

    const QString label = m_devices.at(index).deviceName;

    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, label]() {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            const QString err = reply.error().message();
            qWarning() << "UDisks2 Unmount failed:" << err;
            emit mountError(tr("Could not unmount %1: %2").arg(label, err));
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
    const QString devicePath = entry.devicePath;
    const QString fsType = entry.fsType;
    const QString label = entry.deviceName;

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

            if (isNtfs && dirtyNtfs) {
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
    };
}
