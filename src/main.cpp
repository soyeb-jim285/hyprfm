#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QFontDatabase>
#include <QDBusInterface>
#include <QDBusReply>
#include <QStyleHints>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <unistd.h>
#ifdef HYPRFM_HAS_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

#include "services/configmanager.h"
#include "services/themeloader.h"
#include "services/fileoperations.h"
#include "services/clipboardmanager.h"
#include "services/draghelper.h"
#include "models/filesystemmodel.h"
#include "models/tablistmodel.h"
#include "models/bookmarkmodel.h"
#include "models/devicemodel.h"
#include "models/recentfilesmodel.h"
#include "models/searchresultsmodel.h"
#include "models/searchproxymodel.h"
#include "services/searchservice.h"
#include "services/undomanager.h"
#include "services/previewservice.h"
#include "services/metadataextractor.h"
#include "services/diskusageservice.h"
#include "services/remoteaccessservice.h"
#include "services/rcloneservice.h"
#include "services/runtimefeaturesservice.h"
#include "services/dependencychecker.h"
#include "services/gitstatusservice.h"
#include "services/sessionstate.h"
#include "providers/thumbnailprovider.h"
#include "providers/iconprovider.h"
#include "providers/pdfpreviewprovider.h"
#include <QIcon>
#include <QEvent>
#include <functional>
#include <QUrl>
#include <dlfcn.h>
#include <signal.h>
#include <QCryptographicHash>
#include <QThreadPool>

namespace {

// Renderer choice: Vulkan when the machine has a hardware Vulkan device,
// OpenGL otherwise. On Mesa the OpenGL driver maps libLLVM for its shader
// compiler while RADV/ANV do not: ~47 MB less PSS and a slightly faster
// first frame on a Radeon iGPU. QSG_RHI_BACKEND / QT_QUICK_BACKEND still win.
//
// Qt has no fallback of its own -- a window asked for Vulkan on a machine
// without it never appears -- so it has to be probed, and the probe costs
// ~50 ms (it loads every installed Vulkan driver, which Qt then does again).
// So it never runs on the startup path: the answer is cached in
// ~/.cache/hyprfm/renderer, keyed on the installed Vulkan driver manifests,
// and a launch without a valid answer uses OpenGL and probes on a worker
// thread after its first frame. Each Vulkan launch leaves a per-pid marker
// until it paints; a marker whose process is gone means Vulkan launched and
// never drew, and every later launch stays on OpenGL until the drivers change.

// The loader is reached through dlopen with hand-declared structs rather than
// <vulkan/vulkan.h>, so building HyprFM needs neither the Vulkan headers nor
// libvulkan; the Vulkan ABI is frozen. A CPU-only device (lavapipe) does not
// count: OpenGL on the real GPU beats Vulkan on the CPU.
bool hasHardwareVulkanDevice()
{
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return false;

    struct AppInfo { int sType; const void *next; const char *app; uint32_t appVer;
                     const char *engine; uint32_t engineVer; uint32_t apiVersion; };
    struct CreateInfo { int sType; const void *next; uint32_t flags; const AppInfo *app;
                        uint32_t layers; const char *const *layerNames;
                        uint32_t exts; const char *const *extNames; };
    using Instance = void *;
    using Device = void *;
    using GetProc = void *(*)(Instance, const char *);
    using Create = int (*)(const CreateInfo *, const void *, Instance *);
    using Destroy = void (*)(Instance, const void *);
    using Enumerate = int (*)(Instance, uint32_t *, Device *);
    using GetProps = void (*)(Device, void *);

    bool found = false;
    auto getProc = reinterpret_cast<GetProc>(dlsym(lib, "vkGetInstanceProcAddr"));
    auto create = getProc ? reinterpret_cast<Create>(getProc(nullptr, "vkCreateInstance")) : nullptr;
    const AppInfo app{0 /* APPLICATION_INFO */, nullptr, "hyprfm", 0, nullptr, 0,
                      (1u << 22) | (1u << 12) /* 1.1 */};
    const CreateInfo info{1 /* INSTANCE_CREATE_INFO */, nullptr, 0, &app, 0, nullptr, 0, nullptr};
    Instance instance = nullptr;
    if (create && create(&info, nullptr, &instance) == 0) {
        auto destroy = reinterpret_cast<Destroy>(getProc(instance, "vkDestroyInstance"));
        auto enumerate = reinterpret_cast<Enumerate>(getProc(instance, "vkEnumeratePhysicalDevices"));
        auto props = reinterpret_cast<GetProps>(getProc(instance, "vkGetPhysicalDeviceProperties"));
        uint32_t count = 0;
        if (enumerate && props && enumerate(instance, &count, nullptr) == 0 && count > 0) {
            QList<Device> devices(count);
            enumerate(instance, &count, devices.data());
            for (Device device : std::as_const(devices)) {
                // VkPhysicalDeviceProperties is ~830 bytes; deviceType is the
                // fifth uint32 (after apiVersion, driverVersion, vendorID,
                // deviceID). 4 = VK_PHYSICAL_DEVICE_TYPE_CPU.
                alignas(8) unsigned char buffer[2048] = {};
                props(device, buffer);
                uint32_t deviceType = 0;
                memcpy(&deviceType, buffer + 16, sizeof deviceType);
                if (deviceType != 4) {
                    found = true;
                    break;
                }
            }
        }
        if (destroy)
            destroy(instance, nullptr);
    }
    dlclose(lib);
    return found;
}

// Every place the Vulkan loader looks for driver manifests, with each
// manifest's mtime: installing, removing or upgrading a driver changes it.
QByteArray vulkanDriverFingerprint()
{
    QByteArray data;
    for (const char *var : {"VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES"})
        data += qgetenv(var) + '|';
    const auto dirsFrom = [](const char *var, const QString &fallback) {
        const QString value = qEnvironmentVariable(var);
        return (value.isEmpty() ? fallback : value).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    };
    QStringList roots = dirsFrom("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg"));
    roots << QStringLiteral("/etc");
    roots << dirsFrom("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share"));
    roots << dirsFrom("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    for (const QString &root : std::as_const(roots)) {
        const QDir dir(root + QStringLiteral("/vulkan/icd.d"));
        const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries)
            data += entry.absoluteFilePath().toUtf8() + ':'
                + QByteArray::number(entry.lastModified().toMSecsSinceEpoch()) + '|';
    }
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex();
}

