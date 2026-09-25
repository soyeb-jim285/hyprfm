#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QFont>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QFontDatabase>
#include <QDBusArgument>
#include <QDBusMessage>
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
#include <thread>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <signal.h>
#include <QCryptographicHash>
#include <QThreadPool>
#include <QProcess>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(QuillPlugin)

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

// Bringing up Vulkan costs ~47 ms between the QML tree being ready and the
// scene graph being initialised, and Qt does it on the GUI thread once the
// window is shown. Loading the loader + ICD and creating a throwaway instance
// on a worker while QML is still being built overlaps most of that with work
// already happening: 47 -> 34 ms, ~8 ms off the window appearing.
void warmVulkanDriver()
{
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return;
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
    auto getProc = reinterpret_cast<GetProc>(dlsym(lib, "vkGetInstanceProcAddr"));
    auto create = getProc ? reinterpret_cast<Create>(getProc(nullptr, "vkCreateInstance")) : nullptr;
    const AppInfo app{0, nullptr, "hyprfm", 0, nullptr, 0, (1u << 22) | (1u << 12)};
    const CreateInfo info{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
    Instance instance = nullptr;
    if (create && create(&info, nullptr, &instance) == 0) {
        uint32_t count = 0;
        if (auto enumerate = reinterpret_cast<Enumerate>(getProc(instance, "vkEnumeratePhysicalDevices")))
            enumerate(instance, &count, nullptr);
        if (auto destroy = reinterpret_cast<Destroy>(getProc(instance, "vkDestroyInstance")))
            destroy(instance, nullptr);
    }
    // No dlclose: keeping the driver loaded is the point.
}

// The driver manifests in every place the Vulkan loader looks for them.
QFileInfoList vulkanDriverManifests()
{
    const auto dirsFrom = [](const char *var, const QString &fallback) {
        const QString value = qEnvironmentVariable(var);
        return (value.isEmpty() ? fallback : value).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    };
    QStringList roots = dirsFrom("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg"));
    roots << QStringLiteral("/etc");
    roots << dirsFrom("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share"));
    roots << dirsFrom("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    QFileInfoList manifests;
    for (const QString &root : std::as_const(roots))
        manifests += QDir(root + QStringLiteral("/vulkan/icd.d")).entryInfoList(QDir::Files, QDir::Name);
    return manifests;
}

// Variables through which the user already chose the Vulkan drivers.
constexpr const char *kVulkanDriverVars[] = {
    "VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES", "VK_LOADER_DRIVERS_SELECT"};

// Each manifest with its mtime: installing, removing or upgrading a driver
// changes it.
QByteArray vulkanDriverFingerprint()
{
    QByteArray data;
    for (const char *var : kVulkanDriverVars)
        data += qgetenv(var) + '|';
    for (const QFileInfo &manifest : vulkanDriverManifests())
        data += manifest.absoluteFilePath().toUtf8() + ':'
            + QByteArray::number(manifest.lastModified().toMSecsSinceEpoch()) + '|';
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex();
}

// The Vulkan loader opens every installed driver when an instance is
// created, whether or not it has hardware to drive: with the NVIDIA driver
// installed beside RADV and no NVIDIA card active, instance creation took
// 46 ms against 19 ms for RADV alone. When exactly one manifest drives real
// hardware, launches name it in VK_DRIVER_FILES. Each manifest is tried in a
// child process (hyprfm --vulkan-probe with only that driver visible), so
// this process never changes its environment while threads are running.
QString soleHardwareVulkanDriver()
{
    for (const char *var : kVulkanDriverVars)
        if (!qEnvironmentVariableIsEmpty(var))
            return {};
    QString sole;
    for (const QFileInfo &manifest : vulkanDriverManifests()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("VK_DRIVER_FILES"), manifest.absoluteFilePath());
        QProcess probe;
        probe.setProcessEnvironment(env);
        probe.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--vulkan-probe")});
        if (!probe.waitForFinished(5000)) {
            probe.kill();
            probe.waitForFinished();
            return {};
        }
        if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0)
            continue;
        if (!sole.isEmpty())
            return {}; // several drivers with hardware: leave the choice to the loader
        sole = manifest.absoluteFilePath();
    }
    return sole;
}

