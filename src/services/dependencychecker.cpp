#include "services/dependencychecker.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Per-distro install command templates. Placeholder {pkg} is substituted
// with the package name chosen for each dependency. Keeping the list
// small on purpose — anything not matched falls through to the generic
// "install {pkg}" hint.
struct DistroTemplate {
    const char *id;           // matches ID / ID_LIKE from /etc/os-release
    const char *displayName;
    const char *installCmd;   // format string, {pkg} will be replaced
};

static const DistroTemplate kDistros[] = {
    {"arch",     "Arch Linux / Manjaro", "sudo pacman -S {pkg}"},
    {"manjaro",  "Manjaro",              "sudo pacman -S {pkg}"},
    {"endeavouros", "EndeavourOS",       "sudo pacman -S {pkg}"},
    {"cachyos",  "CachyOS",              "sudo pacman -S {pkg}"},
    {"debian",   "Debian",               "sudo apt install {pkg}"},
    {"ubuntu",   "Ubuntu",               "sudo apt install {pkg}"},
    {"linuxmint","Linux Mint",           "sudo apt install {pkg}"},
    {"pop",      "Pop!_OS",              "sudo apt install {pkg}"},
    {"fedora",   "Fedora",               "sudo dnf install {pkg}"},
    {"rhel",     "Red Hat / CentOS",     "sudo dnf install {pkg}"},
    {"opensuse", "openSUSE",             "sudo zypper install {pkg}"},
    {"suse",     "SUSE",                 "sudo zypper install {pkg}"},
    {"alpine",   "Alpine",               "sudo apk add {pkg}"},
    {"void",     "Void",                 "sudo xbps-install {pkg}"},
    {"gentoo",   "Gentoo",               "sudo emerge {pkg}"},
    {"nixos",    "NixOS",                "nix-env -iA nixpkgs.{pkg}"},
};

QString substitute(QString tmpl, const QString &pkg)
{
    return tmpl.replace(QStringLiteral("{pkg}"), pkg);
}

// Build an install hint map for all known distros. `pkgByDistro` lets a
// caller override the package name per distro (e.g. fd is "fd" on Arch
// but "fd-find" on Debian). Unlisted distros get the default package name.
QVariantMap buildHints(const QString &defaultPkg,
                       const QHash<QString, QString> &pkgByDistro = {})
{
    QVariantMap out;
    for (const auto &d : kDistros) {
        const QString id = QString::fromLatin1(d.id);
        const QString pkg = pkgByDistro.value(id, defaultPkg);
        out[id] = substitute(QString::fromLatin1(d.installCmd), pkg);
    }
    out[QStringLiteral("generic")] =
        QStringLiteral("Install '%1' via your distribution's package manager.")
            .arg(defaultPkg);
    return out;
}

// Install hint for a compile-time feature: there's no package to install
// on the host, so instruct the user to rebuild with the right library or
// pick a different HyprFM build.
QVariantMap buildFeatureHint(const QString &library)
{
    QVariantMap out;
    const QString msg = QStringLiteral(
        "This build of HyprFM was compiled without %1 support. "
        "Install the full (non-minimal) package or rebuild HyprFM with %1 "
        "available at configure time.").arg(library);
    out[QStringLiteral("generic")] = msg;
    return out;
}

} // namespace

DependencyChecker::DependencyChecker(QObject *parent)
    : QObject(parent)
{
    detectDistro();
    populate();
}

void DependencyChecker::refresh()
{
    populate();
    emit dependenciesChanged();
}

void DependencyChecker::detectDistro()
{
    // Prefer the host os-release when running under Flatpak, so the install
    // command we show matches the distro outside the sandbox.
    const QStringList candidates = {
        QStringLiteral("/run/host/etc/os-release"),
        QStringLiteral("/run/host/usr/lib/os-release"),
        QStringLiteral("/etc/os-release"),
        QStringLiteral("/usr/lib/os-release"),
    };

    QString id;
    QString idLike;
    QString prettyName;

    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            auto readValue = [](const QString &line) {
                const int eq = line.indexOf(QLatin1Char('='));
                if (eq < 0)
                    return QString();
                QString v = line.mid(eq + 1).trimmed();
                if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
                    v = v.mid(1, v.size() - 2);
                return v;
            };
            if (line.startsWith(QStringLiteral("ID=")))
                id = readValue(line).toLower();
            else if (line.startsWith(QStringLiteral("ID_LIKE=")))
                idLike = readValue(line).toLower();
            else if (line.startsWith(QStringLiteral("PRETTY_NAME=")))
                prettyName = readValue(line);
        }
        if (!id.isEmpty())
            break;
    }

    // Normalise to a known distro id we have templates for. Fall back to
    // ID_LIKE tokens so derivatives (e.g. CachyOS → arch) map correctly.
    auto matches = [](const QString &candidate) {
        for (const auto &d : kDistros) {
            if (candidate == QLatin1String(d.id))
                return true;
        }
        return false;
    };

    if (matches(id)) {
        m_distroId = id;
    } else {
        const QStringList likeTokens = idLike.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &tok : likeTokens) {
            if (matches(tok)) {
                m_distroId = tok;
                break;
            }
        }
    }

    if (m_distroId.isEmpty())
        m_distroId = QStringLiteral("generic");
    m_distroName = prettyName.isEmpty() ? m_distroId : prettyName;
}

