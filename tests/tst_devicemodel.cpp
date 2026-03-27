#include <QTest>
#include <QAbstractItemModelTester>
#include "models/devicemodel.h"

class TestDeviceModel : public QObject
{
    Q_OBJECT

private slots:
    void testInitialRefresh()
    {
        DeviceModel model;
        // Constructor calls refresh(); root "/" must always be present
        QVERIFY(model.rowCount() >= 1);
    }

    void testModelConsistency()
    {
        DeviceModel model;
        // QAbstractItemModelTester exercises all model invariants
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        Q_UNUSED(tester)
    }

    void testRoleNames()
    {
        DeviceModel model;
        const QHash<int, QByteArray> roles = model.roleNames();

        QCOMPARE(roles.value(DeviceModel::DeviceNameRole),   QByteArray("deviceName"));
        QCOMPARE(roles.value(DeviceModel::MountPointRole),   QByteArray("mountPoint"));
        QCOMPARE(roles.value(DeviceModel::TotalSizeRole),    QByteArray("totalSize"));
        QCOMPARE(roles.value(DeviceModel::FreeSpaceRole),    QByteArray("freeSpace"));
        QCOMPARE(roles.value(DeviceModel::UsagePercentRole), QByteArray("usagePercent"));
        QCOMPARE(roles.value(DeviceModel::RemovableRole),    QByteArray("removable"));
        QCOMPARE(roles.value(DeviceModel::MountedRole),      QByteArray("mounted"));

        QCOMPARE(roles.size(), 7);
    }

    void testRootDevicePresent()
    {
        DeviceModel model;

        int rootRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            if (model.data(idx, DeviceModel::MountPointRole).toString() == "/") {
                rootRow = i;
                break;
            }
        }

        QVERIFY2(rootRow >= 0, "Root mount point '/' not found in DeviceModel");

        QModelIndex idx = model.index(rootRow);
        QCOMPARE(model.data(idx, DeviceModel::RemovableRole).toBool(), false);
        QCOMPARE(model.data(idx, DeviceModel::MountedRole).toBool(), true);

        qint64 total = model.data(idx, DeviceModel::TotalSizeRole).toLongLong();
        QVERIFY2(total > 0, "Root device totalSize should be > 0");

        int usage = model.data(idx, DeviceModel::UsagePercentRole).toInt();
        QVERIFY(usage >= 0);
        QVERIFY(usage <= 100);
    }

    void testNoVirtualFilesystems()
    {
        DeviceModel model;

        const QStringList virtualPrefixes = { "/proc", "/sys", "/dev" };

        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            QString mp = model.data(idx, DeviceModel::MountPointRole).toString();

            for (const QString &prefix : virtualPrefixes) {
                bool isVirtual = (mp == prefix) || mp.startsWith(prefix + "/");
                QVERIFY2(!isVirtual,
                    qPrintable(QString("Virtual filesystem found in model: %1").arg(mp)));
            }
        }
    }

    void testDeviceNamesNotEmpty()
    {
        DeviceModel model;

        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            QString name = model.data(idx, DeviceModel::DeviceNameRole).toString();
            QVERIFY2(!name.isEmpty(),
                qPrintable(QString("Device at row %1 has empty name").arg(i)));
        }
    }

    void testFreeSpaceLessOrEqualTotal()
    {
        DeviceModel model;

        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            qint64 total = model.data(idx, DeviceModel::TotalSizeRole).toLongLong();
            qint64 free  = model.data(idx, DeviceModel::FreeSpaceRole).toLongLong();
            QVERIFY2(free <= total,
                qPrintable(QString("Row %1: freeSpace (%2) > totalSize (%3)")
                    .arg(i).arg(free).arg(total)));
        }
    }

    void testInvalidIndex()
    {
        DeviceModel model;
        QVariant result = model.data(QModelIndex(), DeviceModel::DeviceNameRole);
        QVERIFY(!result.isValid());
    }

    void testOutOfBoundsIndex()
    {
        DeviceModel model;
        QModelIndex idx = model.index(9999);
        QVariant result = model.data(idx, DeviceModel::DeviceNameRole);
        QVERIFY(!result.isValid());
    }

    void testRefreshIdempotent()
    {
        DeviceModel model;
        int countBefore = model.rowCount();

        model.refresh();
        model.refresh();

        QCOMPARE(model.rowCount(), countBefore);
    }

    void testMountOutOfBounds()
    {
        DeviceModel model;
        // These must not crash
        model.mount(-1);
        model.mount(9999);
        model.unmount(-1);
        model.unmount(9999);
    }

    void testUsagePercentBounds()
    {
        DeviceModel model;

        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            int usage = model.data(idx, DeviceModel::UsagePercentRole).toInt();
            QVERIFY2(usage >= 0,
                qPrintable(QString("Row %1: usagePercent %2 < 0").arg(i).arg(usage)));
            QVERIFY2(usage <= 100,
                qPrintable(QString("Row %1: usagePercent %2 > 100").arg(i).arg(usage)));
        }
    }
};

QTEST_MAIN(TestDeviceModel)
#include "tst_devicemodel.moc"
