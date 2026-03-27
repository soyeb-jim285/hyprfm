// tests/testdir.h
#pragma once

#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

// Dolphin-inspired test fixture: auto-cleanup temporary directory with
// convenience methods for creating known file hierarchies.
class TestDir : public QTemporaryDir
{
public:
    TestDir() : QTemporaryDir() {}

    // Create a file with optional content and timestamp
    QString createFile(const QString &relativePath,
                       const QByteArray &content = QByteArray(),
                       const QDateTime &modified = QDateTime())
    {
        QString fullPath = path() + "/" + relativePath;
        QFileInfo info(fullPath);
        QDir().mkpath(info.absolutePath());

        QFile f(fullPath);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();

        if (modified.isValid()) {
            f.open(QIODevice::ReadWrite);
            f.setFileTime(modified, QFileDevice::FileModificationTime);
            f.close();
        }
        return fullPath;
    }

    // Create multiple empty files
    void createFiles(const QStringList &relativePaths)
    {
        for (const auto &p : relativePaths)
            createFile(p);
    }

    // Create a directory (returns absolute path)
    QString createDir(const QString &relativePath)
    {
        QString fullPath = path() + "/" + relativePath;
        QDir().mkpath(fullPath);
        return fullPath;
    }

    // Create a symlink
    QString createSymlink(const QString &target, const QString &linkRelPath)
    {
        QString linkPath = path() + "/" + linkRelPath;
        QFileInfo info(linkPath);
        QDir().mkpath(info.absolutePath());
        QFile::link(target, linkPath);
        return linkPath;
    }

    // Remove a file
    void removeFile(const QString &relativePath)
    {
        QFile::remove(path() + "/" + relativePath);
    }

    // Check if file exists
    bool exists(const QString &relativePath) const
    {
        return QFile::exists(path() + "/" + relativePath);
    }
};
