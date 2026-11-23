#include "computermodel.h"
#include <QFile>
#include <QStorageInfo>
#include <QSet>
#include <algorithm>

bool ComputerModel::validCapacity(qint64 total, qint64 free)
{
    // The same threshold Strata uses for rclone/FUSE's 1 PiB and 18 EB placeholders.
    return total > 0 && total < (qint64(1) << 50) && free >= 0;
}

QString ComputerModel::deviceIcon(const QString &name, const QString &path)
{
    const QString value = (name + ' ' + path).toLower();
    if (value.contains("google drive") || value.contains("google-drive") || value.contains("gdrive")) return "strata-google-drive";
    if (value.contains("proton")) return "strata-proton-drive";
    if (value.contains("filelu")) return "strata-filelu";
    if (value.contains("scramble")) return "strata-scramble-cloud";
    if (value.contains("nixos")) return "strata-nixos";
    if (value.contains("linux")) return "strata-linux";
    if (path == "/") return QFile::exists("/etc/NIXOS") ? "strata-nixos" : "strata-linux";
    for (const QString &provider : {QString("cloud"), QString("rclone"), QString("onedrive"), QString("dropbox"), QString("nextcloud")})
        if (value.contains(provider)) return "strata-cloud";
    if (value.contains("mtp:") || value.contains("afc:")) return "strata-smartphone";
    return "strata-hard-drive";
}

void ComputerModel::setDevices(QAbstractItemModel *devices)
{
    if (m_devices == devices) return;
    if (m_devices) disconnect(m_devices, nullptr, this, nullptr);
    m_devices = devices;
    if (devices) {
        const auto update = [this]() { if (m_loaded) refresh(); };
        connect(devices, &QAbstractItemModel::modelReset, this, update);
        connect(devices, &QAbstractItemModel::rowsInserted, this, update);
        connect(devices, &QAbstractItemModel::rowsRemoved, this, update);
        connect(devices, &QAbstractItemModel::dataChanged, this, update);
        connect(devices, &QObject::destroyed, this, [this]() { m_devices = nullptr; if (m_loaded) refresh(); });
    }
    if (m_loaded) refresh();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    return {{Name,"deviceName"}, {DevicePath,"devicePath"}, {MountPoint,"mountPoint"},
            {Total,"totalSize"}, {Free,"freeSpace"}, {Usage,"usagePercent"}, {Mounted,"mounted"},
            {Removable,"removable"}, {Backend,"backend"}, {SourceIndex,"sourceIndex"},
            {CapacityKnown,"capacityKnown"}, {Icon,"deviceIcon"}};
}

QVariant ComputerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    return m_rows[index.row()].value(QString::fromUtf8(roleNames().value(role)));
}

QVariantMap ComputerModel::get(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows[index] : QVariantMap{};
}

void ComputerModel::refresh()
{
    m_loaded = true;
    QList<QVariantMap> rows;
    QSet<QString> mounts;
    if (m_devices) {
        const auto names = m_devices->roleNames();
        for (int index = 0; index < m_devices->rowCount(); ++index) {
            QVariantMap row;
            for (auto it = names.begin(); it != names.end(); ++it)
                row[QString::fromUtf8(it.value())] = m_devices->data(m_devices->index(index, 0), it.key());
            const QString mount = row.value("mountPoint").toString();
            if (!mount.isEmpty() && mounts.contains(mount)) continue;
            if (!mount.isEmpty()) mounts.insert(mount);
            row["sourceIndex"] = index;
            rows.append(row);
        }
    }
    // Computer remains usable without UDisks/GVfs. Only stat the local root;
    // remote usage comes from the existing device monitor, never synchronous
    // probes of remote/FUSE mount points on the UI thread.
    if (!mounts.contains("/")) {
        const QStorageInfo root = QStorageInfo::root();
        rows.prepend({{"deviceName", tr("File System")}, {"devicePath", QString::fromUtf8(root.device())},
                      {"mountPoint", "/"}, {"totalSize", root.bytesTotal()}, {"freeSpace", root.bytesAvailable()},
                      {"mounted", true}, {"removable", false}, {"backend", "local"}, {"sourceIndex", -1}});
    }
    for (auto &row : rows) {
        const qint64 total = row.value("totalSize").toLongLong();
        const qint64 free = row.value("freeSpace").toLongLong();
        const bool known = row.value("mounted").toBool() && validCapacity(total, free);
        row["capacityKnown"] = known;
        row["totalSize"] = known ? total : qint64(0);
        row["freeSpace"] = known ? qBound(qint64(0), free, total) : qint64(0);
        row["usagePercent"] = known ? qBound(0, int(100.0 * (total - qMin(free, total)) / total), 100) : 0;
        row["deviceIcon"] = deviceIcon(row.value("deviceName").toString(), row.value("mountPoint").toString().isEmpty()
                                     ? row.value("devicePath").toString() : row.value("mountPoint").toString());
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap &left, const QVariantMap &right) {
        if (left.value("mountPoint") == "/") return right.value("mountPoint") != "/";
        if (right.value("mountPoint") == "/") return false;
        return left.value("deviceName").toString().compare(right.value("deviceName").toString(), Qt::CaseInsensitive) < 0;
    });
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}
