#include "services/undomanager.h"
#include "services/fileoperations.h"

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

QHash<QString, QString> parseGioAttributes(const QString &attributeText)
{
    QHash<QString, QString> attrs;
    static const QRegularExpression attrRe(R"(([A-Za-z0-9:-]+)=(.*?)(?= [A-Za-z0-9:-]+=|$))");

    auto it = attrRe.globalMatch(attributeText.trimmed());
    while (it.hasNext()) {
        const auto match = it.next();
        attrs.insert(match.captured(1), match.captured(2));
    }

    return attrs;
}

QString latestTrashUriForOriginalPath(const QString &originalPath)
{
    QProcess proc;
    proc.start("gio", {
        "list",
        "-h",
        "-l",
        "-u",
        "-a",
        "trash::orig-path,trash::deletion-date",
        "trash:///"
    });
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};

    QString latestUri;
    QDateTime latestDeletedAt;
    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split('\t');
        if (fields.size() < 4)
            continue;

        const QString uri = fields.at(0).trimmed();
        const auto attrs = parseGioAttributes(fields.mid(3).join(" "));
        if (QDir::cleanPath(attrs.value("trash::orig-path")) != originalPath)
            continue;

        QDateTime deletedAt = QDateTime::fromString(attrs.value("trash::deletion-date"), Qt::ISODate);
        if (!deletedAt.isValid())
            deletedAt = QDateTime::fromString(attrs.value("trash::deletion-date"), Qt::ISODateWithMs);

        if (latestUri.isEmpty() || !latestDeletedAt.isValid() || deletedAt > latestDeletedAt) {
            latestUri = uri;
            latestDeletedAt = deletedAt;
        }
    }

    return latestUri;
}

QVariantList prepareOperations(const QVariantList &operations, FileOperations *fileOps)
{
    QVariantList prepared;
    for (const QVariant &variant : operations) {
        QVariantMap item = variant.toMap();
        if (item.value("overwrite").toBool() && item.value("backupPath").toString().isEmpty())
            item["backupPath"] = fileOps->conflictBackupPath(item.value("targetPath").toString());
        prepared.append(item);
    }
    return prepared;
}

QStringList operationsField(const QVariantList &operations, const QString &field)
{
    QStringList values;
    for (const QVariant &variant : operations)
        values.append(variant.toMap().value(field).toString());
    return values;
}

QVariantList buildOperations(const QStringList &sources, const QStringList &targets,
                             const QStringList &backupPaths = {})
{
    QVariantList operations;
    const int count = std::min(sources.size(), targets.size());
    for (int i = 0; i < count; ++i) {
        QVariantMap item;
        item["sourcePath"] = sources.at(i);
        item["targetPath"] = targets.at(i);
        const QString backupPath = i < backupPaths.size() ? backupPaths.at(i) : QString();
        item["overwrite"] = !backupPath.isEmpty();
        if (!backupPath.isEmpty())
            item["backupPath"] = backupPath;
        operations.append(item);
    }
    return operations;
}

bool hasBackupPaths(const QStringList &backupPaths)
{
    for (const QString &backupPath : backupPaths) {
        if (!backupPath.isEmpty())
            return true;
    }
    return false;
}

}

UndoManager::UndoManager(FileOperations *fileOps, QObject *parent)
    : QObject(parent)
    , m_fileOps(fileOps)
{
}

bool UndoManager::canUndo() const { return !m_undoStack.isEmpty() && !m_fileOps->busy(); }
bool UndoManager::canRedo() const { return !m_redoStack.isEmpty() && !m_fileOps->busy(); }

