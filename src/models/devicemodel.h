#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QList>
#include <QTimer>

typedef struct _GMount GMount;
typedef struct _GVolume GVolume;
typedef struct _GVolumeMonitor GVolumeMonitor;

enum class DeviceBackend {
    UDisks2,
    Gio,
    MtpMissing,   // Android phone seen by udev, but gvfs-mtp is not installed
};

struct DeviceEntry {
    QString deviceName;
    QString devicePath;   // block device path, e.g. /dev/nvme0n1p5
    QString mountPoint;   // empty if unmounted
    QString fsType;       // e.g. "ntfs", "ext4" — used for error messages
    qint64  totalSize;
    qint64  freeSpace;
    int     usagePercent;
    bool    removable;
    bool    mounted;
    QString alternateMountPoint;
    DeviceBackend backend = DeviceBackend::UDisks2;
    GVolume *gioVolume = nullptr;
    GMount *gioMount = nullptr;
};

class QDBusMessage;

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        DeviceNameRole = Qt::UserRole + 1,
        DevicePathRole,
        MountPointRole,
        TotalSizeRole,
        FreeSpaceRole,
        UsagePercentRole,
        RemovableRole,
        MountedRole,
        BackendRole,
        UserUnmountableRole,
    };

    explicit DeviceModel(QObject *parent = nullptr, bool deferInitialRefresh = false);
    ~DeviceModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void unmount(int index);
    Q_INVOKABLE void mount(int index);

    // A FUSE filesystem the user mounted inside their home folder -- an rclone
    // remote (Google Drive, Filen, ...), sshfs and the like.  These are listed
    // as devices while mounted, with their size, and unmounted with fusermount.
    static bool isUserFuseMount(const QString &fsType, const QString &path, const QString &home);
    // Mounted by the user, so the sidebar may offer to unmount it with one click:
    // phones and FUSE mounts (GIO), and drives auto-mounted under /run/media or
    // /media -- never /, nor fixed mounts such as /mnt/... from fstab.
    static bool isUserUnmountable(const QString &mountPoint, bool gioBackend);
    static int usagePercentFor(qint64 total, qint64 free);

public slots:
    void refresh();
    void refreshAsync();
    void scheduleRefresh();

signals:
    void deviceMounted(const QString &mountPoint);
    void mountError(const QString &message);

private:
    void applyUDisksReply(const QDBusMessage &reply);
    void clearDevices();
    void setupGioMonitor();
    void setupUDisks2();
    void appendUnbrowsableMtpDevices();
    void setupMtpWatcher();
    static bool isVirtual(const QString &fsType);
    void unmountFuse(const QString &mountPoint, const QString &label);
    void probeMountUsage(const QString &mountPoint);
    void applyMountUsage(const QString &mountPoint, qint64 total, qint64 free);

    struct MountUsage { qint64 total = 0; qint64 free = 0; };
    QHash<QString, MountUsage> m_mountUsage;
    QSet<QPair<QString, QByteArray>> m_mountProbesPending;

    QList<DeviceEntry> m_devices;
    GVolumeMonitor *m_volumeMonitor = nullptr;
    quint64 m_refreshGeneration = 0;
    QTimer m_refreshTimer;
    QFileSystemWatcher m_usbWatcher;
    QStringList m_usbDeviceFiles;
};
