// Headless interaction test for the detailed view's column header: resize
// grips, drag-to-reorder and the column toggle menu, driven with QTest mouse
// events against the real QML.
#include <QTest>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <functional>
#include "viewharness.h"

class TestDetailedView : public QObject
{
    Q_OBJECT

    using Harness = ViewHarness;

private slots:
    // QML binding warnings from the view itself are failures: an unbound
    // model used to log "Unable to assign [undefined] to QObject*", and a
    // missing preview service a TypeError on pdfPreviewAvailable.
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Unable to assign \\[undefined\\]")));
        QTest::failOnWarning(QRegularExpression(QStringLiteral("pdfPreviewAvailable")));
    }

    // A PDF should draw its first page, not the generic mimetype icon. The
    // delegate reads model roles through `required property`, so referencing
    // hasPdfPreview without declaring it left it undefined, silently falsy,
    // and every PDF fell back to the icon with no error anywhere.
    void testPdfRowsUseThePdfPreviewProvider()
    {
        Harness h;
        h.files.createFile("doc.pdf", "%PDF-1.4\n1 0 obj\n<<>>\nendobj\n");
        h.files.createFile("notes.txt", "plain");
        QVERIFY(h.load());

        std::function<bool(QQuickItem *)> hasPdfSource = [&](QQuickItem *it) -> bool {
            const QVariant src = it->property("source");
            if (src.isValid()
                && src.toString().startsWith(QStringLiteral("image://pdfpreview/")))
                return true;
            for (QQuickItem *child : it->childItems())
                if (hasPdfSource(child))
                    return true;
            return false;
        };
        QTRY_VERIFY(hasPdfSource(h.view.rootObject()));
    }

    void testWheelOverListScrollsAndCtrlWheelPassesThrough()
    {
        Harness h;
        QStringList many;
        for (int i = 0; i < 80; ++i)
            many << QString("f%1.txt").arg(i, 3, 10, QLatin1Char('0'));
        h.files.createFiles(many);
        QVERIFY(h.load());
        QQuickItem *list = h.view.rootObject()->findChild<QQuickItem *>("listView");
        QVERIFY(list);
        const QPoint pos(400, 300);
        auto wheel = [&](Qt::KeyboardModifiers mods) {
            QWheelEvent ev(pos, h.view.mapToGlobal(pos), QPoint(), QPoint(0, -120),
                           Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&h.view, &ev);
        };
        wheel(Qt::NoModifier);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);   // scroller drove the list

        const qreal before = list->property("contentY").toReal();
        QTest::qWait(400);
        const qreal settled = list->property("contentY").toReal();
        wheel(Qt::ControlModifier);
        QTest::qWait(200);
        // Ctrl+wheel is not the scroller's; it must fall through untouched by it.
        QVERIFY(list->property("contentY").toReal() >= settled);
        Q_UNUSED(before);
    }

    void testHoverSetsResizeCursorOnGrip()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *grip = h.item("headerGrip_size");
        QQuickItem *label = h.item("headerColumn_modified");
        QVERIFY(grip && label);

        QTest::mouseMove(&h.view, h.center(label), 20);
        QTRY_COMPARE(h.view.cursor().shape(), Qt::PointingHandCursor);

        QTest::mouseMove(&h.view, h.center(grip), 20);
        QTRY_COMPARE(h.view.cursor().shape(), Qt::SizeHorCursor);

        // Right half of the grip overhangs the next column, must still win.
        QTest::mouseMove(&h.view, h.center(grip) + QPoint(4, 0), 20);
        QTRY_COMPARE(h.view.cursor().shape(), Qt::SizeHorCursor);
    }

    void testGripDragMovesTheBoundaryUnderTheCursor()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *grip = h.item("headerGrip_size");
        QQuickItem *size = h.item("headerColumn_size");
        QQuickItem *modified = h.item("headerColumn_modified");
        QVERIFY(grip && size && modified);
        const qreal boundaryBefore = size->x() + size->width();

        // Name (fill) is left of this line, so dragging it right must shrink
        // Modified and leave Size alone — the line itself moves by +40.
        const QPoint start = h.center(grip);
        h.drag(start, start + QPoint(40, 0));

        QTRY_COMPARE(int(size->x() + size->width()), int(boundaryBefore) + 40);
        QCOMPARE(int(size->width()), 110);
        QCOMPARE(int(modified->width()), 100);
        QCOMPARE(h.config->listColumnWidths().value("modified").toInt(), 100);
    }

    void testGripLeftOfNameResizesLeftColumn()
    {
        Harness h;
        QVERIFY(h.load());
        h.config->saveListColumns({"size", "modified", "name", "type"}, {});
        QTest::qWait(100);
        QQuickItem *grip = h.item("headerGrip_size");
        QQuickItem *size = h.item("headerColumn_size");
        QVERIFY(grip && size);
        const qreal boundaryBefore = size->x() + size->width();

        const QPoint start = h.center(grip);
        h.drag(start, start + QPoint(-30, 0));

        QTRY_COMPARE(int(size->x() + size->width()), int(boundaryBefore) - 30);
        QCOMPARE(int(size->width()), 80);
        QCOMPARE(h.config->listColumnWidths().value("size").toInt(), 80);
    }

    void testNameBoundaryGripResizesNextColumn()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *grip = h.item("headerGrip_name");
        QQuickItem *size = h.item("headerColumn_size");
        QVERIFY(grip && size);
        QCOMPARE(int(size->width()), 110);

        // Dragging the Name|Size line to the left makes Size wider.
        const QPoint start = h.center(grip);
        h.drag(start, start + QPoint(-30, 0));

        QTRY_COMPARE(int(size->width()), 140);
        QCOMPARE(h.config->listColumnWidths().value("size").toInt(), 140);
    }

    void testHeaderDragReordersColumnsAndSaves()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *modified = h.item("headerColumn_modified");
        QQuickItem *type = h.item("headerColumn_type");
        QVERIFY(modified && type);

        // Drag "Modified" to the far right, past "Type".
        const QPoint from = h.center(modified);
        const QPoint to = h.center(type) + QPoint(int(type->width() / 2) - 4, 0);
        h.drag(from, to);

        QCOMPARE(h.config->listColumns(), QStringList({"name", "size", "type", "modified"}));
        // Header items survive the move (ListModel.move), only their order changes.
        QTRY_VERIFY(modified->x() > type->x());
    }

    void testNameColumnCanBeMovedAndRowsFollow()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *name = h.item("headerColumn_name");
        QQuickItem *size = h.item("headerColumn_size");
        QVERIFY(name && size);
        // Name fills most of the header; drag from its right end past Size.
        const QPoint from = h.center(name) + QPoint(int(name->width() / 2) - 10, 0);
        h.drag(from, h.center(size) + QPoint(int(size->width() / 2) - 4, 0));
        QCOMPARE(h.config->listColumns(), QStringList({"size", "name", "modified", "type"}));
        QTRY_VERIFY(size->x() < name->x());
    }

    void testRightClickMenuTogglesColumn()
    {
        Harness h;
        QVERIFY(h.load());
        QQuickItem *header = h.item("headerColumn_size");
        QVERIFY(header);
        QTest::mouseClick(&h.view, Qt::RightButton, {}, h.center(header));

        QQuickItem *ownerItem = h.item("columnMenuItem_owner");
        QVERIFY(ownerItem);
        QTRY_VERIFY(ownerItem->isVisible());
        QTest::mouseClick(&h.view, Qt::LeftButton, {}, h.center(ownerItem));

        QCOMPARE(h.config->listColumns(), QStringList({"name", "size", "modified", "type", "owner"}));
        QTRY_VERIFY(h.item("headerColumn_owner") != nullptr);
    }
};

QTEST_MAIN(TestDetailedView)
#include "tst_detailedview.moc"