void UndoManager::record(const UndoRecord &rec)
{
    m_undoStack.push(rec);
    if (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.removeFirst();
    if (!m_isUndoRedo)
        m_redoStack.clear();
    emit stackChanged();
}

QStringList UndoManager::computeCreatedPaths(const QStringList &sources, const QString &dest)
{
    QStringList result;
    for (const auto &src : sources)
        result.append(dest + "/" + QFileInfo(src).fileName());
    return result;
}

// ── Undoable wrappers ────────────────────────────────────────────────────

void UndoManager::copyFiles(const QStringList &sources, const QString &destination)
{
    copyResolvedItems(buildOperations(sources, computeCreatedPaths(sources, destination)));
}

void UndoManager::copyResolvedItems(const QVariantList &operations)
{
    const QVariantList preparedOperations = prepareOperations(operations, m_fileOps);
    const QStringList sourcePaths = operationsField(preparedOperations, "sourcePath");
    const QStringList targetPaths = operationsField(preparedOperations, "targetPath");
    const QStringList backupPaths = operationsField(preparedOperations, "backupPath");

    m_fileOps->copyResolvedItems(preparedOperations);

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_fileOps, &FileOperations::operationFinished,
                    this, [this, sourcePaths, targetPaths, backupPaths, conn](bool success, const QString &) {
        disconnect(*conn);
        if (success)
            record({UndoRecord::Copy, sourcePaths, {}, {}, {}, targetPaths, backupPaths});
    });
}

void UndoManager::moveFiles(const QStringList &sources, const QString &destination)
{
    moveResolvedItems(buildOperations(sources, computeCreatedPaths(sources, destination)));
}

void UndoManager::moveResolvedItems(const QVariantList &operations)
{
    const QVariantList preparedOperations = prepareOperations(operations, m_fileOps);
    const QStringList sourcePaths = operationsField(preparedOperations, "sourcePath");
    const QStringList targetPaths = operationsField(preparedOperations, "targetPath");
    const QStringList backupPaths = operationsField(preparedOperations, "backupPath");

    m_fileOps->moveResolvedItems(preparedOperations);

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_fileOps, &FileOperations::operationFinished,
                    this, [this, sourcePaths, targetPaths, backupPaths, conn](bool success, const QString &) {
        disconnect(*conn);
        if (success)
            record({UndoRecord::Move, sourcePaths, {}, {}, {}, targetPaths, backupPaths});
    });
}

void UndoManager::trashFiles(const QStringList &paths)
{
    m_fileOps->trashFiles(paths);

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_fileOps, &FileOperations::operationFinished,
                    this, [this, paths, conn](bool success, const QString &) {
        disconnect(*conn);
        if (success)
            record({UndoRecord::Trash, paths, {}, {}, {}, {}, {}});
    });
}

bool UndoManager::rename(const QString &path, const QString &newName)
{
    QString oldName = QFileInfo(path).fileName();
    bool ok = m_fileOps->rename(path, newName);
    if (ok) {
        QString dir = QFileInfo(path).absolutePath();
        record({UndoRecord::Rename, {path}, dir, oldName, newName, {dir + "/" + newName}, {}});
    }
    return ok;
}

void UndoManager::createFolder(const QString &parentPath, const QString &name)
{
    m_fileOps->createFolder(parentPath, name);
    QString created = QDir(parentPath).filePath(name);
    if (QDir(created).exists())
        record({UndoRecord::CreateFolder, {}, parentPath, {}, name, {created}, {}});
}

void UndoManager::createFile(const QString &parentPath, const QString &name)
{
    m_fileOps->createFile(parentPath, name);
    QString created = QDir(parentPath).filePath(name);
    if (QFile::exists(created))
        record({UndoRecord::CreateFile, {}, parentPath, {}, name, {created}, {}});
}

// ── Undo ─────────────────────────────────────────────────────────────────

void UndoManager::undo()
{
    if (m_undoStack.isEmpty() || m_fileOps->busy())
        return;

    UndoRecord rec = m_undoStack.pop();
    m_isUndoRedo = true;
    executeUndo(rec);
    m_redoStack.push(rec);
    m_isUndoRedo = false;
    emit stackChanged();
}

