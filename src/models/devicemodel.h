#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QJsonObject>
#include <QProcess>

struct DeviceEntry {
    QString deviceName;
    QString devicePath;   // block device path, e.g. /dev/nvme0n1p5
    QString mountPoint;   // empty if unmounted
    qint64  totalSize;
    qint64  freeSpace;
    int     usagePercent;
    bool    removable;
    bool    mounted;
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
    };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void unmount(int index);
    Q_INVOKABLE void mount(int index);

signals:
    void deviceMounted(const QString &mountPoint);

private:
    void setupUDisks2();
    void processDevice(const QJsonObject &dev, bool parentRemovable = false);
    static bool isVirtual(const QString &fsType);

    QList<DeviceEntry> m_devices;
};
