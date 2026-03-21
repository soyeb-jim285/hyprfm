#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QDBusInterface>
#include <QProcess>

struct DeviceEntry {
    QString deviceName;
    QString mountPoint;
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

private:
    void setupUDisks2();
    static bool isVirtual(const QString &fsType);
    static QString formatSize(qint64 bytes);

    QList<DeviceEntry> m_devices;
};
