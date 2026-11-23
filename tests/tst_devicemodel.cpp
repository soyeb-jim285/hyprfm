#include <QTest>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStorageInfo>
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
        QCOMPARE(roles.value(DeviceModel::DevicePathRole),   QByteArray("devicePath"));
        QCOMPARE(roles.value(DeviceModel::MountPointRole),   QByteArray("mountPoint"));
        QCOMPARE(roles.value(DeviceModel::TotalSizeRole),    QByteArray("totalSize"));
        QCOMPARE(roles.value(DeviceModel::FreeSpaceRole),    QByteArray("freeSpace"));
        QCOMPARE(roles.value(DeviceModel::UsagePercentRole), QByteArray("usagePercent"));
        QCOMPARE(roles.value(DeviceModel::RemovableRole),    QByteArray("removable"));
        QCOMPARE(roles.value(DeviceModel::MountedRole),      QByteArray("mounted"));
        QCOMPARE(roles.value(DeviceModel::BackendRole),      QByteArray("backend"));
        QCOMPARE(roles.value(DeviceModel::UserUnmountableRole), QByteArray("userUnmountable"));

        QCOMPARE(roles.size(), 10);
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

        // Synthetic 1 EiB quota and out-of-range free values must not overflow qint64 or leave [0, 100].
        const qint64 oneEiB = qint64(1) << 60;
        QCOMPARE(DeviceModel::usagePercentFor(oneEiB, oneEiB / 4), 75);
        QCOMPARE(DeviceModel::usagePercentFor(oneEiB, oneEiB * 2), 0);
        QCOMPARE(DeviceModel::usagePercentFor(oneEiB, -1), 100);
        QCOMPARE(DeviceModel::usagePercentFor(0, 0), 0);
    }

    void testUserUnmountableRule()
    {
        // what the sidebar offers to unmount with one click
        QVERIFY(DeviceModel::isUserUnmountable("/run/media/u/USB", false));
        QVERIFY(DeviceModel::isUserUnmountable("/media/USB", false));
        QVERIFY(DeviceModel::isUserUnmountable("/home/u/Google Drive", true));
        QVERIFY(DeviceModel::isUserUnmountable("mtp://phone/", true));
        QVERIFY(!DeviceModel::isUserUnmountable("/", false));
        QVERIFY(!DeviceModel::isUserUnmountable("/", true));
        QVERIFY(!DeviceModel::isUserUnmountable("/mnt/winapps", false));
        QVERIFY(!DeviceModel::isUserUnmountable("/home", false));
        QVERIFY(!DeviceModel::isUserUnmountable("", true));
    }

    void testUserFuseMountFilter()
    {
        const QString home = QStringLiteral("/home/u");
        // rclone remotes, sshfs: inside the home folder -> a device
        QVERIFY(DeviceModel::isUserFuseMount("fuse.rclone", "/home/u/Google Drive", home));
        QVERIFY(DeviceModel::isUserFuseMount("fuse.sshfs", "/home/u/mnt/server", home));
        QVERIFY(DeviceModel::isUserFuseMount("fuse", "/home/u/x", home));
        QVERIFY(DeviceModel::isUserFuseMount("fuse.rclone", "/home/u/Filen", home + "/"));
        // not FUSE, not inside home, home itself, a sibling home, the desktop's own helpers
        QVERIFY(!DeviceModel::isUserFuseMount("ext4", "/home/u/disk", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuseblk", "/home/u/ntfs", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.rclone", "/mnt/remote", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.rclone", "/home/u", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.rclone", "/home/u2/Filen", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.portal", "/home/u/.doc", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.gvfsd-fuse", "/home/u/.gvfs", home));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.rclone", "/x", ""));
        QVERIFY(!DeviceModel::isUserFuseMount("fuse.rclone", "/x", "/"));
    }

    // Every FUSE mount the user has in their home right now (an rclone Google
    // Drive or Filen, say) is listed while it is mounted, with its size.
    void testUserFuseMountsListed()
    {
        QFile mountinfo(QStringLiteral("/proc/self/mountinfo"));
        QVERIFY(mountinfo.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto unescape = [](QString s) {
            static const QRegularExpression octal(QStringLiteral("\\\\([0-7]{3})"));
            QRegularExpressionMatch m;
            while ((m = octal.match(s)).hasMatch())
                s.replace(m.capturedStart(), m.capturedLength(), QChar(m.captured(1).toInt(nullptr, 8)));
            return s;
        };
        QStringList expected;
        for (const QString &line : QString::fromUtf8(mountinfo.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const QStringList halves = line.split(QStringLiteral(" - "));
            if (halves.size() < 2)
                continue;
            const QStringList pre = halves.at(0).split(' ');
            const QStringList post = halves.at(1).split(' ');
            if (pre.size() < 5 || post.isEmpty())
                continue;
            const QString path = unescape(pre.at(4));
            // (a mount whose daemon died or hangs has no size to show and is left out)
            if (DeviceModel::isUserFuseMount(post.at(0), path, QDir::homePath())
                && QStorageInfo(path).isValid())
                expected << path;
        }
        if (expected.isEmpty())
            QSKIP("no FUSE mount in the home folder on this machine");

        DeviceModel model;
        // (looked up afresh each time: GIO events may reset the model meanwhile)
        const auto rowFor = [&model](const QString &path) {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.data(model.index(i), DeviceModel::MountPointRole).toString() == path)
                    return i;
            return -1;
        };
        const auto value = [&](const QString &path, int role) {
            const int row = rowFor(path);
            return row < 0 ? QVariant() : model.data(model.index(row), role);
        };
        for (const QString &path : std::as_const(expected)) {
            QVERIFY2(rowFor(path) >= 0, qPrintable(QStringLiteral("FUSE mount not listed: %1").arg(path)));
            QCOMPARE(value(path, DeviceModel::BackendRole).toString(), QStringLiteral("gio"));
            QVERIFY(value(path, DeviceModel::MountedRole).toBool());
            QVERIFY(!value(path, DeviceModel::DeviceNameRole).toString().isEmpty());
            // The size arrives from a probe off the UI thread.
            if (QStorageInfo(path).bytesTotal() > 0)
                QTRY_VERIFY_WITH_TIMEOUT(value(path, DeviceModel::TotalSizeRole).toLongLong() > 0, 10000);
            QVERIFY(value(path, DeviceModel::FreeSpaceRole).toLongLong()
                    <= value(path, DeviceModel::TotalSizeRole).toLongLong());
        }
    }

    void testBackendRoleValues()
    {
        DeviceModel model;

        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            const QString backend = model.data(idx, DeviceModel::BackendRole).toString();
            QVERIFY2(backend == QStringLiteral("udisks2")
                         || backend == QStringLiteral("gio")
                         || backend == QStringLiteral("mtp-missing"),
                     qPrintable(QString("Unexpected backend at row %1: %2")
                                    .arg(i)
                                    .arg(backend)));
        }
    }
};

QTEST_MAIN(TestDeviceModel)
#include "tst_devicemodel.moc"
