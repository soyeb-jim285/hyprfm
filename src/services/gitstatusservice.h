#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QTimer>

class GitStatusService : public QObject
{
    Q_OBJECT

public:
    explicit GitStatusService(QObject *parent = nullptr);

    void setRootPath(const QString &path);
    QString statusForPath(const QString &path) const;
    bool isGitRepo() const;

signals:
    void statusChanged();

private:
    void startFindRepoRoot(const QString &path);
    void queryGitStatus();
    void scheduleGitStatus();
    void stopProcesses();
    void onGitStatusFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onGitIndexChanged(const QString &path);
    void markParentsDirty(const QString &filePath);
    bool isRemotePath(const QString &path) const;

    QString m_rootPath;
    QString m_repoRoot;
    QHash<QString, QString> m_statusCache;
    QHash<QString, bool> m_dirtyDirs;
    QProcess *m_repoProcess = nullptr;
    QProcess *m_gitProcess = nullptr;
    QFileSystemWatcher *m_indexWatcher = nullptr;
    // Coalesces bursts of index changes (rebase, stash apply, large stage) into
    // a single `git status` run. Without this, each write to .git/index kicks
    // off another query and forces FileSystemModel to invalidate GitStatus*
    // roles for every visible row.
    QTimer m_statusDebounce;
    bool m_gitAvailable = false;
};