void UndoManager::executeUndo(const UndoRecord &rec)
{
    switch (rec.type) {
    case UndoRecord::Copy:
        if (hasBackupPaths(rec.backupPaths)) {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_fileOps, &FileOperations::operationFinished,
                            this, [this, rec, conn](bool success, const QString &) {
                disconnect(*conn);
                if (success)
                    restoreBackupPaths(rec.createdPaths, rec.backupPaths);
            });
        }
        m_fileOps->deleteFiles(rec.createdPaths);
        break;

    case UndoRecord::Move:
        if (hasBackupPaths(rec.backupPaths)) {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_fileOps, &FileOperations::operationFinished,
                            this, [this, rec, conn](bool success, const QString &) {
                disconnect(*conn);
                if (success)
                    restoreBackupPaths(rec.createdPaths, rec.backupPaths);
            });
        }
        m_fileOps->moveResolvedItems(buildOperations(rec.createdPaths, rec.sourcePaths));
        break;

    case UndoRecord::Rename: {
        // Undo rename = rename back
        QString dir = rec.destination;
        QString currentPath = dir + "/" + rec.newName;
        m_fileOps->rename(currentPath, rec.oldName);
        break;
    }

    case UndoRecord::Trash:
        // Undo trash = restore from trash
        restoreFromTrash(rec.sourcePaths);
        break;

    case UndoRecord::CreateFolder:
        for (const auto &p : rec.createdPaths)
            QDir(p).removeRecursively();
        break;

    case UndoRecord::CreateFile:
        for (const auto &p : rec.createdPaths)
            QFile::remove(p);
        break;
    }
}

// ── Redo ─────────────────────────────────────────────────────────────────

void UndoManager::redo()
{
    if (m_redoStack.isEmpty() || m_fileOps->busy())
        return;

    UndoRecord rec = m_redoStack.pop();
    m_isUndoRedo = true;
    executeRedo(rec);
    m_undoStack.push(rec);
    m_isUndoRedo = false;
    emit stackChanged();
}

void UndoManager::executeRedo(const UndoRecord &rec)
{
    switch (rec.type) {
    case UndoRecord::Copy:
        m_fileOps->copyResolvedItems(buildOperations(rec.sourcePaths, rec.createdPaths, rec.backupPaths));
        break;

    case UndoRecord::Move:
        m_fileOps->moveResolvedItems(buildOperations(rec.sourcePaths, rec.createdPaths, rec.backupPaths));
        break;

    case UndoRecord::Rename: {
        QString path = rec.destination + "/" + rec.oldName;
        m_fileOps->rename(path, rec.newName);
        break;
    }

    case UndoRecord::Trash:
        m_fileOps->trashFiles(rec.sourcePaths);
        break;

    case UndoRecord::CreateFolder:
        if (!rec.createdPaths.isEmpty())
            m_fileOps->createFolder(rec.destination, rec.newName);
        break;

    case UndoRecord::CreateFile:
        if (!rec.createdPaths.isEmpty())
            m_fileOps->createFile(rec.destination, rec.newName);
        break;
    }
}

// ── Trash restore ────────────────────────────────────────────────────────

void UndoManager::restoreFromTrash(const QStringList &originalPaths)
{
    QStringList restoreTargets;

    for (const QString &origPath : originalPaths) {
        const QString trashUri = latestTrashUriForOriginalPath(QDir::cleanPath(origPath));
        if (!trashUri.isEmpty())
            restoreTargets.append(trashUri);
    }

    if (!restoreTargets.isEmpty())
        m_fileOps->restoreFromTrash(restoreTargets);
}

void UndoManager::restoreBackupPaths(const QStringList &targets, const QStringList &backupPaths)
{
    QVariantList restoreOperations;
    const int count = std::min(targets.size(), backupPaths.size());
    for (int i = 0; i < count; ++i) {
        if (backupPaths.at(i).isEmpty() || !QFileInfo::exists(backupPaths.at(i)))
            continue;

        QVariantMap item;
        item["sourcePath"] = backupPaths.at(i);
        item["targetPath"] = targets.at(i);
        item["overwrite"] = false;
        restoreOperations.append(item);
    }

    if (!restoreOperations.isEmpty())
        m_fileOps->moveResolvedItems(restoreOperations);
}
