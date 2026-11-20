#include <QTest>
#include <QStandardItemModel>
#include "models/computermodel.h"

class TestComputerModel : public QObject
{
    Q_OBJECT
private slots:
    void capacityAndSourceChanges()
    {
        QStandardItemModel devices;
        devices.setItemRoleNames({{Qt::UserRole + 1, "deviceName"}, {Qt::UserRole + 2, "mountPoint"},
                                 {Qt::UserRole + 3, "totalSize"}, {Qt::UserRole + 4, "freeSpace"},
                                 {Qt::UserRole + 5, "mounted"}});
        auto append = [&](const QString &name, const QString &mount, qint64 total, qint64 free, bool mounted) {
            auto *item = new QStandardItem;
            item->setData(name, Qt::UserRole + 1);
            item->setData(mount, Qt::UserRole + 2);
            item->setData(total, Qt::UserRole + 3);
            item->setData(free, Qt::UserRole + 4);
            item->setData(mounted, Qt::UserRole + 5);
            devices.appendRow(item);
        };
        append("Root", "/", 1000, 250, true);
        append("Google Drive", "/mnt/gdrive", qint64(1) << 50, qint64(1) << 50, true);
        append("USB", "", 0, 0, false);
        append("Duplicate", "/", 1000, 250, true);
        ComputerModel model;
        model.setDevices(&devices);
        model.refresh();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.get(0).value("mountPoint").toString(), "/");
        QCOMPARE(model.get(0).value("usagePercent").toInt(), 75);
        QVERIFY(model.get(0).value("capacityKnown").toBool());
        const auto cloud = model.get(1);
        QCOMPARE(cloud.value("sourceIndex").toInt(), 1);
        QVERIFY(!cloud.value("capacityKnown").toBool());
        QCOMPARE(cloud.value("totalSize").toLongLong(), 0);
        QCOMPARE(cloud.value("deviceIcon").toString(), "strata-google-drive");
        QVERIFY(!model.get(2).value("capacityKnown").toBool());
        devices.item(1)->setData(qint64(100), Qt::UserRole + 3);
        devices.item(1)->setData(qint64(200), Qt::UserRole + 4);
        QVERIFY(model.get(1).value("capacityKnown").toBool());
        QCOMPARE(model.get(1).value("freeSpace").toLongLong(), 100);
        QCOMPARE(model.get(1).value("usagePercent").toInt(), 0);
        devices.removeRow(1);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.get(1).value("sourceIndex").toInt(), 1);
    }

    void rootWithoutDeviceService()
    {
        ComputerModel model;
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.get(0).value("mountPoint").toString(), "/");
        QCOMPARE(model.get(0).value("sourceIndex").toInt(), -1);
        QVERIFY(model.get(0).value("mounted").toBool());
    }
};
QTEST_GUILESS_MAIN(TestComputerModel)
#include "tst_computermodel.moc"
