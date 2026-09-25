// Shared headless harness for the QML views: real ConfigManager / ThemeLoader /
// FileSystemModel, small stubs for the rest, QTest mouse helpers.
#pragma once
#include <QTest>
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <functional>
#include "testdir.h"
#include "models/filesystemmodel.h"
#include "services/configmanager.h"
#include "services/themeloader.h"

class FileOpsStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList pendingTargetPaths READ pendingTargetPaths CONSTANT)
public:
    QStringList pendingTargetPaths() const { return {}; }
    Q_INVOKABLE bool isRemotePath(const QString &) const { return false; }
    Q_INVOKABLE bool isSlowPath(const QString &) const { return false; }
    Q_INVOKABLE bool isArchive(const QString &) const { return false; }
    Q_INVOKABLE bool isExecutable(const QString &) const { return false; }
};

class ClipboardStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isCut READ isCut CONSTANT)
public:
    bool isCut() const { return false; }
    Q_INVOKABLE bool contains(const QString &) const { return false; }
};

class DragHelperStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active CONSTANT)
    Q_PROPERTY(QStringList activePaths READ activePaths CONSTANT)
public:
    bool active() const { return false; }
    QStringList activePaths() const { return {}; }
    Q_INVOKABLE void startDrag(const QStringList &, const QString &, int) {}
signals:
    void dragFinished();
};

struct ViewHarness {
    QTemporaryDir configDir;
    QTemporaryDir moduleDir;
    TestDir files;
    ConfigManager *config = nullptr;
    ThemeLoader *theme = nullptr;
    FileSystemModel *model = nullptr;
    FileOpsStub fileOps;
    ClipboardStub clipboard;
    DragHelperStub dragHelper;
    QQuickView view;

    // Extra context properties a view needs beyond the common set.
    std::function<void(QQmlContext *)> extraContext;

    bool load(const QString &qmlRelPath = QStringLiteral("src/qml/views/FileDetailedView.qml"),
              const char *modelProperty = "viewModel")
    {
        files.createFiles({"a.txt", "b.txt", "c.txt"});
        config = new ConfigManager(configDir.path() + "/config.toml", &view);
        theme = new ThemeLoader(&view);
        theme->loadTheme(QStringLiteral("catppuccin-mocha"), {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        model = new FileSystemModel(&view);
        model->setSynchronousReload(true);
        model->setRootPath(files.path());

        // The generated module qmldir says `prefer :/HyprFM/`, which makes
        // the engine insist on the qrc bundle that only the app binary has.
        // Stage a copy without that line next to a symlink to the qml tree.
        QDir().mkpath(moduleDir.path() + "/HyprFM");
        QFile src(QStringLiteral(TEST_MODULE_DIR "/HyprFM/qmldir"));
        if (!src.open(QIODevice::ReadOnly))
            return false;
        QFile dst(moduleDir.path() + "/HyprFM/qmldir");
        if (!dst.open(QIODevice::WriteOnly))
            return false;
        for (const QByteArray &line : src.readAll().split('\n')) {
            if (!line.startsWith("prefer "))
                dst.write(line + '\n');
        }
        dst.close();
        QFile::link(QStringLiteral(TEST_MODULE_DIR "/HyprFM/qml"), moduleDir.path() + "/HyprFM/qml");

        QQmlEngine *engine = view.engine();
        engine->addImportPath(moduleDir.path());
        engine->addImportPath(QStringLiteral(TEST_SOURCE_DIR "/src/qml"));
        QQmlContext *ctx = view.rootContext();
        ctx->setContextProperty("config", config);
        ctx->setContextProperty("theme", theme);
        ctx->setContextProperty("fileOps", &fileOps);
        ctx->setContextProperty("clipboard", &clipboard);
        ctx->setContextProperty("dragHelper", &dragHelper);
        if (extraContext)
            extraContext(ctx);

        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(900, 600);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/") + qmlRelPath));
        if (view.status() != QQuickView::Ready) {
            for (const auto &e : view.errors())
                qWarning() << e.toString();
            return false;
        }
        view.rootObject()->setProperty(modelProperty, QVariant::fromValue(static_cast<QObject *>(model)));
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            return false;
        QTest::qWait(100);
        return true;
    }

    // Repeater delegates have no QObject parent, so walk the item tree.
    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        for (QQuickItem *child : parent->childItems()) {
            if (child->objectName() == name)
                return child;
            if (QQuickItem *hit = findItem(child, name))
                return hit;
        }
        return nullptr;
    }
    QQuickItem *item(const QString &name)
    {
        QQuickItem *it = findItem(view.rootObject(), name);
        if (!it)
            qWarning() << "no item" << name;
        return it;
    }
    QPoint center(QQuickItem *it)
    {
        return it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint();
    }
    void drag(const QPoint &from, const QPoint &to)
    {
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        const int steps = 8;
        for (int i = 1; i <= steps; ++i) {
            QTest::mouseMove(&view, from + (to - from) * i / steps, 10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, {}, to);
    }
};

