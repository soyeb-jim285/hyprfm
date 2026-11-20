#include "services/sessionstate.h"

void SessionState::setGridColumns(int value)
{
    if (m_gridColumns == value) return;
    m_gridColumns = value;
    emit gridColumnsChanged();
}

void SessionState::setGridIconSize(int value)
{
    if (m_gridIconSize == value) return;
    m_gridIconSize = value;
    emit gridIconSizeChanged();
}

void SessionState::setRowHeightDetailed(int value)
{
    if (m_rowHeightDetailed == value) return;
    m_rowHeightDetailed = value;
    emit rowHeightDetailedChanged();
}

void SessionState::setRowHeightMiller(int value)
{
    if (m_rowHeightMiller == value) return;
    m_rowHeightMiller = value;
    emit rowHeightMillerChanged();
}

void SessionState::setRowHeightCompact(int value)
{
    if (m_rowHeightCompact == value) return;
    m_rowHeightCompact = value;
    emit rowHeightCompactChanged();
}
