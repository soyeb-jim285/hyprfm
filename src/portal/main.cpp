#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QDBusVariant>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QDir>
#include <QDebug>

using Rule = QPair<uint, QString>;
using Rules = QList<Rule>;
using Filter = QPair<QString, Rules>;
using Filters = QList<Filter>;
using StringPair = QPair<QString, QString>;
using StringPairs = QList<StringPair>;
struct Choice {
    QString id, label;
    StringPairs items;
    QString selected;
};
using Choices = QList<Choice>;
Q_DECLARE_METATYPE(Choice)
Q_DECLARE_METATYPE(Choices)
QDBusArgument &operator<<(QDBusArgument &arg, const Choice &v)
{
    arg.beginStructure();
    arg << v.id << v.label << v.items << v.selected;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, Choice &v)
{
    arg.beginStructure();
    arg >> v.id >> v.label >> v.items >> v.selected;
    arg.endStructure();
    return arg;
}

struct Pending : QObject {
    QTemporaryDir directory;
    QProcess process;
    QDBusMessage call;
    QVariantMap options;
    bool cancelled = false;
    explicit Pending(QObject *parent) : QObject(parent) {}
};

class Portal : public QDBusVirtualObject
{
    Q_OBJECT
public:
    using QDBusVirtualObject::QDBusVirtualObject;
    QHash<QString, Pending *> pending;
    QString introspect(const QString &path) const override
    {
        if (pending.contains(path))
            return QStringLiteral(
                "<interface name='org.freedesktop.impl.portal.Request'><method name='Close'/></interface>");
        return QStringLiteral(
            "<interface name='org.freedesktop.impl.portal.FileChooser'>"
            "<property name='version' type='u' access='read'/>"
            "<method name='OpenFile'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg "
            "type='s' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg "
            "type='u' direction='out'/><arg type='a{sv}' direction='out'/></method>"
            "<method name='SaveFile'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg "
            "type='s' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg "
            "type='u' direction='out'/><arg type='a{sv}' direction='out'/></method>"
            "<method name='SaveFiles'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg "
            "type='s' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg "
            "type='u' direction='out'/><arg type='a{sv}' direction='out'/></method>"
            "</interface>");
    }
    void finish(const QString &handle, uint response)
    {
        auto *p = pending.take(handle);
        if (!p)
            return;
        QVariantMap results;
        if (response == 0) {
            QJsonParseError error;
            const auto output = p->process.readAllStandardOutput();
            const auto doc = QJsonDocument::fromJson(output, &error);
            const auto json = doc.object();
            QStringList uris;
            for (const auto &value : json.value("uris").toArray()) {
                const QUrl uri(value.toString());
                if (!uri.isLocalFile() || !QDir::isAbsolutePath(uri.toLocalFile())) {
                    response = 2;
                    break;
                }
                uris << uri.toString(QUrl::FullyEncoded);
            }
            if (output.trimmed().isEmpty())
                response = 1;
            else if (error.error != QJsonParseError::NoError || !doc.isObject() || uris.isEmpty())
                response = 2;
            if (response == 0) {
                results.insert("uris", uris);
                const auto filters = qdbus_cast<Filters>(p->options.value("filters"));
                const int index = json.value("filter_index").toInt(-1);
                if (index >= 0 && index < filters.size())
                    results.insert("current_filter", QVariant::fromValue(filters[index]));
                StringPairs choices;
                const auto values = json.value("choices").toObject();
                for (auto it = values.begin(); it != values.end(); ++it)
                    choices << StringPair(it.key(), it.value().toString());
                if (!choices.isEmpty())
                    results.insert("choices", QVariant::fromValue(choices));
            }
        }
        QDBusConnection::sessionBus().send(p->call.createReply({response, results}));
        p->deleteLater();
    }
    bool handleMessage(const QDBusMessage &msg, const QDBusConnection &bus) override
    {
        const auto args = msg.arguments();
        if (msg.interface() == "org.freedesktop.DBus.Properties") {
            if (msg.member() == "Get" && args.size() == 2 &&
                args[0].toString() == "org.freedesktop.impl.portal.FileChooser" &&
                args[1].toString() == "version")
                return bus.send(msg.createReply({QVariant::fromValue(QDBusVariant(uint(4)))}));
            if (msg.member() == "GetAll" && args.size() == 1 &&
                args[0].toString() == "org.freedesktop.impl.portal.FileChooser")
                return bus.send(msg.createReply(QVariantList{QVariantMap{{"version", uint(4)}}}));
            return false;
        }
        // Only xdg-desktop-portal owns the backend request lifecycle.
        if (bus.interface()->serviceOwner("org.freedesktop.portal.Desktop").value() != msg.service()) {
            bus.send(msg.createErrorReply(QDBusError::AccessDenied,
                                          "Only xdg-desktop-portal may call this backend"));
            return true;
        }
        if (msg.interface() == "org.freedesktop.impl.portal.Request" && msg.member() == "Close") {
            if (auto *p = pending.value(msg.path())) {
                p->cancelled = true;
                p->process.kill();
            }
            bus.send(msg.createReply());
            return true;
        }
        if (msg.interface() != "org.freedesktop.impl.portal.FileChooser" || args.size() != 5 ||
            (msg.member() != "OpenFile" && msg.member() != "SaveFile" && msg.member() != "SaveFiles"))
            return false;
        const QString handle = qvariant_cast<QDBusObjectPath>(args[0]).path();
        if (handle.isEmpty() || pending.contains(handle)) {
            bus.send(msg.createErrorReply(QDBusError::InvalidArgs, "Invalid or duplicate request"));
            return true;
        }
        auto *p = new Pending(this);
        p->call = msg;
        p->options = qdbus_cast<QVariantMap>(args[4]);
        const auto &options = p->options;
        const auto pathOption = [&](const char *key) {
            QByteArray bytes = qdbus_cast<QByteArray>(options.value(key));
            if (bytes.endsWith('\0'))
                bytes.chop(1);
            return QFile::decodeName(bytes);
        };
        QJsonObject request{{"mode", msg.member() == "SaveFile"    ? "save"
                                     : msg.member() == "SaveFiles" ? "save_files"
                                                                   : "open"},
                            {"title", args[3].toString()},
                            {"app_id", args[1].toString()},
                            {"parent_window", args[2].toString()},
                            {"modal", options.value("modal", true).toBool()},
                            {"directory", msg.member() == "SaveFiles" || options.value("directory").toBool()},
                            {"multiple", options.value("multiple").toBool()},
                            {"accept_label", options.value("accept_label").toString()},
                            {"current_name", options.value("current_name").toString()},
                            {"current_file", pathOption("current_file")},
                            {"current_folder", pathOption("current_folder")}};
        if (request.value("current_name").toString().isEmpty() &&
            !request.value("current_file").toString().isEmpty())
            request["current_name"] = QFileInfo(request.value("current_file").toString()).fileName();
        QJsonArray jsonFilters;
        const auto filters = qdbus_cast<Filters>(options.value("filters"));
        const auto currentFilter = qdbus_cast<Filter>(options.value("current_filter"));
        int filterIndex = 0;
        for (int i = 0; i < filters.size(); ++i) {
            QJsonArray rules;
            for (const auto &rule : filters[i].second)
                rules.append(QJsonObject{{"type", int(rule.first)}, {"pattern", rule.second}});
            jsonFilters.append(QJsonObject{{"name", filters[i].first}, {"rules", rules}});
            if (filters[i] == currentFilter)
                filterIndex = i;
        }
        request["filters"] = jsonFilters;
        request["filter_index"] = filterIndex;
        QJsonArray jsonChoices;
        for (const auto &choice : qdbus_cast<Choices>(options.value("choices"))) {
            QJsonArray items;
            for (const auto &item : choice.items)
                items.append(QJsonObject{{"id", item.first}, {"label", item.second}});
            jsonChoices.append(QJsonObject{
                {"id", choice.id}, {"label", choice.label}, {"items", items}, {"selected", choice.selected}});
        }
        request["choices"] = jsonChoices;
        QJsonArray names;
        for (QByteArray name : qdbus_cast<QList<QByteArray>>(options.value("files"))) {
            if (name.endsWith('\0'))
                name.chop(1);
            names.append(QFile::decodeName(name));
        }
        request["files"] = names;
        const QString requestPath = p->directory.filePath("request.json");
        QFile file(requestPath);
        if (!p->directory.isValid() || !file.open(QIODevice::WriteOnly) ||
            file.write(QJsonDocument(request).toJson()) < 0) {
            bus.send(msg.createReply({uint(2), QVariantMap{}}));
            delete p;
            return true;
        }
        file.close();
        pending.insert(handle, p);
        QObject::connect(
            &p->process, &QProcess::finished, this, [this, p, handle](int code, QProcess::ExitStatus status) {
                finish(handle, p->cancelled ? 1 : status != QProcess::NormalExit || code != 0 ? 2 : 0);
            });
        QObject::connect(&p->process, &QProcess::errorOccurred, this,
                         [this, handle](QProcess::ProcessError error) {
                             if (error == QProcess::FailedToStart)
                                 finish(handle, 2);
                         });
        QObject::connect(&p->process, &QProcess::readyReadStandardError, this,
                         [p] { qWarning().noquote() << p->process.readAllStandardError(); });
        QString executable = qEnvironmentVariable("HYPRFM_PICKER_EXECUTABLE");
        if (executable.isEmpty())
            executable = QCoreApplication::applicationDirPath() + "/hyprfm";
        p->process.start(executable, {"--file-picker", requestPath});
        return true;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<Rule>();
    qDBusRegisterMetaType<Rules>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<Filters>();
    qDBusRegisterMetaType<StringPair>();
    qDBusRegisterMetaType<StringPairs>();
    qDBusRegisterMetaType<Choice>();
    qDBusRegisterMetaType<Choices>();
    qDBusRegisterMetaType<QList<QByteArray>>();
    auto bus = QDBusConnection::sessionBus();
    Portal portal;
    if (!bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::SubPath) ||
        !bus.registerService("org.freedesktop.impl.portal.desktop.hyprfm")) {
        qCritical() << bus.lastError().message();
        return 1;
    }
    QDBusServiceWatcher watcher("org.freedesktop.portal.Desktop", bus,
                                QDBusServiceWatcher::WatchForOwnerChange);
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, &portal,
                     [&portal](const QString &, const QString &oldOwner, const QString &) {
                         if (oldOwner.isEmpty())
                             return;
                         for (auto *request : std::as_const(portal.pending)) {
                             request->cancelled = true;
                             request->process.kill();
                         }
                     });
    return app.exec();
}
#include "main.moc"
