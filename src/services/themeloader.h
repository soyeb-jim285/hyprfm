#pragma once
#include <QObject>
#include <QColor>
#include <QMap>

class ThemeLoader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor base READ base NOTIFY themeChanged)
    Q_PROPERTY(QColor mantle READ mantle NOTIFY themeChanged)
    Q_PROPERTY(QColor crust READ crust NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor overlay READ overlay NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY themeChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor error READ error NOTIFY themeChanged)

public:
    explicit ThemeLoader(QObject *parent = nullptr);
    void loadTheme(const QString &nameOrPath, const QString &themesDir);
    QColor color(const QString &name) const;
    QColor base() const { return color("base"); }
    QColor mantle() const { return color("mantle"); }
    QColor crust() const { return color("crust"); }
    QColor surface() const { return color("surface"); }
    QColor overlay() const { return color("overlay"); }
    QColor text() const { return color("text"); }
    QColor subtext() const { return color("subtext"); }
    QColor muted() const { return color("muted"); }
    QColor accent() const { return color("accent"); }
    QColor success() const { return color("success"); }
    QColor warning() const { return color("warning"); }
    QColor error() const { return color("error"); }
signals:
    void themeChanged();
private:
    QMap<QString, QColor> m_colors;
    static QMap<QString, QColor> s_defaults;
};
