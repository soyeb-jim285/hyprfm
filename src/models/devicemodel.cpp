#include "models/devicemodel.h"

#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

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

void DeviceModel::processDevice(const QJsonObject &dev, bool parentRemovable)
{
    bool rm = dev.value("rm").toBool() || dev.value("hotplug").toBool() || parentRemovable;

    const QJsonArray children = dev.value("children").toArray();
    for (const QJsonValue &child : children)
        processDevice(child.toObject(), rm);

    QString type = dev.value("type").toString();
    if (type != "part" && type != "lvm" && type != "crypt") {
        if (type != "disk" || !children.isEmpty())
            return;
    }

    QString fstype = dev.value("fstype").toString();
    if (fstype.isEmpty() || fstype == "swap")
        return;
    if (isVirtual(fstype))
        return;

    QString path = dev.value("path").toString();
    if (path.startsWith("/dev/loop") || path.startsWith("/dev/zram"))
        return;

    QString mountpoint = dev.value("mountpoint").toString();
    bool mounted = !mountpoint.isEmpty() && mountpoint != "[SWAP]";

    if (mounted && (mountpoint.startsWith("/boot") ||
                    mountpoint.startsWith("/snap") ||
                    mountpoint.startsWith("/nix") ||
                    mountpoint.startsWith("/efi")))
        return;

    // Skip EFI System Partition by GUID or by vfat heuristic
    static const QString kEfiGuid = "c12a7328-f81f-11d2-ba4b-00a098cb80e7";
    QString parttype = dev.value("parttype").toString().toLower();
    if (parttype == kEfiGuid)
        return;

    qint64 size = dev.value("size").toVariant().toLongLong();

    QString label = dev.value("label").toString();
    QString name;
    if (!label.isEmpty())
        name = label;
    else if (mounted && mountpoint == "/")
        name = "/";
    else if (mounted)
        name = mountpoint.section('/', -1);
    else
        name = path.section('/', -1);

    qint64 total = 0, free = 0;
    int usage = 0;
    if (mounted) {
        total = dev.value("fssize").toVariant().toLongLong();
        free  = dev.value("fsavail").toVariant().toLongLong();
        if (total > 0)
            usage = static_cast<int>((total - free) * 100 / total);
    } else {
        total = size;
    }

    m_devices.append({name, path, mounted ? mountpoint : "",
                      total, free, usage, rm, mounted});
}

void DeviceModel::refresh()
{
    beginResetModel();
    m_devices.clear();

    QProcess proc;
    proc.start("lsblk", {"-Jbp", "-o",
        "NAME,PATH,TYPE,FSTYPE,MOUNTPOINT,LABEL,RM,HOTPLUG,SIZE,FSSIZE,FSAVAIL,PARTTYPE"});
    if (!proc.waitForFinished(3000)) {
        qWarning() << "DeviceModel: lsblk timed out";
        endResetModel();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonArray devices = doc.object().value("blockdevices").toArray();
    for (const QJsonValue &dev : devices)
        processDevice(dev.toObject());

    endResetModel();
}

void DeviceModel::unmount(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    const DeviceEntry &dev = m_devices.at(index);
    if (!dev.mounted)
        return;

    auto *proc = new QProcess(this);
    proc->setProgram("udisksctl");
    proc->setArguments({"unmount", "--no-user-interaction", "-b", dev.devicePath});
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

    const QString devicePath = m_devices.at(index).devicePath;

    auto *proc = new QProcess(this);
    proc->setProgram("udisksctl");
    proc->setArguments({"mount", "-b", devicePath});
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, devicePath](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode == 0) {
            refresh();
            for (const auto &dev : m_devices) {
                if (dev.devicePath == devicePath && dev.mounted) {
                    emit deviceMounted(dev.mountPoint);
                    break;
                }
            }
        } else {
            qWarning() << "udisksctl mount failed:" << proc->readAllStandardError();
        }
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