// A GPU driver (Mesa, NVIDIA's DRM module, virtio-gpu) exposes a render node.
// Without one there is no hardware to render with: Qt's OpenGL path would
// fall to llvmpipe, Mesa's software rasteriser, which measured 332 ms to the
// window, 320 MB PSS and 2.0 s of CPU at startup, against 194 ms, 73 MB and
// 0.3 s for Qt Quick's own software renderer. HyprFM uses no shader effects,
// so it renders the same.
bool hasGpuRenderNode()
{
    return !QDir(QStringLiteral("/dev/dri"))
                .entryList({QStringLiteral("renderD*")}, QDir::System)
                .isEmpty();
}

class RendererChoice
{
public:
    RendererChoice()
        : m_dir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                + QStringLiteral("/hyprfm"))
    {
        if (!qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")
            || !qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
            return;
        if (!hasGpuRenderNode()) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
            return;
        }
#if QT_CONFIG(vulkan)
        m_fingerprint = vulkanDriverFingerprint();
        if (previousVulkanLaunchNeverPainted()) {
            store(false);
            return;
        }

        // "<fingerprint> vulkan|opengl [driver manifest]"
        QFile cache(m_dir + QStringLiteral("/renderer"));
        const QList<QByteArray> fields = cache.open(QIODevice::ReadOnly)
            ? cache.readAll().trimmed().split(' ') : QList<QByteArray>();
        if (fields.size() < 2 || fields.first() != m_fingerprint) {
            m_probeAfterFirstFrame = true;
            return;
        }
        if (fields.at(1) != "vulkan")
            return;

        QDir().mkpath(m_dir);
        QFile marker(markerPath(QCoreApplication::applicationPid()));
        if (!marker.open(QIODevice::WriteOnly))
            return; // without the safety net, stay on OpenGL
        marker.close();
        m_vulkan = true;
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        // Safe here: the renderer is chosen before QGuiApplication exists,
        // while this is the only thread. Taken back after the first frame so
        // that nothing launched from the file manager inherits it.
        if (fields.size() == 3 && QFileInfo::exists(QString::fromUtf8(fields.at(2)))) {
            qputenv("VK_DRIVER_FILES", fields.at(2));
            m_pinnedDriver = true;
        }
#endif
    }

    // Only a launch that dies (a driver crash) or never paints and gets
    // killed should count against Vulkan. Anything that returns from main
    // without a frame -- a QML load error, say -- is not Vulkan's fault.
    ~RendererChoice()
    {
        if (m_vulkan)
            QFile::remove(markerPath(QCoreApplication::applicationPid()));
    }

    bool usesVulkan() const { return m_vulkan; }

    void firstFramePainted()
    {
        if (m_vulkan)
            QFile::remove(markerPath(QCoreApplication::applicationPid()));
        if (m_pinnedDriver) {
            // The instance exists by now, and later windows share it.
            qunsetenv("VK_DRIVER_FILES");
            m_pinnedDriver = false;
        }
        if (m_probeAfterFirstFrame) {
            m_probeAfterFirstFrame = false;
            QThreadPool::globalInstance()->start([dir = m_dir, fingerprint = m_fingerprint] {
                const bool vulkan = hasHardwareVulkanDevice();
                store(dir, fingerprint, vulkan, vulkan ? soleHardwareVulkanDriver() : QString());
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

    void store(bool vulkan) const { store(m_dir, m_fingerprint, vulkan, {}); }

    static void store(const QString &dir, const QByteArray &fingerprint, bool vulkan,
                      const QString &driver)
    {
        QDir().mkpath(dir);
        QSaveFile file(dir + QStringLiteral("/renderer"));
        if (!file.open(QIODevice::WriteOnly))
            return;
        QByteArray line = fingerprint + (vulkan ? " vulkan" : " opengl");
        if (!driver.isEmpty() && !driver.contains(QLatin1Char(' ')))
            line += ' ' + driver.toUtf8();
        file.write(line + '\n');
        file.commit();
    }

    QString m_dir;
    QByteArray m_fingerprint;
    bool m_vulkan = false;
    bool m_pinnedDriver = false;
    bool m_probeAfterFirstFrame = false;
};

// What one main window owns. Everything the windows share (config, file
// operations, clipboard, devices, ...) lives on the engine's root context.
struct AppWindow : public QObject
{
    using QObject::QObject;
    TabListModel *tabModel = nullptr;
    SessionState *sessionState = nullptr;
    QQuickWindow *window = nullptr;
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

// What the desktop itself is set to, read the way every Wayland toolkit reads
// it: the XDG portal's Settings interface. Used only for what the user has not
// configured - an absent portal simply leaves the built-in defaults in place.
struct DesktopSettings
{
    QString iconTheme;
    QString fontFamily;
    qreal fontPointSize = 0;
    QString fontHinting;   // none / slight / medium / full, as the desktop sets it
};

DesktopSettings readDesktopSettings()
{
    DesktopSettings settings;
    if (!QDBusConnection::sessionBus().isConnected())
        return settings;

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Settings"),
                          QDBusConnection::sessionBus());
    if (!portal.isValid())
        return settings;
    // A desktop that is slow to answer must not hold up the window.
    portal.setTimeout(200);

    const QString ns = QStringLiteral("org.gnome.desktop.interface");

    // One ReadAll instead of a round trip per key: each call is ~1.5 ms and
    // this sits on the way to the first frame.
    QVariantMap values;
    QDBusMessage all = portal.call(QStringLiteral("ReadAll"), QStringList{ns});
    if (all.type() == QDBusMessage::ReplyMessage && !all.arguments().isEmpty()) {
        const QDBusArgument arg = all.arguments().constFirst().value<QDBusArgument>();
        QMap<QString, QVariantMap> namespaces;
        arg >> namespaces;
        values = namespaces.value(ns);
    }

    const auto readKey = [&](const QString &key) -> QString {
        const auto it = values.constFind(key);
        if (it != values.constEnd()) {
            const QVariant value = it.value();
            return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant().toString()
                                                    : value.toString();
        }
        // Portals without ReadAll (or namespaces they do not carry).
        QDBusReply<QDBusVariant> one = portal.call(QStringLiteral("ReadOne"), ns, key);
        if (one.isValid())
            return one.value().variant().toString();
        return {};
    };

    settings.iconTheme = readKey(QStringLiteral("icon-theme")).trimmed();
    settings.fontHinting = readKey(QStringLiteral("font-hinting")).trimmed();

    // font-name is Pango's format: family, optional style words, then the size
    // ("Adwaita Sans 11", "Cantarell Bold 12").
    const QString fontName = readKey(QStringLiteral("font-name")).trimmed();
    if (!fontName.isEmpty()) {
        const qsizetype lastSpace = fontName.lastIndexOf(QLatin1Char(' '));
        bool isNumber = false;
        const qreal size = lastSpace > 0 ? fontName.mid(lastSpace + 1).toDouble(&isNumber) : 0;
        if (isNumber && size > 0) {
            settings.fontFamily = fontName.left(lastSpace).trimmed();
            settings.fontPointSize = size;
        } else {
            settings.fontFamily = fontName;
        }
    }
    return settings;
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
            const QFont current = m_app->font();
            if (current.family() != wanted.family()
                || !qFuzzyCompare(current.pointSizeF(), wanted.pointSizeF())) {
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
        // Internal: run by soleHardwareVulkanDriver() with one driver visible.
        if (a == "--vulkan-probe")
            return hasHardwareVulkanDevice() ? 0 : 1;
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

    // Before QGuiApplication: it may set VK_DRIVER_FILES, which is only safe
    // while this is the only thread.
    RendererChoice renderer;

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

#if QT_CONFIG(vulkan)
    if (renderer.usesVulkan())
        std::thread(warmVulkanDriver).detach();
#endif

    // The first string any Qt process measures costs ~7 ms: fontconfig matches
    // the family, FreeType loads the face, and the shaper initialises. That
    // used to land on whichever Text item QML built first (the status bar's
    // disk label, as it happened). The font database and FreeType are shared
    // across threads under their own locks - only QFontCache is per-thread -
    // so doing it on a worker leaves the GUI thread ~1 ms of engine setup.
    std::thread([] {
        QFontMetricsF(QFont()).horizontalAdvance(QStringLiteral("0123456789 GB free of"));
    }).detach();

    // One process serves every window. Launching HyprFM while it runs hands
    // the request to it over a per-uid unix socket: `hyprfm <path>` adds a tab
    // to the window last used (what desktop launchers and `xdg-open` rely on),
    // and a bare launch or `--new-window` opens another window.
    //
    // The process that manages to listen on the socket is the "primary" one:
    // it answers those handoffs and owns the saved session (tabs + geometry)
    // of its first window.
    const QString hyprfmSocketName = QStringLiteral("hyprfm-%1").arg(static_cast<uint>(getuid()));
    QLocalServer *ipcServer = nullptr;
    bool isPrimary = false;
    {
        QLocalSocket probe;
        probe.connectToServer(hyprfmSocketName);
        const bool instanceRunning = probe.waitForConnected(150);

        if (instanceRunning) {
            // A path opens as a tab in the running instance; a bare launch or
            // --new-window asks it for another window. Either way this process
            // is done. If the handoff cannot be written, fall through and run
            // as a window of its own, as every launch used to.
            QJsonObject msg;
            msg.insert(QStringLiteral("path"), initialOpenPath);
            if (newWindow || initialOpenPath.isEmpty())
                msg.insert(QStringLiteral("newWindow"), true);
            QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
            payload.append('\n');
            probe.write(payload);
            if (probe.waitForBytesWritten(500))
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

    // Use native text rendering (FreeType/fontconfig) for crisp fonts matching GTK apps
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    const DesktopSettings desktop = readDesktopSettings();

    auto resolveUiFont = [&](const QString &preferredFamily) {
        // Resolve the platform UI font first so the app does not depend on
        // theme-local font defaults that may not exist inside a sandbox.
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        if (font.family().isEmpty())
            font = app.font();

        if (!preferredFamily.trimmed().isEmpty()) {
            font.setFamily(preferredFamily.trimmed());
        } else if (!desktop.fontFamily.isEmpty()) {
            // Nothing configured: look the same as the rest of the desktop,
            // size included - Theme.qml scales the whole UI from it.
            font.setFamily(desktop.fontFamily);
            if (desktop.fontPointSize > 0)
                font.setPointSizeF(desktop.fontPointSize);
        }

        // Hinting is the desktop's call, not ours: forcing full hinting while
        // the desktop asks for slight snapped stems to the pixel grid and made
        // the same font look heavier here than in every GTK window next to it.
        // fontconfig decides when the desktop says nothing.
        if (desktop.fontHinting == QLatin1String("none"))
            font.setHintingPreference(QFont::PreferNoHinting);
        else if (desktop.fontHinting == QLatin1String("slight"))
            font.setHintingPreference(QFont::PreferVerticalHinting);
        else if (desktop.fontHinting == QLatin1String("medium")
                 || desktop.fontHinting == QLatin1String("full"))
            font.setHintingPreference(QFont::PreferFullHinting);
        else
            font.setHintingPreference(QFont::PreferDefaultHinting);
        return font;
    };

    // $XDG_CONFIG_HOME/hyprfm (~/.config/hyprfm when unset). Not under
    // Flatpak: the runtime points XDG_CONFIG_HOME at a per-app directory, and
    // Flatpak installs have always kept their config in the real
    // ~/.config/hyprfm, so following it there would lose it.
    const bool inFlatpak = QFile::exists(QStringLiteral("/.flatpak-info"));
    const QString configDir = (inFlatpak
            ? QDir::homePath() + QStringLiteral("/.config")
            : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        + QStringLiteral("/hyprfm");
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
    if (timingEnabled) {
        const QFont uiFont = app.font();
        qDebug().nospace() << "[startup] ui font " << uiFont.family() << ' '
                           << uiFont.pointSizeF() << "pt (desktop: " << desktop.fontFamily
                           << ' ' << desktop.fontPointSize << ", icons: " << config->iconTheme() << ')';
    }
    ThemeLoader *theme = new ThemeLoader(&app);
    theme->loadTheme(config->theme(), themeDirs);
    mark("ThemeLoader loaded");

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

    // ── Shared services: one per process, whatever the number of windows ──
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

    PreviewService *previewService = new PreviewService(&app);
    MetadataExtractor *metadataExtractor = new MetadataExtractor(&app);
    previewService->setMetadataExtractor(metadataExtractor);
    DiskUsageService *diskUsageService = new DiskUsageService(&app);
    RemoteAccessService *remoteAccessService = new RemoteAccessService(&app);
    RcloneService *rcloneService = new RcloneService(&app);
    RuntimeFeaturesService *runtimeFeatures = new RuntimeFeaturesService(&app);
    config->setShowWindowControlsDefault(runtimeFeatures->useIntegratedWindowControls());

    // Keep the live UI in sync with persisted config values.
    QObject::connect(config, &ConfigManager::configChanged, [=, &app, &resolveUiFont]() {
        theme->loadTheme(config->theme(), themeDirs);
        bookmarks->setBookmarks(config->bookmarks(), config->bookmarkNames());
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

    // HyprFM and Quill are both compiled in. These paths only serve the
    // on-disk fallback copy of the HyprFM module (see below). No source-tree
    // path: an installed binary preferred it over its own install whenever
    // the build tree it was built in still existed.
    if (!dataDir.isEmpty())
        engine.addImportPath(dataDir);
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR));

    // An unset icon theme follows the desktop; Adwaita is the last resort when
    // nothing answers. Resolved inside ConfigManager so QML's config.iconTheme
    // (the ?theme= in every image://icon URL) sees the same answer.
    config->setIconThemeFallback(desktop.iconTheme.isEmpty() ? QStringLiteral("Adwaita")
                                                             : desktop.iconTheme);

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

    QQmlContext *shared = engine.rootContext();
    shared->setContextProperty("config", config);
    shared->setContextProperty("theme", theme);
    shared->setContextProperty("bookmarks", bookmarks);
    shared->setContextProperty("fileOps", fileOps);
    shared->setContextProperty("undoManager", undoManager);
    shared->setContextProperty("clipboard", clipboard);
    shared->setContextProperty("dragHelper", dragHelper);
    shared->setContextProperty("devices", devices);
    shared->setContextProperty("recentFiles", recentFiles);
    shared->setContextProperty("previewService", previewService);
    shared->setContextProperty("metadataExtractor", metadataExtractor);
    shared->setContextProperty("diskUsageService", diskUsageService);
    shared->setContextProperty("remoteAccessService", remoteAccessService);
    shared->setContextProperty("rcloneService", rcloneService);
    shared->setContextProperty("runtimeFeatures", runtimeFeatures);
    shared->setContextProperty("dependencies", dependencies);

    // The main window, compiled once and instantiated per window. The qrc
    // module is qmlcachegen-compiled; the installed on-disk copy is only the
    // fallback for a qrc payload that turns out incomplete (Qt 6.7.3 built
    // with NO_CACHEGEN dropped SettingsPanel.qml from it in v0.4.14).
    mark("engine.load start");
    QQmlComponent mainComponent(&engine);
    mainComponent.loadFromModule("HyprFM", "Main");
    const QString installedMainQml = dataDir.isEmpty()
        ? QString()
        : QDir(dataDir).filePath(QStringLiteral("HyprFM/qml/Main.qml"));
    if (mainComponent.isError() && !installedMainQml.isEmpty() && QFile::exists(installedMainQml)) {
        qWarning() << "HyprFM: embedded QML module failed to load, falling back to" << installedMainQml
                   << mainComponent.errorString();
        mainComponent.loadUrl(QUrl::fromLocalFile(installedMainQml));
    }
    if (mainComponent.isError()) {
        qWarning().noquote() << mainComponent.errorString();
        return -1;
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

    // ── Windows ───────────────────────────────────────────────────────────
    // Every window runs in this one process. Launching HyprFM again while it
    // runs asks it for another window over the socket instead of starting a
    // second process: the compiled QML, the shared services, the GPU device
    // and the libraries are already there, which made a second window
    // measurably cheaper to open (see the commit). What each window owns --
    // its tabs, its listings, its searches, its zoom -- lives in a child
    // context, so Main.qml sees the same names it always did.
    QList<AppWindow *> windows;
    AppWindow *sessionWindow = nullptr;   // the one whose state is session.json
    AppWindow *lastActiveWindow = nullptr;

    auto createWindow = [&](const QJsonObject &session, const QString &openPath) -> AppWindow * {
        auto *w = new AppWindow(&app);

        w->tabModel = new TabListModel(w);
        w->tabModel->setDefaultViewMode(config->defaultView());
        QObject::connect(config, &ConfigManager::configChanged, w->tabModel, [config, tabModel = w->tabModel]() {
            tabModel->setDefaultViewMode(config->defaultView());
        });
        if (session.contains("tabs")) {
            w->tabModel->restoreSession(session.value("tabs").toArray(), session.value("activeTab").toInt(0));
        } else if (!openPath.isEmpty()) {
            // A window with no session starts on the requested path in its
            // one tab rather than opening a second tab for it later.
            if (auto *tab = w->tabModel->activeTab())
                tab->navigateTo(openPath);
        }

        // Session-scoped view state (zoom per view). 0 keeps built-in defaults.
        w->sessionState = new SessionState(w);
        w->sessionState->setGridColumns(session.value("gridColumns").toInt());
        w->sessionState->setRowHeightDetailed(session.value("rowHeightDetailed").toInt());
        w->sessionState->setRowHeightMiller(session.value("rowHeightMiller").toInt());

        const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        TabModel *activeTab = w->tabModel->activeTab();
        const QString initialPrimaryPath = activeTab && !activeTab->currentPath().isEmpty()
            ? activeTab->currentPath() : homePath;
        const QString initialSecondaryPath = activeTab && !activeTab->secondaryCurrentPath().isEmpty()
            ? activeTab->secondaryCurrentPath() : initialPrimaryPath;

        auto *fsModel = new FileSystemModel(w);
        fsModel->setShowHidden(config->showHidden());
        fsModel->setHiddenLast(config->hiddenLast());
        fsModel->setRootPath(initialPrimaryPath);
        auto *splitFsModel = new FileSystemModel(w);
        splitFsModel->setShowHidden(config->showHidden());
        splitFsModel->setHiddenLast(config->hiddenLast());
        if (activeTab && activeTab->splitViewEnabled())
            splitFsModel->setRootPath(initialSecondaryPath);
        auto *millerParentModel = new FileSystemModel(w);
        millerParentModel->setShowHidden(config->showHidden());
        millerParentModel->setHiddenLast(config->hiddenLast());
        auto *millerPreviewModel = new FileSystemModel(w);
        millerPreviewModel->setShowHidden(config->showHidden());
        millerPreviewModel->setHiddenLast(config->hiddenLast());
        mark("fsModels populated");
        QObject::connect(config, &ConfigManager::configChanged, w,
                         [=]() {
            for (FileSystemModel *model : {fsModel, splitFsModel, millerParentModel, millerPreviewModel}) {
                model->setHiddenLast(config->hiddenLast());
                model->setShowHidden(config->showHidden());
            }
        });

        auto *searchResults = new SearchResultsModel(w);
        auto *searchProxy = new SearchProxyModel(w);
        searchProxy->setSourceModel(searchResults);
        auto *splitSearchResults = new SearchResultsModel(w);
        auto *splitSearchProxy = new SearchProxyModel(w);
        splitSearchProxy->setSourceModel(splitSearchResults);
        auto *searchService = new SearchService(w);
        searchService->setObjectName("primary");
        searchService->setResultsModel(searchResults);
        auto *splitSearchService = new SearchService(w);
        splitSearchService->setObjectName("secondary");
        splitSearchService->setResultsModel(splitSearchResults);

        auto *primaryGitService = new GitStatusService(w);
        auto *secondaryGitService = new GitStatusService(w);
        fsModel->setGitStatusService(primaryGitService);
        splitFsModel->setGitStatusService(secondaryGitService);

        auto *context = new QQmlContext(shared, w);
        context->setContextProperty("tabModel", w->tabModel);
        context->setContextProperty("sessionState", w->sessionState);
        context->setContextProperty("fsModel", fsModel);
        context->setContextProperty("splitFsModel", splitFsModel);
        context->setContextProperty("millerParentModel", millerParentModel);
        context->setContextProperty("millerPreviewModel", millerPreviewModel);
        context->setContextProperty("searchProxy", searchProxy);
        context->setContextProperty("searchResults", searchResults);
        context->setContextProperty("searchService", searchService);
        context->setContextProperty("splitSearchProxy", splitSearchProxy);
        context->setContextProperty("splitSearchResults", splitSearchResults);
        context->setContextProperty("splitSearchService", splitSearchService);

        QObject *root = mainComponent.create(context);
        w->window = qobject_cast<QQuickWindow *>(root);
        if (!w->window) {
            qWarning().noquote() << "HyprFM: could not create a window:" << mainComponent.errorString();
            delete root;
            delete w;
            return nullptr;
        }
        root->setParent(w);

        applyWindowEffects(w->window);
        QObject::connect(config, &ConfigManager::configChanged, w->window, [=]() {
            applyWindowEffects(w->window);
        });

        windows.append(w);
        QObject::connect(w, &QObject::destroyed, &app, [&windows, &sessionWindow, &lastActiveWindow, w, &app, &engine]() {
            windows.removeAll(w);
            // A closed window frees a whole window's worth of objects at once,
            // but the JS heap only shrinks on the engine's next collection and
            // glibc keeps what is freed: each open/close cycle left ~13 MB
            // behind, 112 -> 202 MB after eight. Collecting and trimming once
            // things settle keeps it at ~1.5 MB (the collection costs ~30 ms,
            // so not during the close itself).
            QTimer::singleShot(1000, &app, [&engine]() {
                engine.collectGarbage();
#ifdef __GLIBC__
                malloc_trim(0);
#endif
            });
            if (sessionWindow == w) sessionWindow = nullptr;
            if (lastActiveWindow == w) lastActiveWindow = nullptr;
        });
        return w;
    };

    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &app, [&](QWindow *focused) {
        for (AppWindow *w : std::as_const(windows))
            if (w->window == focused)
                lastActiveWindow = w;
    });

    // With tabs to restore, the launch path is added on top of them once the
    // window exists (below). With nothing to restore there is no reason to
    // start on $HOME and navigate afterwards: that listed a directory nobody
    // asked for, reset the model, rebuilt every delegate, and flashed the
    // home folder on screen before the requested one.
    const bool restoringSession = sessionData.contains(QStringLiteral("tabs"));
    AppWindow *first = createWindow(sessionData,
                                    (isPrimary && restoringSession) ? QString() : initialOpenPath);
    mark("engine.load done");
    if (!first)
        return -1;
    if (isPrimary)
        sessionWindow = first;

    // First-frame checkpoint: one-shot hook on the first window's
    // frameSwapped signal, for the timing log and the renderer choice.
    // SingleShotConnection disconnects at the first emission, on the render
    // thread. Disconnecting from inside the queued slot instead was too late:
    // frames swapped before the first delivery each queued another call,
    // which then ran against the deleted connection.
    QObject::connect(first->window, &QQuickWindow::sceneGraphInitialized, first->window,
                     [mark] { mark("scenegraph initialized"); },
                     static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));
    QObject::connect(first->window, &QQuickWindow::beforeRendering, first->window,
                     [mark] { mark("first render begins"); },
                     static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));
    QObject::connect(first->window, &QQuickWindow::afterRendering, first->window,
                     [mark] { mark("first render done"); },
                     static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));
    QObject::connect(first->window, &QQuickWindow::frameSwapped, first->window, [mark, &renderer]() {
        mark("first frame swapped");
        renderer.firstFramePainted();
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

    QTimer sessionSaveTimer;
    sessionSaveTimer.setSingleShot(true);
    sessionSaveTimer.setInterval(250);

    // Only the session window is saved: the others are extra views, as the
    // extra processes they replace were, and must not overwrite its tabs.
    auto saveSession = [&]() {
        if (!sessionWindow)
            return;

        QJsonObject session;
        session["tabs"] = sessionWindow->tabModel->saveSession();
        session["activeTab"] = sessionWindow->tabModel->activeIndex();
        session["gridColumns"] = sessionWindow->sessionState->gridColumns();
        session["rowHeightDetailed"] = sessionWindow->sessionState->rowHeightDetailed();
        session["rowHeightMiller"] = sessionWindow->sessionState->rowHeightMiller();

        QQuickWindow *win = sessionWindow->window;
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

    if (sessionWindow) {
        QObject::connect(sessionWindow->tabModel, &TabListModel::sessionChanged, &app, scheduleSessionSave);
        // Zoom changes are session state too, so persist them the same way.
        QObject::connect(sessionWindow->sessionState, &SessionState::gridColumnsChanged, &app, scheduleSessionSave);
        QObject::connect(sessionWindow->sessionState, &SessionState::rowHeightDetailedChanged, &app, scheduleSessionSave);
        QObject::connect(sessionWindow->sessionState, &SessionState::rowHeightMillerChanged, &app, scheduleSessionSave);
        QQuickWindow *win = sessionWindow->window;
        QObject::connect(win, &QQuickWindow::xChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::yChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::widthChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::heightChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::visibilityChanged, &app, scheduleSessionSave);
    }

    // A window's models go with it when it closes. The session window saves
    // first; closing it while others are open leaves them running unsaved,
    // like the extra processes did.
    auto releaseOnClose = [&](AppWindow *w) {
        QObject::connect(w->window, &QQuickWindow::closing, w, [&, w]() {
            if (w == sessionWindow) {
                sessionSaveTimer.stop();
                saveSession();
                sessionWindow = nullptr;
            }
            w->deleteLater();
        });
    };
    releaseOnClose(first);

    // Save session on quit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        sessionSaveTimer.stop();
        saveSession();
    });

    // Restore window geometry
    if (sessionWindow && sessionData.contains("windowWidth")) {
        QQuickWindow *win = sessionWindow->window;
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

    // Raise, focus, and open a path as a tab in the window last used -- for
    // the initial argv path and for paths handed over by a later invocation.
    // Empty path just raises the window.
    auto openPathInNewTab = [&](const QString &path) {
        AppWindow *target = lastActiveWindow ? lastActiveWindow
            : sessionWindow ? sessionWindow
            : windows.isEmpty() ? nullptr : windows.constLast();
        if (!target)
            return;
        if (!path.isEmpty())
            target->tabModel->openPath(path);   // reuses a tab already showing it
        QQuickWindow *win = target->window;
        if (win->visibility() == QWindow::Minimized || win->visibility() == QWindow::Hidden)
            win->showNormal();
        win->raise();
        win->requestActivate();
    };

    auto openNewWindow = [&](const QString &path) {
        if (AppWindow *w = createWindow(QJsonObject(), path)) {
            releaseOnClose(w);
            w->window->raise();
            w->window->requestActivate();
        }
    };

    // The socket was opened before the session load; now that the windows and
    // the navigation helpers exist, start answering handoffs on it.
    if (isPrimary) {
        QObject::connect(ipcServer, &QLocalServer::newConnection, &app, [&]() {
            while (QLocalSocket *conn = ipcServer->nextPendingConnection()) {
                QObject::connect(conn, &QLocalSocket::readyRead, conn, [&, conn]() {
                    const QByteArray data = conn->readAll();
                    for (const QByteArray &line : data.split('\n')) {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.isEmpty()) continue;
                        QJsonParseError err;
                        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
                        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
                        const QString path = doc.object().value(QStringLiteral("path")).toString();
                        if (doc.object().value(QStringLiteral("newWindow")).toBool())
                            openNewWindow(path);
                        else
                            openPathInNewTab(path);
                    }
                });
                QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
            }
        });
    }

    // Apply the path this process was launched with (if any) as a new tab on
    // the restored session. A window without a session already opened on it.
    if (!initialOpenPath.isEmpty() && isPrimary && restoringSession)
        QTimer::singleShot(0, &app, [&]() { openPathInNewTab(initialOpenPath); });

    return app.exec();
}