class RendererChoice
{
public:
    RendererChoice()
        : m_dir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                + QStringLiteral("/hyprfm"))
    {
#if QT_CONFIG(vulkan)
        if (!qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")
            || !qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
            return;

        m_fingerprint = vulkanDriverFingerprint();
        if (previousVulkanLaunchNeverPainted()) {
            store(false);
            return;
        }

        QFile cache(m_dir + QStringLiteral("/renderer"));
        const QList<QByteArray> fields = cache.open(QIODevice::ReadOnly)
            ? cache.readAll().trimmed().split(' ') : QList<QByteArray>();
        if (fields.size() != 2 || fields.first() != m_fingerprint) {
            m_probeAfterFirstFrame = true;
            return;
        }
        if (fields.last() != "vulkan")
            return;

        QDir().mkpath(m_dir);
        QFile marker(markerPath(QCoreApplication::applicationPid()));
        if (!marker.open(QIODevice::WriteOnly))
            return; // without the safety net, stay on OpenGL
        marker.close();
        m_vulkan = true;
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#endif
    }

    void firstFramePainted()
    {
        if (m_vulkan)
            QFile::remove(markerPath(QCoreApplication::applicationPid()));
        if (m_probeAfterFirstFrame) {
            m_probeAfterFirstFrame = false;
            QThreadPool::globalInstance()->start([dir = m_dir, fingerprint = m_fingerprint] {
                store(dir, fingerprint, hasHardwareVulkanDevice());
            });
        }
    }

private:
    QString markerPath(qint64 pid) const
    {
        return m_dir + QStringLiteral("/renderer.pending.") + QString::number(pid);
    }

    // A marker left by a process that no longer exists: that launch asked for
    // Vulkan and died before its first frame.
    bool previousVulkanLaunchNeverPainted() const
    {
        bool failed = false;
        const QStringList markers = QDir(m_dir).entryList({QStringLiteral("renderer.pending.*")},
                                                          QDir::Files);
        for (const QString &name : markers) {
            const pid_t pid = name.section(QLatin1Char('.'), -1).toInt();
            if (pid > 0 && kill(pid, 0) == 0)
                continue; // still starting up
            QFile::remove(m_dir + QLatin1Char('/') + name);
            failed = true;
        }
        return failed;
    }

    void store(bool vulkan) const { store(m_dir, m_fingerprint, vulkan); }

    static void store(const QString &dir, const QByteArray &fingerprint, bool vulkan)
    {
        QDir().mkpath(dir);
        QSaveFile file(dir + QStringLiteral("/renderer"));
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(fingerprint + (vulkan ? " vulkan\n" : " opengl\n"));
        file.commit();
    }

    QString m_dir;
    QByteArray m_fingerprint;
    bool m_vulkan = false;
    bool m_probeAfterFirstFrame = false;
};

// Printed by --help. Qt's QCommandLineParser would need a constructed
// QCoreApplication, and both --help and --version have to answer before the
// Wayland check below — `hyprfm --help` over SSH should still work.
void printUsage()
{
    printf(
        "HyprFM %s — a Qt6/QML file manager for Wayland\n"
        "\n"
        "Usage:\n"
        "  hyprfm [options] [path]\n"
        "\n"
        "With no path, launching HyprFM while it is already running opens\n"
        "another window. With a path, the running window gains a tab instead,\n"
        "unless --new-window is given.\n"
        "\n"
        "Options:\n"
        "  -n, --new-window   Open a separate window even when a path is given.\n"
        "  -h, --help         Show this help and exit.\n"
        "  -v, --version      Show the version and exit.\n"
        "\n"
        "Environment:\n"
        "  HYPRFM_TIMING=1    Print startup timings to stderr.\n"
        "  HYPRFM_MSAA=2|4    Enable full-window multisampling (costly).\n"
        "\n"
        "Qt options such as -style are accepted and passed through.\n",
        HYPRFM_VERSION);
}

