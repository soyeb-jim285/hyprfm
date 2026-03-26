#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class FileOperations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit FileOperations(QObject *parent = nullptr);

    bool busy() const;
    double progress() const;
    QString statusText() const;

    Q_INVOKABLE void copyFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void moveFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void trashFiles(const QStringList &paths);
    Q_INVOKABLE void deleteFiles(const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &path, const QString &newName);
    Q_INVOKABLE void createFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE void createFile(const QString &parentPath, const QString &name);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openFileWith(const QString &path, const QString &desktopFile);
    Q_INVOKABLE void copyPathToClipboard(const QString &path);
    Q_INVOKABLE void openInTerminal(const QString &dirPath);

signals:
    void busyChanged();
    void progressChanged(double progress, const QString &speed, const QString &eta);
    void statusTextChanged();
    void operationFinished(bool success, const QString &error);

private:
    void runProcess(const QString &program, const QStringList &args);
    void parseRsyncProgress(const QByteArray &data);

    QProcess *m_process = nullptr;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusText;
};
