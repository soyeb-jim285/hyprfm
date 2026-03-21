#include "models/devicemodel.h"

#include <QStorageInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcess>
#include <QDebug>

// Filesystem types that are considered virtual / noise
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
    // Connect to UDisks2 ObjectManager signals for plug/unplug events
    QDBusConnection sys = QDBusConnection::systemBus();
    if (!sys.isConnected()) {
        qWarning() << "DeviceModel: cannot connect to system D-Bus; auto-refresh disabled";
        return;
    }

    // InterfacesAdded
    sys.connect(
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded",
        this,
        SLOT(refresh())
    );

    // InterfacesRemoved
    sys.connect(
        "org.freedesktop.UDisks2",
        "/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved",
        this,
        SLOT(refresh())
    );
}

void DeviceModel::refresh()
{
    beginResetModel();
    m_devices.clear();

    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &vol : volumes) {
        if (!vol.isValid() || !vol.isReady())
            continue;

        const QString fsType = vol.fileSystemType();
        if (isVirtual(fsType))
            continue;

        // Skip very short mount points that look like kernel pseudo-mounts
        const QString mp = vol.rootPath();
        if (mp.isEmpty())
            continue;

        const qint64 total = vol.bytesTotal();
        const qint64 free  = vol.bytesFree();
        const int usage    = (total > 0)
            ? static_cast<int>((total - free) * 100 / total)
            : 0;

        // Heuristic: consider a device removable if its root path is NOT / or /boot*
        // and its device name starts with /dev/sd, /dev/nvme, etc.
        // We also treat loop and zram devices as non-removable internal.
        const QString devName = vol.device();
        bool removable = false;
        if (!devName.startsWith("/dev/loop") &&
            !devName.startsWith("/dev/zram") &&
            mp != "/" &&
            !mp.startsWith("/boot") &&
            !mp.startsWith("/nix") &&
            !mp.startsWith("/snap"))
        {
            // Anything mounted under /media or /run/media is likely removable
            removable = mp.startsWith("/media") || mp.startsWith("/run/media");
        }

        // Friendly name: last component of device or mount point
        QString name = vol.displayName();
        if (name.isEmpty() || name == mp) {
            name = mp.section('/', -1);
            if (name.isEmpty())
                name = "/";
        }

        m_devices.append({
            name,
            mp,
            total,
            free,
            usage,
            removable,
            true
        });
    }

    endResetModel();
}

void DeviceModel::unmount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const QString mp = m_devices.at(index).mountPoint;

    auto *proc = new QProcess(this);
    proc->setProgram("udisksctl");
    proc->setArguments({"unmount", "--no-user-interaction", "-p",
                        "block_devices/" + mp.section('/', -1)});
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode == 0)
            refresh();
        else
            qWarning() << "udisksctl unmount failed:" << proc->readAllStandardError();
    });
    proc->start();
}

void DeviceModel::mount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const QString devName = m_devices.at(index).deviceName;

    auto *proc = new QProcess(this);
    proc->setProgram("udisksctl");
    proc->setArguments({"mount", "--no-user-interaction", "-b", devName});
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode == 0)
            refresh();
        else
            qWarning() << "udisksctl mount failed:" << proc->readAllStandardError();
    });
    proc->start();
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
        {MountPointRole,   "mountPoint"},
        {TotalSizeRole,    "totalSize"},
        {FreeSpaceRole,    "freeSpace"},
        {UsagePercentRole, "usagePercent"},
        {RemovableRole,    "removable"},
        {MountedRole,      "mounted"},
    };
}