// The platform theme publishes its own UI font once the QPA plugin has
// settled, which happens *after* the first window is created and silently
// overwrites the font set at startup. Anything built before that keeps the
// configured family while everything created later (view delegates, recycled
// rows) gets the platform one, so the window ends up in two fonts. Re-apply
// ours whenever the platform pushes a replacement.
class UiFontGuard : public QObject
{
public:
    UiFontGuard(QGuiApplication *app, std::function<QFont()> desiredFont)
        : QObject(app)
        , m_app(app)
        , m_desiredFont(std::move(desiredFont))
    {
        m_app->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ApplicationFontChange && !m_applying) {
            const QFont wanted = m_desiredFont();
            if (m_app->font().family() != wanted.family()) {
                m_applying = true;
                m_app->setFont(wanted);
                m_applying = false;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QGuiApplication *m_app;
    std::function<QFont()> m_desiredFont;
    bool m_applying = false;
};

const char kExampleTheme[] = R"(# HyprFM theme sample.
#
# Copy this file to "mytheme.toml" in this directory, edit the colours, then put
#     [general]
#     theme = "mytheme"
# in ~/.config/hyprfm/config.toml, or pick it in Settings. Every *.toml here is
# listed there, and a file here shadows the bundled theme of the same name.
#
# Any key you leave out keeps its built-in default.

[colors]
base    = "#1e1e2e"  # file view background
mantle  = "#181825"  # toolbar, dialogs, breadcrumb
crust   = "#11111b"  # deepest layer: title bar, sidebar
surface = "#313244"  # cards, inputs, hovered rows
overlay = "#45475a"  # borders, separators, inactive marks
text    = "#cdd6f4"  # primary text
subtext = "#bac2de"  # secondary text
muted   = "#6c7086"  # icons, disabled text
accent  = "#89b4fa"  # selection, focus ring, links
success = "#a6e3a1"  # completed operations
warning = "#f9e2af"  # warnings
error   = "#f38ba8"  # errors, destructive actions
)";

} // namespace


