#pragma once
#include <QObject>

// Session-scoped view state persisted in session.json (never config.toml).
// Currently just the zoom remembered for each view so a restart keeps the
// icon size / column count / row height the user left them at. Values default to 0,
// meaning "nothing saved yet": the views then keep their built-in defaults.
class SessionState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY gridColumnsChanged)
    Q_PROPERTY(int gridIconSize READ gridIconSize WRITE setGridIconSize NOTIFY gridIconSizeChanged)
    Q_PROPERTY(int rowHeightDetailed READ rowHeightDetailed WRITE setRowHeightDetailed NOTIFY rowHeightDetailedChanged)
    Q_PROPERTY(int rowHeightMiller READ rowHeightMiller WRITE setRowHeightMiller NOTIFY rowHeightMillerChanged)
    Q_PROPERTY(int rowHeightCompact READ rowHeightCompact WRITE setRowHeightCompact NOTIFY rowHeightCompactChanged)

public:
    explicit SessionState(QObject *parent = nullptr) : QObject(parent) {}

    int gridColumns() const { return m_gridColumns; }
    void setGridColumns(int value);

    // Grid icon size in logical px (zoom without fixed columns).
    int gridIconSize() const { return m_gridIconSize; }
    void setGridIconSize(int value);

    // Each pane zooms on its own and writes what it shows here, so a new tab
    // or the next start begins from the last zoom used; panes do not follow
    // each other's writes. The settings sliders do mean every pane: they
    // call this after writing a value.
    Q_INVOKABLE void applyZoomToAllPanes() { emit zoomAppliedToAllPanes(); }

    int rowHeightDetailed() const { return m_rowHeightDetailed; }
    void setRowHeightDetailed(int value);

    int rowHeightMiller() const { return m_rowHeightMiller; }
    void setRowHeightMiller(int value);

    int rowHeightCompact() const { return m_rowHeightCompact; }
    void setRowHeightCompact(int value);

signals:
    void gridColumnsChanged();
    void gridIconSizeChanged();
    void zoomAppliedToAllPanes();
    void rowHeightDetailedChanged();
    void rowHeightMillerChanged();
    void rowHeightCompactChanged();

private:
    int m_gridColumns = 0;
    int m_gridIconSize = 0;
    int m_rowHeightDetailed = 0;
    int m_rowHeightMiller = 0;
    int m_rowHeightCompact = 0;
};
