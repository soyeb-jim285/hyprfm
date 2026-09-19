#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "models/recentfilesmodel.h"

class TestRecentFilesModel : public QObject
{
    Q_OBJECT

private slots:
    // The saved list is loaded as is and entries whose file is gone are
    // dropped by a worker afterwards, so a slow disk cannot hold up startup.
    void testMissingEntriesAreDroppedAfterLoading()
    {
        QTemporaryDir dir;
        const QString kept = dir.filePath("kept.txt");
        QFile file(kept);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        QJsonArray entries;
        for (const QString &path : {kept, dir.filePath("gone.txt")})
            entries.append(QJsonObject{{"path", path}, {"time", "2026-09-19T10:00:00"}});
        QFile store(dir.filePath("recents.json"));
        QVERIFY(store.open(QIODevice::WriteOnly));
        store.write(QJsonDocument(entries).toJson());
        store.close();

        RecentFilesModel model(store.fileName());
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), RecentFilesModel::FilePathRole).toString(), kept);
    }
};

QTEST_GUILESS_MAIN(TestRecentFilesModel)
#include "tst_recentfilesmodel.moc"
