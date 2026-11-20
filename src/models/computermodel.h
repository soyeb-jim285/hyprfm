#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QVariantMap>

// A read-only device overview. Mount points are navigation destinations, never
// file-operation selections, and missing/synthetic capacities stay unknown.
class ComputerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { Name = Qt::UserRole + 1, DevicePath, MountPoint, Total, Free, Usage,
                Mounted, Removable, Backend, SourceIndex, CapacityKnown, Icon };
    explicit ComputerModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    void setDevices(QAbstractItemModel *devices);
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE void refresh();
    static bool validCapacity(qint64 total, qint64 free);
    static QString deviceIcon(const QString &name, const QString &path);
signals:
    void countChanged();
private:
    QPointer<QAbstractItemModel> m_devices;
    QList<QVariantMap> m_rows;
    bool m_loaded = false;
};
