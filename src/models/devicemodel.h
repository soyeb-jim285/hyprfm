#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QList>
#include <QTimer>
#include <memory>

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
    // A locked encrypted container (LUKS, BitLocker): mounting it means
    // unlocking it first.  Once unlocked, its opened filesystem is listed
    // instead and the container itself is not.  (Last, so the positional
    // initialisers above stay valid.)
    bool    encrypted = false;
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
        EncryptedRole,
    };

    explicit DeviceModel(QObject *parent = nullptr, bool deferInitialRefresh = false);
    ~DeviceModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void unmount(int index);
    // On a locked encrypted container this asks for its passphrase
    // (unlockRequested) instead of mounting.
    Q_INVOKABLE void mount(int index);
    // Unlock an encrypted container with its passphrase, then mount what it
    // holds.  Answered by deviceUnlocked or unlockFailed (and then
    // deviceMounted or mountError).  By device path: the row may move while the
    // user types.
    Q_INVOKABLE void unlock(const QString &devicePath, const QString &passphrase);

    // A readable name for a filesystem without a label, e.g. "2.0 TB FAT
    // volume", in place of the UUID that names its automatic mount point.
    static QString fallbackName(qint64 size, const QString &idType);
    // A mount point named after a filesystem UUID or serial (what udisks uses
    // for an unlabelled filesystem): "E79B-1256", "63f1d802-9a75-...".
    static bool looksLikeVolumeId(const QString &name);
    static bool isWrongPassphraseError(const QString &message);

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
    void unlockRequested(const QString &devicePath, const QString &name);
    void deviceUnlocked(const QString &devicePath);
    void unlockFailed(const QString &devicePath, const QString &message);

private:
    void applyUDisksReply(const QDBusMessage &reply);
    void clearDevices();
    void setupGioMonitor();
    void setupUDisks2();
    void appendUnbrowsableMtpDevices();
    void setupMtpWatcher();
    static bool isVirtual(const QString &fsType);
    void unmountFuse(const QString &mountPoint, const QString &label);
    void probeFuseUsage(const QString &mountPoint);
    void applyFuseUsage(const QString &mountPoint, qint64 total, qint64 free);
    void mountUDisksFilesystem(const QString &objectPath, const QString &fsType,
                               const QString &label);

    // Size of each user FUSE mount, as last answered, so a refresh shows it at once;
    // probes in flight, at most one per mount (a hung daemon keeps its probe).
    struct FuseUsage { qint64 total = 0; qint64 free = 0; };
    struct FuseProbeContext {
        QMutex mutex;
        DeviceModel *model = nullptr;
    };
    std::shared_ptr<FuseProbeContext> m_fuseProbeContext = std::make_shared<FuseProbeContext>();
    QHash<QString, FuseUsage> m_fuseUsage;
    QSet<QString> m_fuseProbesPending;

    QList<DeviceEntry> m_devices;
    GVolumeMonitor *m_volumeMonitor = nullptr;
    quint64 m_refreshGeneration = 0;
    QTimer m_refreshTimer;
    QFileSystemWatcher m_usbWatcher;
    QStringList m_usbDeviceFiles;
};
