#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QFileSystemWatcher>

class GitStatusService : public QObject
{
    Q_OBJECT

public:
    explicit GitStatusService(QObject *parent = nullptr);

    void setRootPath(const QString &path);
    QString statusForPath(const QString &path) const;

signals:
    void statusChanged();

private:
    QString findRepoRoot(const QString &path) const;
    void queryGitStatus();
    void onGitStatusFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onGitIndexChanged(const QString &path);
    void markParentsDirty(const QString &filePath);
    bool isRemotePath(const QString &path) const;

    QString m_rootPath;
    QString m_repoRoot;
    QHash<QString, QString> m_statusCache;
    QHash<QString, bool> m_dirtyDirs;
    QProcess *m_gitProcess = nullptr;
    QFileSystemWatcher *m_indexWatcher = nullptr;
    bool m_gitAvailable = false;
};
