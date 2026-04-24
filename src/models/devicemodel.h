#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QTimer>

typedef struct _GMount GMount;
typedef struct _GVolume GVolume;
typedef struct _GVolumeMonitor GVolumeMonitor;

enum class DeviceBackend {
    UDisks2,
    Gio,
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
    };

    explicit DeviceModel(QObject *parent = nullptr, bool deferInitialRefresh = false);
    ~DeviceModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void unmount(int index);
    Q_INVOKABLE void mount(int index);

public slots:
    void refresh();
    void scheduleRefresh();

signals:
    void deviceMounted(const QString &mountPoint);
    void mountError(const QString &message);

private:
    void clearDevices();
    void setupGioMonitor();
    void setupUDisks2();
    static bool isVirtual(const QString &fsType);

    QList<DeviceEntry> m_devices;
    GVolumeMonitor *m_volumeMonitor = nullptr;
    QTimer m_refreshTimer;
};