void DependencyChecker::populate()
{
    m_deps.clear();

    // ── Required runtime tools ────────────────────────────────────────────
    m_deps.append({
        QStringLiteral("gio"),
        QStringLiteral("GIO command-line tool"),
        QStringLiteral("Move, copy, and trash files via GVFS."),
        Kind::Tool, true, hasExecutable(QStringLiteral("gio")),
        {QStringLiteral("gio")},
        buildHints(QStringLiteral("glib2"), {
            {QStringLiteral("debian"),   QStringLiteral("libglib2.0-bin")},
            {QStringLiteral("ubuntu"),   QStringLiteral("libglib2.0-bin")},
            {QStringLiteral("linuxmint"),QStringLiteral("libglib2.0-bin")},
            {QStringLiteral("pop"),      QStringLiteral("libglib2.0-bin")},
            {QStringLiteral("fedora"),   QStringLiteral("glib2")},
            {QStringLiteral("rhel"),     QStringLiteral("glib2")},
            {QStringLiteral("opensuse"), QStringLiteral("glib2-tools")},
            {QStringLiteral("suse"),     QStringLiteral("glib2-tools")},
            {QStringLiteral("alpine"),   QStringLiteral("glib")},
        })
    });

    m_deps.append({
        QStringLiteral("rsync"),
        QStringLiteral("rsync"),
        QStringLiteral("Fast copy/move with progress reporting."),
        Kind::Tool, true, hasExecutable(QStringLiteral("rsync")),
        {QStringLiteral("rsync")},
        buildHints(QStringLiteral("rsync"))
    });

    // ── Optional runtime tools ────────────────────────────────────────────
    m_deps.append({
        QStringLiteral("fd"),
        QStringLiteral("fd (file finder)"),
        QStringLiteral("Fast recursive file search in the search bar."),
        Kind::Tool, false,
        hasExecutable(QStringLiteral("fd")) || hasExecutable(QStringLiteral("fdfind")),
        {QStringLiteral("fd"), QStringLiteral("fdfind")},
        buildHints(QStringLiteral("fd"), {
            {QStringLiteral("debian"),   QStringLiteral("fd-find")},
            {QStringLiteral("ubuntu"),   QStringLiteral("fd-find")},
            {QStringLiteral("linuxmint"),QStringLiteral("fd-find")},
            {QStringLiteral("pop"),      QStringLiteral("fd-find")},
        })
    });

    m_deps.append({
        QStringLiteral("wl-clipboard"),
        QStringLiteral("wl-clipboard"),
        QStringLiteral("Copy and paste files and images under Wayland."),
        Kind::Tool, false,
        hasExecutable(QStringLiteral("wl-copy")) && hasExecutable(QStringLiteral("wl-paste")),
        {QStringLiteral("wl-copy"), QStringLiteral("wl-paste")},
        buildHints(QStringLiteral("wl-clipboard"))
    });

    m_deps.append({
        QStringLiteral("ffmpeg"),
        QStringLiteral("FFmpeg"),
        QStringLiteral("Generate video thumbnails in grid and list views."),
        Kind::Tool, false, hasExecutable(QStringLiteral("ffmpeg")),
        {QStringLiteral("ffmpeg")},
        buildHints(QStringLiteral("ffmpeg"))
    });

    m_deps.append({
        QStringLiteral("bat"),
        QStringLiteral("bat"),
        QStringLiteral("Syntax-highlighted text previews."),
        Kind::Tool, false,
        hasExecutable(QStringLiteral("bat")) || hasExecutable(QStringLiteral("batcat")),
        {QStringLiteral("bat"), QStringLiteral("batcat")},
        buildHints(QStringLiteral("bat"), {
            {QStringLiteral("debian"),   QStringLiteral("bat")},
            {QStringLiteral("ubuntu"),   QStringLiteral("bat")},
            {QStringLiteral("linuxmint"),QStringLiteral("bat")},
            {QStringLiteral("pop"),      QStringLiteral("bat")},
        })
    });

    m_deps.append({
        QStringLiteral("git"),
        QStringLiteral("git"),
        QStringLiteral("Show git status badges on files and folders."),
        Kind::Tool, false, hasExecutable(QStringLiteral("git")),
        {QStringLiteral("git")},
        buildHints(QStringLiteral("git"))
    });

    // ntfs-3g: the mount itself runs on the host via UDisks2, so we must
    // check the host's filesystem when running under Flatpak — `which`
    // inside the sandbox won't see /usr/sbin/mount.ntfs on the host.
    const bool ntfsHelperAvailable =
        hasExecutable(QStringLiteral("mount.ntfs"))
        || hasExecutable(QStringLiteral("mount.ntfs-3g"))
        || hasHostExecutable(QStringLiteral("mount.ntfs"))
        || hasHostExecutable(QStringLiteral("mount.ntfs-3g"));

    m_deps.append({
        QStringLiteral("ntfs-3g"),
        QStringLiteral("ntfs-3g (NTFS mount helper)"),
        QStringLiteral("Mount Windows NTFS partitions from the device sidebar."),
        Kind::Tool, false, ntfsHelperAvailable,
        {QStringLiteral("mount.ntfs"), QStringLiteral("mount.ntfs-3g")},
        buildHints(QStringLiteral("ntfs-3g"))
    });

    m_deps.append({
        QStringLiteral("gvfs"),
        QStringLiteral("GVFS backends"),
        QStringLiteral("Connect to remote file systems (SFTP, SMB, WebDAV)."),
        Kind::Tool, false, hasExecutable(QStringLiteral("gio"))
            && QDir(QStringLiteral("/usr/lib/gvfs")).exists(),
        {QStringLiteral("gio")},
        buildHints(QStringLiteral("gvfs"))
    });

    const bool gvfsAfcAvailable = hasExecutable(QStringLiteral("gio")) && hasAnyFile({
        QStringLiteral("/usr/share/gvfs/mounts/afc.mount"),
        QStringLiteral("/usr/lib/gvfs/gvfsd-afc"),
        QStringLiteral("/usr/libexec/gvfsd-afc"),
    });
    m_deps.append({
        QStringLiteral("gvfs-afc"),
        QStringLiteral("GVFS AFC backend"),
        QStringLiteral("Browse iPhone and iPad files from the device sidebar over USB."),
        Kind::Tool, false, gvfsAfcAvailable,
        {QStringLiteral("afc.mount"), QStringLiteral("gvfsd-afc")},
        buildHints(QStringLiteral("gvfs-afc"), {
            {QStringLiteral("debian"),    QStringLiteral("gvfs-backends")},
            {QStringLiteral("ubuntu"),    QStringLiteral("gvfs-backends")},
            {QStringLiteral("linuxmint"), QStringLiteral("gvfs-backends")},
            {QStringLiteral("pop"),       QStringLiteral("gvfs-backends")},
        })
    });

    const bool gvfsGphoto2Available = hasExecutable(QStringLiteral("gio")) && hasAnyFile({
        QStringLiteral("/usr/share/gvfs/mounts/gphoto2.mount"),
        QStringLiteral("/usr/lib/gvfs/gvfsd-gphoto2"),
        QStringLiteral("/usr/libexec/gvfsd-gphoto2"),
    });
    m_deps.append({
        QStringLiteral("gvfs-gphoto2"),
        QStringLiteral("GVFS gphoto2 backend"),
        QStringLiteral("Browse iPhone photos and videos through the camera/PTP backend."),
        Kind::Tool, false, gvfsGphoto2Available,
        {QStringLiteral("gphoto2.mount"), QStringLiteral("gvfsd-gphoto2")},
        buildHints(QStringLiteral("gvfs-gphoto2"), {
            {QStringLiteral("debian"),    QStringLiteral("gvfs-backends")},
            {QStringLiteral("ubuntu"),    QStringLiteral("gvfs-backends")},
            {QStringLiteral("linuxmint"), QStringLiteral("gvfs-backends")},
            {QStringLiteral("pop"),       QStringLiteral("gvfs-backends")},
        })
    });

    m_deps.append({
        QStringLiteral("usbmuxd"),
        QStringLiteral("usbmuxd"),
        QStringLiteral("Detect and broker USB connections to iPhone and iPad devices."),
        Kind::Tool, false, hasExecutable(QStringLiteral("usbmuxd"))
            || hasHostExecutable(QStringLiteral("usbmuxd")),
        {QStringLiteral("usbmuxd")},
        buildHints(QStringLiteral("usbmuxd"))
    });

    m_deps.append({
        QStringLiteral("libimobiledevice"),
        QStringLiteral("libimobiledevice tools"),
        QStringLiteral("Provide pairing and recovery tools such as idevicepair for iPhone and iPad access."),
        Kind::Tool, false, hasExecutable(QStringLiteral("idevicepair"))
            || hasHostExecutable(QStringLiteral("idevicepair")),
        {QStringLiteral("idevicepair")},
        buildHints(QStringLiteral("libimobiledevice"), {
            {QStringLiteral("debian"),    QStringLiteral("libimobiledevice-utils")},
            {QStringLiteral("ubuntu"),    QStringLiteral("libimobiledevice-utils")},
            {QStringLiteral("linuxmint"), QStringLiteral("libimobiledevice-utils")},
            {QStringLiteral("pop"),       QStringLiteral("libimobiledevice-utils")},
        })
    });

    // ── DBus services ─────────────────────────────────────────────────────
    m_deps.append({
        QStringLiteral("udisks2"),
        QStringLiteral("UDisks2 service"),
        QStringLiteral("Detect, mount, and unmount storage devices."),
        Kind::Service, false, udisks2Reachable(),
        {},
        buildHints(QStringLiteral("udisks2"))
    });

    // ── Optional runtime tools (media previews / metadata) ──────────────
    //
    // These used to be compile-time features linked against poppler-qt6,
    // libexif, taglib, and libavformat. They're now CLI tools so installing
    // the package picks them up at runtime — no rebuild needed.
    m_deps.append({
        QStringLiteral("poppler-utils"),
        QStringLiteral("PDF preview (poppler-utils)"),
        QStringLiteral("Render PDF thumbnails and the PDF quick-preview panel."),
        Kind::Tool, false,
        hasExecutable(QStringLiteral("pdftoppm"))
            && hasExecutable(QStringLiteral("pdfinfo")),
        {QStringLiteral("pdftoppm"), QStringLiteral("pdfinfo")},
        buildHints(QStringLiteral("poppler"), {
            {QStringLiteral("debian"),    QStringLiteral("poppler-utils")},
            {QStringLiteral("ubuntu"),    QStringLiteral("poppler-utils")},
            {QStringLiteral("linuxmint"), QStringLiteral("poppler-utils")},
            {QStringLiteral("pop"),       QStringLiteral("poppler-utils")},
            {QStringLiteral("fedora"),    QStringLiteral("poppler-utils")},
            {QStringLiteral("rhel"),      QStringLiteral("poppler-utils")},
            {QStringLiteral("opensuse"),  QStringLiteral("poppler-tools")},
            {QStringLiteral("suse"),      QStringLiteral("poppler-tools")},
            {QStringLiteral("alpine"),    QStringLiteral("poppler-utils")},
            {QStringLiteral("void"),      QStringLiteral("poppler")},
            {QStringLiteral("gentoo"),    QStringLiteral("app-text/poppler")},
            {QStringLiteral("nixos"),     QStringLiteral("poppler_utils")},
        })
    });

    m_deps.append({
        QStringLiteral("exiftool"),
        QStringLiteral("EXIF metadata (exiftool)"),
        QStringLiteral("Read camera, GPS and timestamp metadata from images."),
        Kind::Tool, false, hasExecutable(QStringLiteral("exiftool")),
        {QStringLiteral("exiftool")},
        buildHints(QStringLiteral("perl-image-exiftool"), {
            {QStringLiteral("debian"),    QStringLiteral("libimage-exiftool-perl")},
            {QStringLiteral("ubuntu"),    QStringLiteral("libimage-exiftool-perl")},
            {QStringLiteral("linuxmint"), QStringLiteral("libimage-exiftool-perl")},
            {QStringLiteral("pop"),       QStringLiteral("libimage-exiftool-perl")},
            {QStringLiteral("fedora"),    QStringLiteral("perl-Image-ExifTool")},
            {QStringLiteral("rhel"),      QStringLiteral("perl-Image-ExifTool")},
            {QStringLiteral("opensuse"),  QStringLiteral("exiftool")},
            {QStringLiteral("suse"),      QStringLiteral("exiftool")},
            {QStringLiteral("alpine"),    QStringLiteral("exiftool")},
            {QStringLiteral("void"),      QStringLiteral("perl-Image-ExifTool")},
            {QStringLiteral("gentoo"),    QStringLiteral("media-libs/exiftool")},
            {QStringLiteral("nixos"),     QStringLiteral("exiftool")},
        })
    });

    m_deps.append({
        QStringLiteral("ffprobe"),
        QStringLiteral("Audio/video metadata (ffprobe)"),
        QStringLiteral("Read tags, duration, codec, and resolution from audio and video."),
        Kind::Tool, false, hasExecutable(QStringLiteral("ffprobe")),
        {QStringLiteral("ffprobe")},
        buildHints(QStringLiteral("ffmpeg"))
    });

#ifdef HYPRFM_HAS_KWINDOWSYSTEM
    const bool hasKWin = true;
#else
    const bool hasKWin = false;
#endif
    m_deps.append({
        QStringLiteral("kwindowsystem"),
        QStringLiteral("Window blur (KWindowSystem)"),
        QStringLiteral("Enable background blur and contrast under KWin."),
        Kind::Feature, false, hasKWin, {},
        buildFeatureHint(QStringLiteral("kwindowsystem"))
    });
}

