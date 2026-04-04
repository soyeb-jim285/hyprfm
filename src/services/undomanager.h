#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QStack>

class FileOperations;

struct UndoRecord {
    enum Type { Copy, Move, Rename, Trash, CreateFolder, CreateFile };
    Type type;
    QStringList sourcePaths;   // original locations
    QString destination;       // dest dir (copy/move)
    QString oldName;           // rename: original name
    QString newName;           // rename: new name
    QStringList createdPaths;  // paths that were created by the operation
    QStringList backupPaths;   // temporary backups for overwritten destinations
};

class UndoManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)

public:
    explicit UndoManager(FileOperations *fileOps, QObject *parent = nullptr);

    bool canUndo() const;
    bool canRedo() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Undoable wrappers around FileOperations
    Q_INVOKABLE void copyFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void copyResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void moveFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void moveResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void trashFiles(const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &path, const QString &newName);
    Q_INVOKABLE QVariantMap renameResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void createFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE void createFile(const QString &parentPath, const QString &name);

signals:
    void stackChanged();

private:
    void executeUndo(const UndoRecord &record);
    void executeRedo(const UndoRecord &record);
    void record(const UndoRecord &rec);
    void restoreFromTrash(const QStringList &originalPaths);
    void restoreBackupPaths(const QStringList &targets, const QStringList &backupPaths);
    static QStringList computeCreatedPaths(const QStringList &sources, const QString &dest);

    FileOperations *m_fileOps;
    QStack<UndoRecord> m_undoStack;
    QStack<UndoRecord> m_redoStack;
    bool m_isUndoRedo = false;
    static constexpr int kMaxUndoDepth = 50;
};
