#include "filepicker.h"
#include <QJsonArray>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QUrl>

FilePicker::FilePicker(const QJsonObject &request, QObject *parent)
    : QSortFilterProxyModel(parent), m_request(request), m_filterIndex(request.value("filter_index").toInt())
{
    for (const auto &value : request.value("choices").toArray()) {
        const auto choice = value.toObject();
        QString selected = choice.value("selected").toString();
        const auto items = choice.value("items").toArray();
        if (selected.isEmpty())
            selected =
                items.isEmpty() ? QStringLiteral("false") : items.first().toObject().value("id").toString();
        m_choices.insert(choice.value("id").toString(), selected);
    }
}

QString FilePicker::initialFolder() const
{
    QString path = m_request.value("current_folder").toString();
    const QString currentFile = m_request.value("current_file").toString();
    if (!currentFile.isEmpty())
        path = QFileInfo(currentFile).absolutePath();
    if (!QFileInfo(path).isDir())
        path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QFileInfo(path).isDir() ? path : QDir::homePath();
}

QString FilePicker::filePath(int row) const
{
    return data(index(row, 0), FileSystemModel::FilePathRole).toString();
}
QString FilePicker::fileName(int row) const
{
    return data(index(row, 0), FileSystemModel::FileNameRole).toString();
}
bool FilePicker::isDir(int row) const
{
    return data(index(row, 0), FileSystemModel::IsDirRole).toBool();
}
QVariantMap FilePicker::folderItemCounts(const QStringList &paths) const
{
    return files()->folderItemCounts(paths);
}
bool FilePicker::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}
bool FilePicker::navigate(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isDir() || !info.isReadable())
        return fail(tr("The folder is not accessible."));
    fail({});
    files()->setRootPath(info.absoluteFilePath());
    return true;
}
void FilePicker::setChoice(const QString &id, const QString &value)
{
    if (m_choices.contains(id))
        m_choices.insert(id, value);
}
void FilePicker::setFilterIndex(int value)
{
    if (value == m_filterIndex)
        return;
    m_filterIndex = value;
    invalidateFilter();
    emit filterIndexChanged();
}
bool FilePicker::filterAcceptsRow(int row, const QModelIndex &) const
{
    if (files()->isDir(row))
        return true;
    if (m_request.value("directory").toBool())
        return false;
    const auto filters = m_request.value("filters").toArray();
    if (m_filterIndex < 0 || m_filterIndex >= filters.size())
        return true;
    const auto rules = filters[m_filterIndex].toObject().value("rules").toArray();
    if (rules.isEmpty())
        return true;
    for (const auto &value : rules) {
        const auto rule = value.toObject();
        const QString pattern = rule.value("pattern").toString();
        if (rule.value("type").toInt() == 0) {
            if (QDir::match(pattern, files()->fileName(row)))
                return true;
        } else {
            const auto mime =
                QMimeDatabase().mimeTypeForFile(files()->filePath(row), QMimeDatabase::MatchExtension);
            if (pattern.endsWith("/*") ? mime.name().startsWith(pattern.chopped(1)) : mime.inherits(pattern))
                return true;
        }
    }
    return false;
}

bool FilePicker::submit(const QString &folder, const QString &name, const QStringList &selection,
                        bool overwrite)
{
    fail({});
    const QString mode = m_request.value("mode").toString();
    QStringList paths;
    if (mode == "save") {
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains(QChar::Null))
            return fail(tr("Enter a valid file name."));
        const QFileInfo parent(folder);
        if (!parent.isAbsolute() || !parent.isDir() || !parent.isWritable())
            return fail(tr("You cannot save in this folder."));
        const QString path = QDir(folder).absoluteFilePath(name);
        const QFileInfo target(path);
        if (target.isDir())
            return fail(tr("A folder with this name already exists."));
        if (target.exists() && !target.isWritable())
            return fail(tr("The file is not writable."));
        if ((target.exists() || target.isSymLink()) && !overwrite) {
            emit overwriteRequested();
            return false;
        }
        paths << path;
    } else {
        paths = selection;
        if (paths.isEmpty() && m_request.value("directory").toBool())
            paths << folder;
        if (paths.isEmpty())
            return fail(tr("Select a file."));
        if (!m_request.value("multiple").toBool() && paths.size() > 1)
            return fail(tr("Select a single item."));
        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (!info.isAbsolute() || !info.exists() || !info.isReadable() ||
                info.isDir() != m_request.value("directory").toBool())
                return fail(tr("The selection is invalid or not accessible."));
        }
        if (mode == "save_files") {
            const QString destination = paths.first();
            if (!QFileInfo(destination).isWritable())
                return fail(tr("The folder is not writable."));
            paths.clear();
            QSet<QString> reserved;
            for (const auto &value : m_request.value("files").toArray()) {
                const QString file = value.toString();
                if (file.isEmpty() || file == "." || file == ".." || file.contains('/') ||
                    file.contains(QChar::Null))
                    return fail(tr("The request contains an invalid file name."));
                QString candidate = QDir(destination).filePath(file);
                int suffix = 1;
                while (QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink() ||
                       reserved.contains(candidate))
                    candidate = QDir(destination).filePath(file + QStringLiteral(" (%1)").arg(suffix++));
                reserved.insert(candidate);
                paths << candidate;
            }
            if (paths.isEmpty())
                return fail(tr("There are no files to save."));
        }
    }
    QJsonArray uris;
    for (const auto &path : paths)
        uris.append(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
    emit completed(QJsonObject{{"uris", uris}, {"filter_index", m_filterIndex}, {"choices", m_choices}});
    return true;
}