QVariantList DependencyChecker::dependencies() const
{
    QVariantList out;
    out.reserve(m_deps.size());
    for (const auto &dep : m_deps)
        out.append(toVariant(dep));
    return out;
}

QVariantList DependencyChecker::missingDependencies() const
{
    QVariantList out;
    for (const auto &dep : m_deps) {
        if (!dep.available)
            out.append(toVariant(dep));
    }
    return out;
}

bool DependencyChecker::hasAnyMissing() const
{
    for (const auto &dep : m_deps) {
        if (!dep.available)
            return true;
    }
    return false;
}

bool DependencyChecker::hasMissingRequired() const
{
    for (const auto &dep : m_deps) {
        if (!dep.available && dep.required)
            return true;
    }
    return false;
}

QString DependencyChecker::installCommandFor(const QString &id) const
{
    for (const auto &dep : m_deps) {
        if (dep.id != id)
            continue;
        if (dep.installHints.contains(m_distroId))
            return dep.installHints.value(m_distroId).toString();
        return dep.installHints.value(QStringLiteral("generic")).toString();
    }
    return {};
}

QVariantMap DependencyChecker::toVariant(const Dependency &dep) const
{
    QVariantMap m;
    m[QStringLiteral("id")]          = dep.id;
    m[QStringLiteral("displayName")] = dep.displayName;
    m[QStringLiteral("purpose")]     = dep.purpose;
    m[QStringLiteral("required")]    = dep.required;
    m[QStringLiteral("available")]   = dep.available;
    m[QStringLiteral("commands")]    = dep.commands;
    m[QStringLiteral("installHints")]= dep.installHints;

    QString kindStr;
    switch (dep.kind) {
    case Kind::Tool:    kindStr = QStringLiteral("tool"); break;
    case Kind::Feature: kindStr = QStringLiteral("feature"); break;
    case Kind::Service: kindStr = QStringLiteral("service"); break;
    }
    m[QStringLiteral("kind")] = kindStr;

    const QString hint = dep.installHints.contains(m_distroId)
        ? dep.installHints.value(m_distroId).toString()
        : dep.installHints.value(QStringLiteral("generic")).toString();
    m[QStringLiteral("installCommand")] = hint;

    return m;
}

