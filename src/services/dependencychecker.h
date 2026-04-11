#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Aggregates every runtime tool and compile-time feature HyprFM can use, with
// human-readable purpose strings and per-distro install commands. Drives the
// MissingDependenciesDialog. Separate from RuntimeFeaturesService (which owns
// simple Q_PROPERTY feature flags consumed throughout QML) — this service is
// the "list it all for the dialog" view.
class DependencyChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList dependencies READ dependencies NOTIFY dependenciesChanged)
    Q_PROPERTY(QVariantList missingDependencies READ missingDependencies NOTIFY dependenciesChanged)
    Q_PROPERTY(bool hasAnyMissing READ hasAnyMissing NOTIFY dependenciesChanged)
    Q_PROPERTY(bool hasMissingRequired READ hasMissingRequired NOTIFY dependenciesChanged)
    Q_PROPERTY(QString distroId READ distroId CONSTANT)
    Q_PROPERTY(QString distroName READ distroName CONSTANT)

public:
    enum class Kind {
        Tool,      // External binary on PATH (or in host /usr/*bin under Flatpak)
        Feature,   // Compile-time feature (#ifdef baked into the build)
        Service,   // DBus service (e.g. UDisks2)
    };

    struct Dependency {
        QString id;              // Stable identifier, e.g. "ntfs-3g"
        QString displayName;     // e.g. "NTFS mount helper"
        QString purpose;         // One-line user-facing description
        Kind kind;
        bool required;           // true → HyprFM won't work well without it
        bool available;          // Resolved at construction / refresh()
        QStringList commands;    // Executable candidates tried on PATH (for tools)
        QVariantMap installHints;// distroId → install command (plus "generic")
    };

    explicit DependencyChecker(QObject *parent = nullptr);

    QVariantList dependencies() const;
    QVariantList missingDependencies() const;
    bool hasAnyMissing() const;
    bool hasMissingRequired() const;
    QString distroId() const { return m_distroId; }
    QString distroName() const { return m_distroName; }

    Q_INVOKABLE void refresh();
    // Returns the install command for a given dep id on the current distro,
    // falling back to the generic hint if no distro-specific entry exists.
    Q_INVOKABLE QString installCommandFor(const QString &id) const;

signals:
    void dependenciesChanged();

private:
    void detectDistro();
    void populate();
    static bool hasExecutable(const QString &name);
    static bool hasHostExecutable(const QString &name);
    static bool inFlatpakSandbox();
    static bool udisks2Reachable();

    QVariantMap toVariant(const Dependency &dep) const;

    QList<Dependency> m_deps;
    QString m_distroId;
    QString m_distroName;
};