// org.freedesktop.appearance color-scheme: 0 no preference, 1 dark, 2 light.
// Published by xdg-desktop-portal, which is what desktop shells write to when
// the user flips light/dark, so it is available even when Qt cannot see it.
static bool desktopPrefersLight()
{
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Settings"),
                          QDBusConnection::sessionBus());
    if (portal.isValid()) {
        const QDBusReply<QDBusVariant> reply =
            portal.call(QStringLiteral("Read"), QStringLiteral("org.freedesktop.appearance"),
                        QStringLiteral("color-scheme"));
        if (reply.isValid()) {
            // The portal answers with a variant wrapping a variant, so the
            // inner one has to be unwrapped or the conversion quietly yields 0
            // and this falls through to Qt for no reason.
            bool ok = false;
            QVariant value = reply.value().variant();
            if (value.canConvert<QDBusVariant>())
                value = value.value<QDBusVariant>().variant();
            const uint scheme = value.toUInt(&ok);
            if (ok && scheme != 0)
                return scheme == 2;
        }
    }
    // No portal, or it has no preference: trust Qt when it knows, else assume
    // dark, which is what this defaulted to before the portal was consulted.
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QLatin1StringView a(argv[i]);
        if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        }
        if (a == "-v" || a == "--version") {
            printf("hyprfm %s\n", HYPRFM_VERSION);
            return 0;
        }
    }

    // Suppress noisy warnings:
    //   - qt.qpa.services: harmless portal registration warning on non-sandboxed apps
    //   - qt.svg: Qt's SVG parser complains about unsupported filter elements
    //     (feTurbulence, feColorMatrix, etc.) on every draw when such SVGs
    //     are previewed/thumbnailed, even though the file still renders.
    QLoggingCategory::setFilterRules(
        "qt.qpa.services.warning=false\n"
        "qt.svg.warning=false");

    // Keep the default path fast. Full-window MSAA is expensive on many
    // Wayland/compositor stacks; opt in with HYPRFM_MSAA=2/4 if wanted.
    QSurfaceFormat fmt;
    fmt.setSamples(qMax(0, qEnvironmentVariableIntValue("HYPRFM_MSAA")));
    QSurfaceFormat::setDefaultFormat(fmt);

    // HyprFM is a Wayland-only application (wl-copy clipboard, Hyprland
    // integration, KWin blur effects). Detect a non-Wayland session before
    // Qt tries to load the wayland QPA plugin so users see an actionable
    // message instead of the cryptic "Failed to create wl_display" error.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        const char *session = sessionType.isEmpty() ? "unknown" : sessionType.constData();
        fprintf(stderr,
                "\n"
                "HyprFM: no Wayland display available (XDG_SESSION_TYPE=%s).\n"
                "\n"
                "HyprFM only supports Wayland sessions. Your current session\n"
                "appears to be X11 or does not expose $WAYLAND_DISPLAY.\n"
                "\n"
                "To run HyprFM:\n"
                "  * Log out and pick a Wayland session at the login screen\n"
                "    (e.g. \"Ubuntu on Wayland\", GNOME on Wayland, Hyprland, KDE\n"
                "    Plasma Wayland).\n"
                "  * If running via Flatpak, also grant Wayland socket access:\n"
                "      flatpak override --user --socket=wayland io.github.soyeb_jim285.HyprFM\n"
                "\n",
                session);
        return 1;
    }

    // Extract an optional path argument. We skip flag-style args so Qt's
    // own options (e.g. `-style`, `-qmljsdebugger`) don't get mistaken
    // for a path. Relative paths are resolved against the caller's cwd
    // before the single-instance handoff, so the receiving process sees
    // an absolute path regardless of where the launcher invoked us from.
    //
    // `--new-window` (`-n`) forces a standalone window even when a path is
    // given, i.e. it opts out of the tab handoff described below.
    QString initialOpenPath;
    bool newWindow = false;
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--new-window") || a == QLatin1String("-n")) {
            newWindow = true;
            continue;
        }
        if (a.startsWith('-')) continue;
        if (initialOpenPath.isEmpty())
            initialOpenPath = a;
    }
    if (!initialOpenPath.isEmpty()) {
        QFileInfo fi(initialOpenPath);
        if (fi.exists())
            initialOpenPath = fi.absoluteFilePath();
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("HyprFM");
    app.setOrganizationName("hyprfm");
    app.setDesktopFileName("hyprfm");

    // Startup timing: opt-in via HYPRFM_TIMING=1 so normal runs stay quiet.
    // Prints milliseconds from QGuiApplication construction at each phase.
    const bool timingEnabled = qEnvironmentVariableIntValue("HYPRFM_TIMING") != 0;
    QElapsedTimer startupTimer;
    startupTimer.start();
    auto mark = [&](const char *label) {
        if (timingEnabled)
            qDebug().nospace() << "[startup] " << qSetFieldWidth(6) << startupTimer.elapsed()
                               << qSetFieldWidth(0) << " ms  " << label;
    };
    mark("QGuiApplication ready");

    // Launching HyprFM again opens another independent window, the way every
    // other file manager behaves. The one exception is `hyprfm <path>` while
    // an instance is already running: that forwards the path over a per-uid
    // unix socket so the running window gains a tab, which is what desktop
    // launchers and `xdg-open` rely on. `--new-window` opts out of even that.
    //
    // The process that manages to listen on the socket is the "primary" one:
    // it answers those handoffs and owns the saved session (tabs + geometry).
    // Extra windows are ordinary processes that share nothing with it.
    const QString hyprfmSocketName = QStringLiteral("hyprfm-%1").arg(static_cast<uint>(getuid()));
    QLocalServer *ipcServer = nullptr;
    bool isPrimary = false;
    {
        QLocalSocket probe;
        probe.connectToServer(hyprfmSocketName);
        const bool instanceRunning = probe.waitForConnected(150);

        if (instanceRunning && !newWindow && !initialOpenPath.isEmpty()) {
            QJsonObject msg;
            msg.insert(QStringLiteral("path"), initialOpenPath);
            QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
            payload.append('\n');
            probe.write(payload);
            probe.waitForBytesWritten(500);
            return 0;
        }

        if (!instanceRunning) {
            // Nobody answered, so any socket file left behind is stale and
            // would block listen(). Two instances starting at the exact same
            // moment can both land here; the second simply wins the socket,
            // which costs nothing but the first one's handoff duty.
            QLocalServer::removeServer(hyprfmSocketName);
            ipcServer = new QLocalServer(&app);
            ipcServer->setSocketOptions(QLocalServer::UserAccessOption);
            if (!ipcServer->listen(hyprfmSocketName))
                qWarning() << "HyprFM: single-instance IPC listen failed:" << ipcServer->errorString();
            isPrimary = ipcServer->isListening();
        }
    }

    QQuickStyle::setStyle("Basic");
    RendererChoice renderer;

    // Use native text rendering (FreeType/fontconfig) for crisp fonts matching GTK apps
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    auto resolveUiFont = [&](const QString &preferredFamily) {
        // Resolve the platform UI font first so the app does not depend on
        // theme-local font defaults that may not exist inside a sandbox.
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        if (font.family().isEmpty())
            font = app.font();

        if (!preferredFamily.trimmed().isEmpty())
            font.setFamily(preferredFamily.trimmed());

        font.setHintingPreference(QFont::PreferFullHinting);
        return font;
    };

    // Ensure config directory exists
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                              + "/.config/hyprfm";
    QDir().mkpath(configDir);
    const QString configPath = configDir + "/config.toml";

    auto firstExistingDir = [](const QStringList &paths) {
        for (const QString &path : paths) {
            const QString cleanPath = QDir::cleanPath(path);
            if (QDir(cleanPath).exists())
                return cleanPath;
        }
        return QString();
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dataDir = firstExistingDir({
        QDir(appDir).filePath("../share/hyprfm"),
        QDir(appDir).filePath("../../share/hyprfm"),
        QStringLiteral(HYPRFM_DATA_DIR),
        QStringLiteral(HYPRFM_SOURCE_DIR),
    });

    QStringList themeSearchPaths = {
        QDir(appDir).filePath("../themes"),
        QDir(appDir).filePath("../../themes"),
        QStringLiteral(HYPRFM_DATA_DIR) + "/themes",
        QStringLiteral(HYPRFM_SOURCE_DIR) + "/themes",
    };
    if (!dataDir.isEmpty())
        themeSearchPaths.prepend(QDir(dataDir).filePath("themes"));

    // User themes come first so ~/.config/hyprfm/themes can add to or override
    // the bundled set.
    QStringList themeDirs;
    const QString userThemesDir = configDir + "/themes";
    QDir().mkpath(userThemesDir);
    {
        // Refresh the documented sample on every start, like config.toml.sample,
        // so it always describes the running version's colour keys.
        // The ".sample" suffix keeps it out of the "*.toml" theme picker.
        QFile sample(userThemesDir + "/example.toml.sample");
        if (sample.open(QIODevice::WriteOnly | QIODevice::Text))
            sample.write(kExampleTheme);
    }
    themeDirs.append(QDir::cleanPath(userThemesDir));
    const QString themesDir = firstExistingDir(themeSearchPaths);
    if (!themesDir.isEmpty())
        themeDirs.append(themesDir);
    if (dataDir.isEmpty())
        qWarning() << "HyprFM: unable to locate data directory";
    if (themesDir.isEmpty())
        qWarning() << "HyprFM: unable to locate themes directory";

    // Only used the first time, while config.toml has no "theme": pick the
    // bundled theme that matches the desktop rather than always landing on the
    // dark one. Qt's own colorScheme() is not enough here -- it reports Unknown
    // under platform themes that do not forward the portal setting (qt6ct,
    // Kvantum), which is a common Hyprland setup -- so ask the portal directly
    // and fall back to Qt, then to dark.
    const QString systemDefaultTheme = desktopPrefersLight()
        ? QStringLiteral("catppuccin-latte")
        : QStringLiteral("catppuccin-mocha");

    // Create backend instances
    ConfigManager *config = new ConfigManager(configPath, &app, themeDirs, systemDefaultTheme);
    mark("ConfigManager loaded");
    app.setFont(resolveUiFont(config->fontFamily()));
    new UiFontGuard(&app, [&]() { return resolveUiFont(config->fontFamily()); });
    ThemeLoader *theme = new ThemeLoader(&app);
    theme->loadTheme(config->theme(), themeDirs);
    mark("ThemeLoader loaded");

    TabListModel *tabModel = new TabListModel(&app);
    tabModel->setDefaultViewMode(config->defaultView());
    QObject::connect(config, &ConfigManager::configChanged, tabModel, [=]() {
        tabModel->setDefaultViewMode(config->defaultView());
    });

    // Restore session (tabs + window geometry)
    const QString sessionPath = configDir + "/session.json";
    QJsonObject sessionData;
    if (isPrimary) {
        QFile sf(sessionPath);
        if (sf.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject())
                sessionData = doc.object();
        }
    }
    if (sessionData.contains("tabs"))
        tabModel->restoreSession(sessionData.value("tabs").toArray(),
                                 sessionData.value("activeTab").toInt(0));

    // Session-scoped view state (zoom per view). 0 keeps built-in defaults.
    SessionState *sessionState = new SessionState(&app);
    sessionState->setGridColumns(sessionData.value("gridColumns").toInt());
    sessionState->setRowHeightDetailed(sessionData.value("rowHeightDetailed").toInt());
    sessionState->setRowHeightMiller(sessionData.value("rowHeightMiller").toInt());

    // A secondary window has no session to restore, so point its single tab
    // straight at the requested path instead of opening a second tab later.
    if (!isPrimary && !initialOpenPath.isEmpty()) {
        if (auto *tab = tabModel->activeTab())
            tab->navigateTo(initialOpenPath);
    }

    BookmarkModel *bookmarks = new BookmarkModel(&app);
    bookmarks->setBookmarks(config->bookmarks(), config->bookmarkNames());

    // Persist bookmark changes to config
    QObject::connect(bookmarks, &BookmarkModel::bookmarksChanged, [=]() {
        config->saveBookmarks(bookmarks->paths(), bookmarks->names());
    });

    FileOperations *fileOps = new FileOperations(&app);
    UndoManager *undoManager = new UndoManager(fileOps, &app);
    ClipboardManager *clipboard = new ClipboardManager(&app);
    // DragHelper created after IconProvider below

    const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString initialPrimaryPath = tabModel->activeTab() && !tabModel->activeTab()->currentPath().isEmpty()
        ? tabModel->activeTab()->currentPath()
        : homePath;
    const QString initialSecondaryPath = tabModel->activeTab() && !tabModel->activeTab()->secondaryCurrentPath().isEmpty()
        ? tabModel->activeTab()->secondaryCurrentPath()
        : initialPrimaryPath;
    const bool initialSplitViewEnabled = tabModel->activeTab() && tabModel->activeTab()->splitViewEnabled();

    FileSystemModel *fsModel = new FileSystemModel(&app);
    fsModel->setShowHidden(config->showHidden());
    fsModel->setRootPath(initialPrimaryPath);
    mark("Primary fsModel populated");

    FileSystemModel *splitFsModel = new FileSystemModel(&app);
    splitFsModel->setShowHidden(config->showHidden());
    if (initialSplitViewEnabled)
        splitFsModel->setRootPath(initialSecondaryPath);
    mark("Secondary fsModel populated");

    FileSystemModel *millerParentModel = new FileSystemModel(&app);
    millerParentModel->setShowHidden(config->showHidden());

    FileSystemModel *millerPreviewModel = new FileSystemModel(&app);
    millerPreviewModel->setShowHidden(config->showHidden());

    SearchResultsModel *searchResults = new SearchResultsModel(&app);
    SearchProxyModel *searchProxy = new SearchProxyModel(&app);
    searchProxy->setSourceModel(searchResults);

    SearchResultsModel *splitSearchResults = new SearchResultsModel(&app);
    SearchProxyModel *splitSearchProxy = new SearchProxyModel(&app);
    splitSearchProxy->setSourceModel(splitSearchResults);

    SearchService *searchService = new SearchService(&app);
    searchService->setObjectName("primary");
    searchService->setResultsModel(searchResults);

    SearchService *splitSearchService = new SearchService(&app);
    splitSearchService->setObjectName("secondary");
    splitSearchService->setResultsModel(splitSearchResults);

    PreviewService *previewService = new PreviewService(&app);
    MetadataExtractor *metadataExtractor = new MetadataExtractor(&app);
    previewService->setMetadataExtractor(metadataExtractor);
    DiskUsageService *diskUsageService = new DiskUsageService(&app);
    RemoteAccessService *remoteAccessService = new RemoteAccessService(&app);
    RcloneService *rcloneService = new RcloneService(&app);
    RuntimeFeaturesService *runtimeFeatures = new RuntimeFeaturesService(&app);
    config->setShowWindowControlsDefault(runtimeFeatures->useIntegratedWindowControls());
    GitStatusService *primaryGitService = new GitStatusService(&app);
    GitStatusService *secondaryGitService = new GitStatusService(&app);
    fsModel->setGitStatusService(primaryGitService);
    splitFsModel->setGitStatusService(secondaryGitService);

    // Keep the live UI in sync with persisted config values.
    QObject::connect(config, &ConfigManager::configChanged, [=, &app, &resolveUiFont]() {
        theme->loadTheme(config->theme(), themeDirs);
        bookmarks->setBookmarks(config->bookmarks(), config->bookmarkNames());
        fsModel->setShowHidden(config->showHidden());
        splitFsModel->setShowHidden(config->showHidden());
        millerParentModel->setShowHidden(config->showHidden());
        millerPreviewModel->setShowHidden(config->showHidden());
        app.setFont(resolveUiFont(config->fontFamily()));
    });

    // Connect lastWindowClosed to quit
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, &QGuiApplication::quit);

    // Create RecentFilesModel
    RecentFilesModel *recentFiles = new RecentFilesModel(configDir + "/recents.json", &app);

    // Create DeviceModel
    DeviceModel *devices = new DeviceModel(&app, true);

    // Aggregate runtime tools + compile-time features + DBus services for the
    // in-app MissingDependenciesDialog. Replaces the older hand-rolled
    // `which` loop that only logged to stderr.
    DependencyChecker *dependencies = new DependencyChecker(&app);

    // When the user installs a missing tool and clicks "Re-check", propagate
    // the refresh into feature services so their Q_PROPERTY bindings (e.g.
    // pdfPreviewAvailable) re-evaluate without requiring an app restart.
    QObject::connect(dependencies, &DependencyChecker::dependenciesChanged,
                     previewService, &PreviewService::refreshSupport);
    QObject::connect(dependencies, &DependencyChecker::dependenciesChanged,
                     metadataExtractor, &MetadataExtractor::refreshSupport);

    QQmlApplicationEngine engine;

    // Prefer the installed data layout, but keep source-tree fallbacks for dev builds.
    if (!dataDir.isEmpty()) {
        engine.addImportPath(dataDir);                           // HyprFM module
        engine.addImportPath(QDir(dataDir).filePath("src/qml")); // Quill module
    }
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR));
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR "/src/qml"));
    engine.addImportPath(QStringLiteral(HYPRFM_SOURCE_DIR));
    engine.addImportPath(QStringLiteral(HYPRFM_SOURCE_DIR "/src/qml"));

    // Set icon theme so QIcon::fromTheme() works (e.g. for drag pixmaps)
    QIcon::setThemeName(config->iconTheme());

    // Register image providers (keep pointer to IconProvider for DragHelper)
    auto *iconProvider = new IconProvider(config->iconTheme());
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    engine.addImageProvider("icon", iconProvider);
    engine.addImageProvider("pdfpreview", new PdfPreviewProvider);

    DragHelper *dragHelper = new DragHelper(iconProvider, &app);

    QObject::connect(config, &ConfigManager::configChanged, [=]() {
        QIcon::setThemeName(config->iconTheme());
        iconProvider->setPrimaryTheme(config->iconTheme());
    });

    // Register context properties
    engine.rootContext()->setContextProperty("config", config);
    engine.rootContext()->setContextProperty("theme", theme);
    engine.rootContext()->setContextProperty("tabModel", tabModel);
    engine.rootContext()->setContextProperty("bookmarks", bookmarks);
    engine.rootContext()->setContextProperty("fileOps", fileOps);
    engine.rootContext()->setContextProperty("undoManager", undoManager);
    engine.rootContext()->setContextProperty("clipboard", clipboard);
    engine.rootContext()->setContextProperty("dragHelper", dragHelper);
    engine.rootContext()->setContextProperty("fsModel", fsModel);
    engine.rootContext()->setContextProperty("splitFsModel", splitFsModel);
    engine.rootContext()->setContextProperty("millerParentModel", millerParentModel);
    engine.rootContext()->setContextProperty("millerPreviewModel", millerPreviewModel);
    engine.rootContext()->setContextProperty("devices", devices);
    engine.rootContext()->setContextProperty("recentFiles", recentFiles);
    engine.rootContext()->setContextProperty("searchProxy", searchProxy);
    engine.rootContext()->setContextProperty("searchResults", searchResults);
    engine.rootContext()->setContextProperty("searchService", searchService);
    engine.rootContext()->setContextProperty("splitSearchProxy", splitSearchProxy);
    engine.rootContext()->setContextProperty("splitSearchResults", splitSearchResults);
    engine.rootContext()->setContextProperty("splitSearchService", splitSearchService);
    engine.rootContext()->setContextProperty("previewService", previewService);
    engine.rootContext()->setContextProperty("metadataExtractor", metadataExtractor);
    engine.rootContext()->setContextProperty("diskUsageService", diskUsageService);
    engine.rootContext()->setContextProperty("remoteAccessService", remoteAccessService);
    engine.rootContext()->setContextProperty("rcloneService", rcloneService);
    engine.rootContext()->setContextProperty("runtimeFeatures", runtimeFeatures);
    engine.rootContext()->setContextProperty("dependencies", dependencies);
    engine.rootContext()->setContextProperty("sessionState", sessionState);

    const QString installedMainQml = dataDir.isEmpty()
        ? QString()
        : QDir(dataDir).filePath(QStringLiteral("HyprFM/qml/Main.qml"));

    // The qrc module is qmlcachegen-compiled, so loading it skips parsing
    // ~60 QML files on every launch. The installed on-disk copy is only the
    // fallback for a qrc payload that turns out incomplete (Qt 6.7.3 built
    // with NO_CACHEGEN dropped SettingsPanel.qml from it in v0.4.14).
    mark("engine.load start");
    engine.loadFromModule("HyprFM", "Main");
    if (engine.rootObjects().isEmpty() && !installedMainQml.isEmpty()
        && QFile::exists(installedMainQml)) {
        qWarning() << "HyprFM: embedded QML module failed to load, falling back to" << installedMainQml;
        engine.load(QUrl::fromLocalFile(installedMainQml));
    }
    mark("engine.load done");

    if (engine.rootObjects().isEmpty())
        return -1;

    // First-frame checkpoint: one-shot hook on the root window's
    // frameSwapped signal, for the timing log and the renderer choice.
    // SingleShotConnection disconnects at the first emission, on the render
    // thread. Disconnecting from inside the queued slot instead was too late:
    // frames swapped before the first delivery each queued another call,
    // which then ran against the deleted connection.
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        QObject::connect(win, &QQuickWindow::frameSwapped, win, [mark, &renderer]() {
            mark("first frame swapped");
            renderer.firstFramePainted();
        }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    }

    auto applyWindowEffects = [config](QQuickWindow *window) {
        if (!window)
            return;

#ifdef HYPRFM_HAS_KWINDOWSYSTEM
        // KWin blur only shows through translucent content; Hyprland keeps
        // using compositor rules against the same transparent window surface.
        const bool blurRequested = config->transparencyEnabled();
        const bool blurAvailable = KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
        KWindowEffects::enableBlurBehind(window, blurRequested && blurAvailable);

        const bool contrastAvailable = KWindowEffects::isEffectAvailable(KWindowEffects::BackgroundContrast);
        KWindowEffects::enableBackgroundContrast(window, blurRequested && contrastAvailable);
#else
        Q_UNUSED(window)
#endif
    };

    QTimer sessionSaveTimer;
    sessionSaveTimer.setSingleShot(true);
    sessionSaveTimer.setInterval(250);

    auto saveSession = [&]() {
        // Secondary windows share no state with the primary instance, so they
        // must not overwrite its saved tabs/geometry.
        if (!isPrimary)
            return;

        QJsonObject session;
        session["tabs"] = tabModel->saveSession();
        session["activeTab"] = tabModel->activeIndex();
        session["gridColumns"] = sessionState->gridColumns();
        session["rowHeightDetailed"] = sessionState->rowHeightDetailed();
        session["rowHeightMiller"] = sessionState->rowHeightMiller();

        if (auto *win = !engine.rootObjects().isEmpty()
                ? qobject_cast<QQuickWindow *>(engine.rootObjects().first())
                : nullptr) {
            session["windowX"] = win->x();
            session["windowY"] = win->y();
            session["windowWidth"] = win->width();
            session["windowHeight"] = win->height();

            QWindow::Visibility savedVisibility = win->visibility();
            if (savedVisibility == QWindow::Hidden
                    || savedVisibility == QWindow::AutomaticVisibility
                    || savedVisibility == QWindow::Minimized) {
                savedVisibility = QWindow::Windowed;
            }
            session["windowVisibility"] = static_cast<int>(savedVisibility);
        }

        QSaveFile sf(sessionPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(session).toJson(QJsonDocument::Compact));
            sf.commit();
        }
    };

    auto scheduleSessionSave = [&]() {
        sessionSaveTimer.start();
    };

    QObject::connect(&sessionSaveTimer, &QTimer::timeout, &app, saveSession);
    QObject::connect(tabModel, &TabListModel::sessionChanged, &app, scheduleSessionSave);
    // Zoom changes are session state too, so persist them the same way.
    QObject::connect(sessionState, &SessionState::gridColumnsChanged, &app, scheduleSessionSave);
    QObject::connect(sessionState, &SessionState::rowHeightDetailedChanged, &app, scheduleSessionSave);
    QObject::connect(sessionState, &SessionState::rowHeightMillerChanged, &app, scheduleSessionSave);

    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        applyWindowEffects(win);
        QObject::connect(config, &ConfigManager::configChanged, win, [=]() {
            applyWindowEffects(win);
        });
        QObject::connect(win, &QQuickWindow::xChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::yChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::widthChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::heightChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::visibilityChanged, &app, scheduleSessionSave);
    }

    // Save session on quit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        sessionSaveTimer.stop();
        saveSession();
    });

    // Restore window geometry
    if (sessionData.contains("windowWidth") && !engine.rootObjects().isEmpty()) {
        if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            win->setX(sessionData.value("windowX").toInt());
            win->setY(sessionData.value("windowY").toInt());
            win->setWidth(sessionData.value("windowWidth").toInt());
            win->setHeight(sessionData.value("windowHeight").toInt());

            QWindow::Visibility restoredVisibility = QWindow::Windowed;
            if (sessionData.contains("windowVisibility")) {
                restoredVisibility = static_cast<QWindow::Visibility>(
                    sessionData.value("windowVisibility").toInt());
            }

            if (restoredVisibility == QWindow::Maximized
                    || restoredVisibility == QWindow::FullScreen
                    || restoredVisibility == QWindow::Windowed) {
                win->setVisibility(restoredVisibility);
            } else {
                win->showNormal();
            }
        }
    }

    // Raise, focus, and navigate to a path — used for both the initial
    // argv path and for paths forwarded by a subsequent invocation over
    // the single-instance socket. Empty path just raises the window.
    auto openPathInNewTab = [&engine, tabModel](const QString &path) {
        if (!path.isEmpty())
            tabModel->openPath(path);   // reuses a tab already showing it
        if (engine.rootObjects().isEmpty())
            return;
        if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            if (win->visibility() == QWindow::Minimized || win->visibility() == QWindow::Hidden)
                win->showNormal();
            win->raise();
            win->requestActivate();
        }
    };

    // The socket was opened before the session load; now that the window and
    // the navigation helper exist, start answering handoffs on it.
    if (isPrimary) {
        QObject::connect(ipcServer, &QLocalServer::newConnection, &app, [ipcServer, openPathInNewTab]() {
            while (QLocalSocket *conn = ipcServer->nextPendingConnection()) {
                QObject::connect(conn, &QLocalSocket::readyRead, conn, [conn, openPathInNewTab]() {
                    const QByteArray data = conn->readAll();
                    for (const QByteArray &line : data.split('\n')) {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.isEmpty()) continue;
                        QJsonParseError err;
                        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
                        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
                        openPathInNewTab(doc.object().value(QStringLiteral("path")).toString());
                    }
                });
                QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
            }
        });
    }

    // Apply the path this process was launched with (if any) as a new tab
    // on the restored session. Secondary windows already navigated their
    // single tab above.
    if (!initialOpenPath.isEmpty() && isPrimary)
        QTimer::singleShot(0, &app, [=]() { openPathInNewTab(initialOpenPath); });

    return app.exec();
}