bool DependencyChecker::hasExecutable(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

bool DependencyChecker::hasAnyFile(const QStringList &paths)
{
    QStringList candidates = paths;
    if (inFlatpakSandbox()) {
        for (const QString &path : paths) {
            if (path.startsWith(QLatin1Char('/')))
                candidates.append(QStringLiteral("/run/host") + path);
        }
    }

    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return true;
    }
    return false;
}

bool DependencyChecker::inFlatpakSandbox()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

bool DependencyChecker::hasHostExecutable(const QString &name)
{
    // Only meaningful inside the Flatpak sandbox; on a host install this
    // would just duplicate the regular PATH lookup.
    if (!inFlatpakSandbox())
        return false;

    static const QStringList hostBinDirs = {
        QStringLiteral("/run/host/usr/sbin"),
        QStringLiteral("/run/host/sbin"),
        QStringLiteral("/run/host/usr/bin"),
        QStringLiteral("/run/host/bin"),
        QStringLiteral("/run/host/usr/local/sbin"),
        QStringLiteral("/run/host/usr/local/bin"),
    };

    for (const QString &dir : hostBinDirs) {
        if (QFileInfo(QDir(dir).filePath(name)).isExecutable())
            return true;
    }
    return false;
}

bool DependencyChecker::udisks2Reachable()
{
    auto *iface = QDBusConnection::systemBus().interface();
    if (!iface)
        return false;
    return iface->isServiceRegistered(QStringLiteral("org.freedesktop.UDisks2"));
}
