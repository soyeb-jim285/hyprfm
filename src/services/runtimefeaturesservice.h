#pragma once

#include <QObject>

class RuntimeFeaturesService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
    Q_PROPERTY(bool batAvailable READ batAvailable CONSTANT)
    Q_PROPERTY(bool udisksctlAvailable READ udisksctlAvailable CONSTANT)
    Q_PROPERTY(bool wlClipboardAvailable READ wlClipboardAvailable CONSTANT)
    Q_PROPERTY(bool gitAvailable READ gitAvailable CONSTANT)

public:
    explicit RuntimeFeaturesService(QObject *parent = nullptr);

    bool ffmpegAvailable() const;
    bool batAvailable() const;
    bool udisksctlAvailable() const;
    bool wlClipboardAvailable() const;
    bool gitAvailable() const;

    Q_INVOKABLE QString installHint(const QString &feature) const;

private:
    static bool hasExecutable(const QString &name);
};
