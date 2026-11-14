#include "terminal/terminalview.h"

#include <QClipboard>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QBuffer>
#include <QImageReader>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QSysInfo>
#include <QRegularExpression>
#include <QStyleHints>
#include <QUrl>
#include <QUrlQuery>
#include <QVarLengthArray>

// Ghostty structs have an `emit` field; this file signals with Q_EMIT.
#undef emit

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <poll.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int Padding = 6;

GhosttyKey ghosttyKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return GhosttyKey(GHOSTTY_KEY_A + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return GhosttyKey(GHOSTTY_KEY_DIGIT_0 + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return GhosttyKey(GHOSTTY_KEY_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space: return GHOSTTY_KEY_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter: return GHOSTTY_KEY_ENTER;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return GHOSTTY_KEY_TAB;
    case Qt::Key_Backspace: return GHOSTTY_KEY_BACKSPACE;
    case Qt::Key_Delete: return GHOSTTY_KEY_DELETE;
    case Qt::Key_Escape: return GHOSTTY_KEY_ESCAPE;
    case Qt::Key_Up: return GHOSTTY_KEY_ARROW_UP;
    case Qt::Key_Down: return GHOSTTY_KEY_ARROW_DOWN;
    case Qt::Key_Left: return GHOSTTY_KEY_ARROW_LEFT;
    case Qt::Key_Right: return GHOSTTY_KEY_ARROW_RIGHT;
    case Qt::Key_Home: return GHOSTTY_KEY_HOME;
    case Qt::Key_End: return GHOSTTY_KEY_END;
    case Qt::Key_PageUp: return GHOSTTY_KEY_PAGE_UP;
    case Qt::Key_PageDown: return GHOSTTY_KEY_PAGE_DOWN;
    case Qt::Key_Insert: return GHOSTTY_KEY_INSERT;
    case Qt::Key_Minus: return GHOSTTY_KEY_MINUS;
    case Qt::Key_Equal: return GHOSTTY_KEY_EQUAL;
    case Qt::Key_BracketLeft: return GHOSTTY_KEY_BRACKET_LEFT;
    case Qt::Key_BracketRight: return GHOSTTY_KEY_BRACKET_RIGHT;
    case Qt::Key_Backslash: return GHOSTTY_KEY_BACKSLASH;
    case Qt::Key_Semicolon: return GHOSTTY_KEY_SEMICOLON;
    case Qt::Key_Apostrophe: return GHOSTTY_KEY_QUOTE;
    case Qt::Key_Comma: return GHOSTTY_KEY_COMMA;
    case Qt::Key_Period: return GHOSTTY_KEY_PERIOD;
    case Qt::Key_Slash: return GHOSTTY_KEY_SLASH;
    case Qt::Key_QuoteLeft: return GHOSTTY_KEY_BACKQUOTE;
    default: return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

GhosttyMods ghosttyMods(Qt::KeyboardModifiers mods)
{
    GhosttyMods out = 0;
    if (mods & Qt::ShiftModifier)
        out |= GHOSTTY_MODS_SHIFT;
    if (mods & Qt::ControlModifier)
        out |= GHOSTTY_MODS_CTRL;
    if (mods & Qt::AltModifier)
        out |= GHOSTTY_MODS_ALT;
    if (mods & Qt::MetaModifier)
        out |= GHOSTTY_MODS_SUPER;
    return out;
}

GhosttyColorRgb rgb(const QColor &c)
{
    return GhosttyColorRgb{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())};
}

QColor qcolor(const GhosttyColorRgb &c)
{
    return QColor(c.r, c.g, c.b);
}

QString s_desktopFamily;
qreal s_desktopPointSize = 0;

// The desktop's monospace font, as GTK terminals use it. Without one, Qt's
// fixed font, or fontconfig's monospace when the platform theme answers with
// a proportional one (HyprFM's does: Noto Sans), which would size every cell
// by a wide M. Qt's fixed font comes from qt6ct here, which can disagree
// with the desktop (Maple Mono 13 against Adwaita Mono 11).
QFont defaultTerminalFont()
{
    if (!s_desktopFamily.isEmpty()) {
        QFont font(s_desktopFamily);
        font.setStyleHint(QFont::Monospace);
        if (s_desktopPointSize > 0)
            font.setPointSizeF(s_desktopPointSize);
        if (QFontInfo(font).fixedPitch())
            return font;
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        const qreal size = font.pointSizeF();
        font = QFont(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        if (size > 0)
            font.setPointSizeF(size);
    }
    return font;
}

// An SGR 4 underline from x0 to x1 under `baseline`, kept inside the cell
// (`bottom`). Curly is what editors use for diagnostics, so it has to read
// as a wave, not a thick line.
void drawUnderline(QPainter *painter, int style, const QColor &color, qreal x0, qreal x1, qreal baseline,
                   qreal bottom, qreal thickness, qreal cellWidth)
{
    const qreal top = qMin(baseline + thickness, bottom - thickness);
    painter->save();
    switch (style) {
    case GHOSTTY_SGR_UNDERLINE_DOUBLE: {
        // Both lines stay in the cell: with little room under the baseline
        // (small fonts) the pair moves up rather than into the next row.
        const qreal gap = qMax(2.0, thickness);
        const qreal first = qMin(top, bottom - 2 * thickness - gap);
        painter->fillRect(QRectF(x0, first, x1 - x0, thickness), color);
        painter->fillRect(QRectF(x0, first + thickness + gap, x1 - x0, thickness), color);
        break;
    }
    case GHOSTTY_SGR_UNDERLINE_CURLY: {
        // The wave fills the descender space below the baseline, one
        // period a cell, as kitty and Ghostty draw it.
        const qreal amplitude = qMax(1.5, (bottom - baseline - thickness) / 2);
        const qreal mid = baseline + (bottom - baseline) / 2;
        const qreal period = qMax(4.0, cellWidth);
        QPainterPath wave(QPointF(x0, mid));
        // A quadratic curve peaks halfway to its control point, so the
        // control sits at twice the amplitude.
        for (qreal x = x0; x < x1; x += period) {
            const qreal half = period / 2;
            wave.quadTo(x + half / 2, mid - 2 * amplitude, qMin(x + half, x1), mid);
            if (x + half < x1)
                wave.quadTo(x + half + half / 2, mid + 2 * amplitude, qMin(x + period, x1), mid);
        }
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipRect(QRectF(x0, baseline - thickness, x1 - x0, bottom - baseline + thickness));
        painter->setPen(QPen(color, thickness, Qt::SolidLine, Qt::FlatCap));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(wave);
        break;
    }
    case GHOSTTY_SGR_UNDERLINE_DOTTED:
    case GHOSTTY_SGR_UNDERLINE_DASHED: {
        // Dash lengths in whole pixels from x = 0, so a run split in two
        // still lines up with its neighbour.
        const qreal on = style == GHOSTTY_SGR_UNDERLINE_DOTTED ? thickness : 3 * thickness;
        const qreal step = 2 * on;
        for (qreal x = std::floor(x0 / step) * step; x < x1; x += step) {
            const qreal a = qMax(x, x0), b = qMin(x + on, x1);
            if (b > a)
                painter->fillRect(QRectF(a, top, b - a, thickness), color);
        }
        break;
    }
    default:
        painter->fillRect(QRectF(x0, top, x1 - x0, thickness), color);
    }
    painter->restore();
}

QString ghosttyString(GhosttyTerminal terminal, GhosttyTerminalData data)
{
    GhosttyString s{};
    if (ghostty_terminal_get(terminal, data, &s) != GHOSTTY_SUCCESS || !s.ptr)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char *>(s.ptr), qsizetype(s.len));
}

// Kitty images per screen (main and alternate each get this much). A PNG
// is refused before decoding if its pixels would not fit, so a small file
// claiming to be huge costs nothing.
constexpr uint64_t KittyImageLimit = 64 * 1024 * 1024;

// Kitty graphics PNGs, decoded by Qt into the RGBA rows Ghostty wants.
// The size is checked before decoding: a tiny PNG can claim to be huge,
// and Ghostty's allocator refuses what is over the storage limit anyway.
bool decodePng(void *, const GhosttyAllocator *allocator, const uint8_t *data, size_t length,
               GhosttySysImage *out)
{
    QByteArray bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(data), qsizetype(length));
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer, "png");
    const QSize size = reader.size();
    if (!size.isValid() || uint64_t(size.width()) * uint64_t(size.height()) * 4 > KittyImageLimit)
        return false;
    reader.setAllocationLimit(int(KittyImageLimit / (1024 * 1024)));
    const QImage image = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        return false;
    const size_t rowBytes = size_t(image.width()) * 4;
    const size_t total = rowBytes * size_t(image.height());
    auto *pixels = static_cast<uint8_t *>(ghostty_alloc(allocator, total));
    if (!pixels)
        return false;
    for (int y = 0; y < image.height(); ++y)
        memcpy(pixels + y * rowBytes, image.constScanLine(y), rowBytes);
    out->width = uint32_t(image.width());
    out->height = uint32_t(image.height());
    out->data = pixels;
    out->data_len = total;
    return true;
}

} // namespace

qreal TerminalView::defaultFontPointSize()
{
    return defaultTerminalFont().pointSizeF();
}

TerminalView::TerminalView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_font(defaultTerminalFont())
{
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setActiveFocusOnTab(true);
    updateCellSize();
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_cursorShown = !m_cursorShown;
        update();
    });

    ghostty_terminal_new(nullptr, &m_terminal, m_cols, m_rowCount);
    ghostty_render_state_new(nullptr, &m_renderState);
    ghostty_render_state_row_iterator_new(nullptr, &m_rows);
    ghostty_render_state_row_cells_new(nullptr, &m_cells);
    ghostty_key_encoder_new(nullptr, &m_keyEncoder);
    ghostty_key_event_new(nullptr, &m_keyEvent);
    ghostty_selection_gesture_new(nullptr, &m_gesture);
    ghostty_selection_gesture_event_new(nullptr, &m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_PRESS);
    ghostty_selection_gesture_event_new(nullptr, &m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_DRAG);
    ghostty_selection_gesture_event_new(nullptr, &m_releaseEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_RELEASE);
    ghostty_mouse_encoder_new(nullptr, &m_mouseEncoder);
    ghostty_mouse_event_new(nullptr, &m_mouseEvent);
    setCursor(Qt::IBeamCursor);
    setAcceptHoverEvents(true);   // motion reports for programs that track it

    const size_t scrollback = size_t(m_scrollbackLines);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollback);
    // Grapheme clustering (mode 2027) on, as kitty and Ghostty run: flags,
    // skin tones and ZWJ emoji are one character, not two to four.
    const GhosttyTerminalModeConfig graphemes = {GHOSTTY_MODE_GRAPHEME_CLUSTER, true};
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_MODE_DEFAULT, &graphemes);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                         reinterpret_cast<const void *>(&TerminalView::onWritePty));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SIZE,
                         reinterpret_cast<const void *>(&TerminalView::onSize));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                         reinterpret_cast<const void *>(&TerminalView::onTitleChanged));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_PWD_CHANGED,
                         reinterpret_cast<const void *>(&TerminalView::onPwdChanged));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_BELL,
                         reinterpret_cast<const void *>(&TerminalView::onBell));
    // Programs may set the clipboard (OSC 52; vim, tmux and neovim over ssh
    // copy this way), as kitty and Ghostty allow by default. Reading it is
    // not offered: no read callback is installed, so a program asking gets
    // nothing back, and any program could otherwise lift whatever the user
    // last copied. Kitty clipboard transactions are held in memory until
    // complete, so their size is capped well below the 64 MiB default.
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE,
                         reinterpret_cast<const void *>(&TerminalView::onClipboardWrite));
    const size_t clipboardLimit = 8 * 1024 * 1024;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE_MAX_BYTES, &clipboardLimit);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DESKTOP_NOTIFICATION,
                         reinterpret_cast<const void *>(&TerminalView::onDesktopNotification));
    // Kitty graphics (kitten icat, yazi, chafa, timg...). Images only come
    // inline with the output: a program asking the terminal to read a file
    // or shared memory is refused, so `cat`ing something hostile cannot
    // make it open local files.
    static const bool pngDecoder = [] {
        ghostty_sys_set(GHOSTTY_SYS_OPT_DECODE_PNG, reinterpret_cast<const void *>(&decodePng));
        return true;
    }();
    Q_UNUSED(pngDecoder);
    const uint64_t imageLimit = KittyImageLimit;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT, &imageLimit);
    const bool off = false;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_FILE, &off);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_TEMP_FILE, nullptr);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM, &off);
    ghostty_render_state_image_iterator_new(nullptr, &m_images);
    m_clock.start();
    m_animationTimer.setSingleShot(true);
    connect(&m_animationTimer, &QTimer::timeout, this, [this] { update(); });
    m_searchRefresh.setSingleShot(true);
    m_searchRefresh.setInterval(16);
    connect(&m_searchRefresh, &QTimer::timeout, this, &TerminalView::refreshSearch);
    // Shells that do not report their folder (OSC 7: plain bash and zsh)
    // still get the right tab title: after output settles, the folder is
    // read from /proc, which is where a cd shows up.
    m_directoryRefresh.setSingleShot(true);
    m_directoryRefresh.setInterval(300);
    connect(&m_directoryRefresh, &QTimer::timeout, this, [this] {
        const QString dir = currentDirectory();
        if (!m_shellReportsDirectory && dir != m_shellDirectory) {
            m_shellDirectory = dir;
            Q_EMIT shellDirectoryChanged();
        }
    });
    m_bellTimer.setSingleShot(true);
    m_bellTimer.setInterval(120);
    connect(&m_bellTimer, &QTimer::timeout, this, [this] { update(); });
    applyColors();
}

TerminalView::~TerminalView()
{
    stop();
    ghostty_search_free(m_search);
    ghostty_mouse_event_free(m_mouseEvent);
    ghostty_mouse_encoder_free(m_mouseEncoder);
    ghostty_selection_gesture_event_free(m_releaseEvent);
    ghostty_selection_gesture_event_free(m_dragEvent);
    ghostty_selection_gesture_event_free(m_pressEvent);
    ghostty_selection_gesture_free(m_gesture, m_terminal);
    ghostty_key_event_free(m_keyEvent);
    ghostty_key_encoder_free(m_keyEncoder);
    ghostty_render_state_row_cells_free(m_cells);
    ghostty_render_state_image_iterator_free(m_images);
    ghostty_render_state_row_iterator_free(m_rows);
    ghostty_render_state_free(m_renderState);
    ghostty_terminal_free(m_terminal);
}

void TerminalView::setWorkingDirectory(const QString &dir)
{
    if (m_workingDirectory == dir)
        return;
    m_workingDirectory = dir;
    Q_EMIT workingDirectoryChanged();
}

void TerminalView::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    updateCellSize();
    resizeGrid();
    Q_EMIT fontChanged();
    update();
}

void TerminalView::setFontFamily(const QString &family)
{
    if (m_fontFamily == family)
        return;
    m_fontFamily = family;
    applyFontOverrides();
}

void TerminalView::setFontSize(qreal size)
{
    if (qFuzzyCompare(m_fontSize, size))
        return;
    m_fontSize = size;
    applyFontOverrides();
}

void TerminalView::setLigatures(bool on)
{
    if (m_ligatures == on)
        return;
    m_ligatures = on;
    updateCellSize();
    Q_EMIT fontChanged();
    update();
}

void TerminalView::setDesktopFont(const QString &family, qreal pointSize)
{
    s_desktopFamily = family;
    s_desktopPointSize = pointSize;
}

void TerminalView::setCursorStyle(const QString &style)
{
    if (m_cursorStyle == style)
        return;
    m_cursorStyle = style;
    applyCursorDefaults();
}

void TerminalView::setCursorBlink(bool on)
{
    if (m_cursorBlink == on)
        return;
    m_cursorBlink = on;
    applyCursorDefaults();
    updateBlinkTimer();
}

void TerminalView::setScrollbackLines(int lines)
{
    lines = qMax(0, lines);
    if (m_scrollbackLines == lines)
        return;
    m_scrollbackLines = lines;
    // Lowering it drops the oldest history at once.
    const size_t max = size_t(lines);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &max);
    if (!m_searchText.isEmpty())
        m_searchRefresh.start();   // matches in the dropped history go
    updateScrollbar();
    Q_EMIT scrollbackLinesChanged();
    update();
}

void TerminalView::applyCursorDefaults()
{
    GhosttyTerminalCursorStyle shape = GHOSTTY_TERMINAL_CURSOR_STYLE_BLOCK;
    if (m_cursorStyle == QLatin1String("bar"))
        shape = GHOSTTY_TERMINAL_CURSOR_STYLE_BAR;
    else if (m_cursorStyle == QLatin1String("underline"))
        shape = GHOSTTY_TERMINAL_CURSOR_STYLE_UNDERLINE;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DEFAULT_CURSOR_STYLE, &shape);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DEFAULT_CURSOR_BLINK, &m_cursorBlink);
    // The defaults only apply on a reset (CSI 0 q), so reset now.
    static const char reset[] = "\x1b[0 q";
    ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const uint8_t *>(reset), sizeof reset - 1);
    Q_EMIT cursorStyleChanged();
    update();
}

// paint() runs on the render thread, so it cannot start a timer; this runs
// on focus and setting changes instead. A program that turns blinking on
// while the setting is off (CSI 1 q) gets a steady cursor: the timer is not
// running to drive it.
void TerminalView::updateBlinkTimer()
{
    const int flashTime = QGuiApplication::styleHints()->cursorFlashTime();
    m_cursorShown = true;
    if (m_cursorBlink && hasActiveFocus() && flashTime > 0)
        m_blinkTimer.start(flashTime / 2);
    else
        m_blinkTimer.stop();
    update();
}

void TerminalView::applyFontOverrides()
{
    QFont font = defaultTerminalFont();
    if (!m_fontFamily.isEmpty())
        font.setFamily(m_fontFamily);
    if (m_fontSize > 0 || m_zoom != 0) {
        const qreal base = m_fontSize > 0 ? m_fontSize : font.pointSizeF();
        if (base > 0) {
            const qreal size = qBound(4.0, base + m_zoom, 72.0);
            m_zoom = size - base;   // so zooming back from a limit starts at once
            font.setPointSizeF(size);
        } else {
            m_zoom = 0;   // a pixel-sized font has no point size to add to
        }
    }
    setFont(font);
}

void TerminalView::setForeground(const QColor &color)
{
    if (m_foreground == color)
        return;
    m_foreground = color;
    applyColors();
    Q_EMIT colorsChanged();
}

void TerminalView::setBackground(const QColor &color)
{
    if (m_background == color)
        return;
    m_background = color;
    applyColors();
    Q_EMIT colorsChanged();
}

void TerminalView::setAnsiPalette(const QVariantList &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    applyColors();
    Q_EMIT colorsChanged();
}

void TerminalView::applyColors()
{
    const GhosttyColorRgb fg = rgb(m_foreground);
    const GhosttyColorRgb bg = rgb(m_background);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
    // The theme sets the 16 ANSI colours; the 256-colour cube and grey ramp
    // above them stay Ghostty's. Colours a program set itself (OSC 4) win.
    if (m_palette.size() == 16) {
        GhosttyColorRgb palette[256];
        ghostty_color_palette_default(palette);
        for (int i = 0; i < 16; ++i)
            palette[i] = rgb(m_palette.at(i).value<QColor>());
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, palette);
    } else {
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, nullptr);
    }
    // A translucent background lets the compositor's blur show through;
    // paint() fills with it, so the item's own fill stays clear.
    setOpaquePainting(m_background.alpha() == 255);
    setFillColor(Qt::transparent);
    update();
}

void TerminalView::updateCellSize()
{
    // Cells are whole pixels, or columns drift apart across a wide line.
    const QFontMetricsF metrics(m_font);
    m_cellWidth = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    m_cellHeight = qCeil(metrics.height());
    m_ascent = metrics.ascent();
    m_capHeight = metrics.capHeight();

    // Runs of cells are drawn as one string, so every glyph has to advance
    // exactly one cell: letter spacing makes up the gap to the whole-pixel
    // cell, and kerning is off, since it would move glyphs off their columns.
    // Ligatures are off unless asked for: monospace ligature fonts (Fira
    // Code, JetBrains Mono, Maple Mono) keep one glyph per cell, others may
    // draw a few pixels off the grid.
    for (int variant = 0; variant < 4; ++variant) {
        QFont font = m_font;
        font.setBold(variant & 1);
        font.setItalic(variant & 2);
        font.setKerning(false);
        if (!m_ligatures) {
            font.setFeature(QFont::Tag("liga"), 0);
            font.setFeature(QFont::Tag("calt"), 0);
            font.setFeature(QFont::Tag("dlig"), 0);
        }
        font.setLetterSpacing(QFont::AbsoluteSpacing,
                              m_cellWidth - QFontMetricsF(font).horizontalAdvance(QLatin1Char('M')));
        m_cellFonts[variant] = font;
    }
}

void TerminalView::resizeGrid()
{
    const auto cols = uint16_t(qMax(2, int((width() - 2 * Padding) / m_cellWidth)));
    const auto rows = uint16_t(qMax(1, int((height() - 2 * Padding) / m_cellHeight)));
    if (cols == m_cols && rows == m_rowCount)
        return;
    m_cols = cols;
    m_rowCount = rows;
    ghostty_terminal_resize(m_terminal, cols, rows, uint32_t(m_cellWidth), uint32_t(m_cellHeight));
    if (m_ptyFd >= 0) {
        winsize ws{rows, cols, uint16_t(cols * m_cellWidth), uint16_t(rows * m_cellHeight)};
        ioctl(m_ptyFd, TIOCSWINSZ, &ws);
    }
    if (!m_searchText.isEmpty())
        m_searchRefresh.start();   // rewrapping moves and merges matches
    updateScrollbar();
    update();
}

void TerminalView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        resizeGrid();
}

void TerminalView::start()
{
    if (m_ptyFd >= 0)
        return;
    resizeGrid();

    QByteArray shell = qgetenv("SHELL");
    if (shell.isEmpty()) {
        const passwd *pw = getpwuid(getuid());
        shell = pw && pw->pw_shell && *pw->pw_shell ? QByteArray(pw->pw_shell) : QByteArray("/bin/sh");
    }
    const QByteArray dir = QFile::encodeName(m_workingDirectory);
    // A Flatpak sandbox has none of the user's programs, so the shell runs on
    // the host. flatpak-spawn hands the pty over as its stdin/out/err.
    static const bool inFlatpak = QFile::exists(QStringLiteral("/.flatpak-info"));
    const QByteArray hostDir = "--directory=" + (dir.isEmpty() ? QByteArray("/") : dir);
    winsize ws{m_rowCount, m_cols, uint16_t(m_cols * m_cellWidth), uint16_t(m_rowCount * m_cellHeight)};

    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0)
        return;
    if (pid == 0) {
        // Child: only async-signal-safe calls from here to exec. Nothing of
        // HyprFM's (its IPC socket, D-Bus, inotify) may leak into the shell,
        // and the shell must start with default signal handling: HyprFM
        // started by `hyprfm &` from a script ignores SIGINT and SIGQUIT,
        // and a shell that inherits that can never Ctrl+C its programs.
        closefrom(3);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig = 1; sig < NSIG; ++sig)
            signal(sig, SIG_DFL);
        if (!dir.isEmpty() && chdir(dir.constData()) != 0)
            (void)chdir("/");
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        unsetenv("COLUMNS");
        unsetenv("LINES");
        // This is not the terminal HyprFM was started from: a shell that
        // thinks it is inside tmux wraps everything for it (kitten icat,
        // yazi), and one claiming kitty or WezTerm would steer programs to
        // the wrong features or remote-control the other terminal.
        for (const char *var : {"TMUX", "TMUX_PANE", "STY", "TERM_PROGRAM", "TERM_PROGRAM_VERSION",
                                "VTE_VERSION", "KITTY_WINDOW_ID", "KITTY_PID", "KITTY_LISTEN_ON",
                                "WEZTERM_PANE", "ALACRITTY_WINDOW_ID", "KONSOLE_VERSION"})
            unsetenv(var);
        if (inFlatpak) {
            // forkpty made the pty our controlling tty. Let it go, so the host
            // side can take it: without one the shell has no job control and
            // Ctrl+C reaches nothing. Letting go hangs up our own group.
            signal(SIGHUP, SIG_IGN);
            ioctl(STDIN_FILENO, TIOCNOTTY);
            signal(SIGHUP, SIG_DFL);
            execlp("flatpak-spawn", "flatpak-spawn", "--host", "--watch-bus",
                   "--env=TERM=xterm-256color", "--env=COLORTERM=truecolor",
                   hostDir.constData(), "sh", "-c",
                   // Flatpak sets SHELL=/bin/sh in here, so look it up there.
                   // Our pty belongs to the sandbox's /dev/pts, which the host
                   // cannot name (`tty` fails, so GPG_TTY and pinentry do), so
                   // `script` gives the shell a host pty and relays to ours.
                   "s=${SHELL:-$(getent passwd \"$(id -u)\" | cut -d: -f7)}; export SHELL=${s:-/bin/sh}; "
                   "command -v script >/dev/null && exec script -qef /dev/null; exec \"$SHELL\"",
                   static_cast<char *>(nullptr));
            _exit(127);
        }
        const char *name = strrchr(shell.constData(), '/');
        execl(shell.constData(), name ? name + 1 : shell.constData(), static_cast<char *>(nullptr));
        _exit(127);
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    m_ptyFd = fd;
    m_pid = pid;
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &TerminalView::readPty);
    Q_EMIT runningChanged();
}

// For the label showing where a link goes: percent-encoded, so bidi
// overrides, invisible characters and spaces cannot dress up the target.
QString TerminalView::hoveredLink() const
{
    return m_hoverLink.uri.isEmpty() ? QString()
                                      : QString::fromLatin1(QUrl(m_hoverLink.uri).toEncoded());
}

QString TerminalView::currentDirectory() const
{
    // Not the OSC 7 report: any output can claim a folder, and a new shell
    // started in a hostile repository runs its git config via the prompt.
    if (m_pid > 0) {
        const QString cwd = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(m_pid)).symLinkTarget();
        if (!cwd.isEmpty())
            return cwd;
    }
    return m_workingDirectory;
}

void TerminalView::stop()
{
    if (m_ptyFd < 0)
        return;
    delete m_notifier;
    m_notifier = nullptr;
    close(m_ptyFd);
    m_ptyFd = -1;
    if (m_pid > 0) {
        // Closing the pty already hangs the shell up; give it a moment to
        // save its history, then make sure it is gone and reaped.
        kill(pid_t(m_pid), SIGHUP);
        for (int i = 0; i < 20 && waitpid(pid_t(m_pid), nullptr, WNOHANG) == 0; ++i)
            usleep(10000);
        if (waitpid(pid_t(m_pid), nullptr, WNOHANG) == 0) {
            kill(pid_t(m_pid), SIGKILL);
            waitpid(pid_t(m_pid), nullptr, 0);
        }
        m_pid = -1;
    }
    Q_EMIT runningChanged();
}

void TerminalView::readPty()
{
    char buf[16384];
    // The kernel buffers only 4 KB of pty output, so a program printing
    // fast blocks every 4 KB until we read again. Going back to the event
    // loop between refills made `seq 500000` take 1.3 s (0.25 s for a plain
    // reader); waiting up to 1 ms for more, for at most 8 ms per wakeup so
    // frames still go out, brings it to ~0.4 s.
    QElapsedTimer budget;
    budget.start();
    for (;;) {
        const ssize_t n = read(m_ptyFd, buf, sizeof buf);
        if (n > 0) {
            ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const uint8_t *>(buf), size_t(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN) {
            pollfd more{m_ptyFd, POLLIN, 0};
            if (budget.elapsed() < 8 && poll(&more, 1, 1) > 0)
                continue;
            break;
        }
        // EOF, or EIO once the shell has exited.
        stop();
        Q_EMIT finished();
        break;
    }
    // New output may have written over (or written) the link under the mouse.
    if (m_hoverPos.x() >= 0)
        updateHoverLink();
    if (!m_searchText.isEmpty() && !m_searchRefresh.isActive())
        m_searchRefresh.start();
    if (!m_shellReportsDirectory && !m_directoryRefresh.isActive())
        m_directoryRefresh.start();
    updateScrollbar();
    update();
}

void TerminalView::writePty(const char *data, qsizetype len)
{
    while (m_ptyFd >= 0 && len > 0) {
        const ssize_t n = write(m_ptyFd, data, size_t(len));
        if (n > 0) {
            data += n;
            len -= n;
        } else if (n < 0 && errno != EINTR) {
            break;   // ponytail: a full pty buffer drops the rest, as Ghostling does
        }
    }
}

void TerminalView::sendText(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    writePty(utf8.constData(), utf8.size());
}

bool TerminalView::changeDirectory(const QString &dir)
{
    // The shell owns the terminal's foreground process group only while
    // nothing it started is running, so an editor never gets a cd typed in.
    if (m_ptyFd < 0 || tcgetpgrp(m_ptyFd) != pid_t(m_pid))
        return false;
    // The path is typed into the line editor, where control characters are
    // keys: a folder named "x^C^Mrm -rf ~" would cancel the cd and run the
    // rest. Quoting cannot stop that, so such paths are not typed at all.
    for (const QChar c : dir) {
        if (c.category() == QChar::Other_Control)
            return false;
    }
    QString quoted = dir;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    // Ctrl+U drops whatever is half-typed; the leading space keeps the cd
    // out of history in shells that ignore space-prefixed lines.
    sendText(QStringLiteral("\x15 cd -- '%1'\r").arg(quoted));
    return true;
}

void TerminalView::onWritePty(GhosttyTerminal, void *self, const uint8_t *data, size_t len)
{
    static_cast<TerminalView *>(self)->writePty(reinterpret_cast<const char *>(data), qsizetype(len));
}

bool TerminalView::onSize(GhosttyTerminal, void *self, GhosttySizeReportSize *out)
{
    const auto *view = static_cast<TerminalView *>(self);
    out->rows = view->m_rowCount;
    out->columns = view->m_cols;
    out->cell_width = uint32_t(view->m_cellWidth);
    out->cell_height = uint32_t(view->m_cellHeight);
    return true;
}

void TerminalView::onTitleChanged(GhosttyTerminal terminal, void *self)
{
    auto *view = static_cast<TerminalView *>(self);
    view->m_title = ghosttyString(terminal, GHOSTTY_TERMINAL_DATA_TITLE);
    Q_EMIT view->titleChanged();
}

void TerminalView::onPwdChanged(GhosttyTerminal terminal, void *self)
{
    auto *view = static_cast<TerminalView *>(self);
    // OSC 7 sends a file:// URL, though some shells send a bare path. The
    // host is the machine name (or localhost), which toLocalFile() would
    // turn into a //host UNC path, so only the path is kept.
    const QString pwd = ghosttyString(terminal, GHOSTTY_TERMINAL_DATA_PWD);
    const QUrl url(pwd);
    view->m_shellReportsDirectory = true;
    view->m_shellDirectory = url.isLocalFile() ? url.path(QUrl::FullyDecoded) : pwd;
    Q_EMIT view->shellDirectoryChanged();
}

void TerminalView::onBell(GhosttyTerminal, void *self)
{
    auto *view = static_cast<TerminalView *>(self);
    // A program ringing in a loop gets one flash per interval, not a strobe.
    if (view->m_bellTimer.isActive())
        return;
    view->m_bellTimer.start();
    view->update();
    if (QQuickWindow *window = view->window(); window && !window->isActive())
        window->alert(0);
    Q_EMIT view->bell();
}

void TerminalView::onClipboardWrite(GhosttyTerminal, void *, const GhosttyClipboardWrite *write)
{
    GhosttyClipboardWriteReply reply = GHOSTTY_INIT_SIZED(GhosttyClipboardWriteReply);
    reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_UNSUPPORTED;
    QClipboard *clipboard = QGuiApplication::clipboard();
    const QClipboard::Mode mode = write->location == GHOSTTY_CLIPBOARD_LOCATION_STANDARD
                                      ? QClipboard::Clipboard
                                      : QClipboard::Selection;
    if (mode == QClipboard::Selection && !clipboard->supportsSelection()) {
        write->reply(write, &reply);
        return;
    }
    if (write->contents_len == 0) {
        clipboard->clear(mode);
        reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
    }
    // Text only: what OSC 52 carries, and all a terminal program has any
    // business putting on the clipboard.
    for (size_t i = 0; i < write->contents_len; ++i) {
        const GhosttyClipboardContent &content = write->contents[i];
        const QByteArray mime(reinterpret_cast<const char *>(content.mime.ptr), qsizetype(content.mime.len));
        if (!mime.startsWith("text/plain"))
            continue;
        clipboard->setText(QString::fromUtf8(reinterpret_cast<const char *>(content.data.ptr),
                                             qsizetype(content.data.len)),
                           mode);
        reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
        break;
    }
    write->reply(write, &reply);
}

void TerminalView::onDesktopNotification(GhosttyTerminal, void *self,
                                         const GhosttyTerminalDesktopNotification *notification)
{
    auto *view = static_cast<TerminalView *>(self);
    // Anything the shell runs can send these, so a loop must not bury the
    // desktop in notifications: one a second, the rest dropped.
    if (view->m_lastNotification.isValid() && view->m_lastNotification.elapsed() < 1000)
        return;
    view->m_lastNotification.start();
    const auto text = [](const GhosttyString &s, qsizetype max) {
        QString out = QString::fromUtf8(reinterpret_cast<const char *>(s.ptr), qsizetype(s.len)).left(max);
        for (QChar &c : out) {
            if (c.category() == QChar::Other_Control)
                c = QLatin1Char(' ');
        }
        return out.trimmed();
    };
    QString title = text(notification->title, 120);
    const QString body = text(notification->body, 1000);
    if (title.isEmpty() && body.isEmpty())
        return;
    if (title.isEmpty())
        title = view->m_title.isEmpty() ? QStringLiteral("Terminal") : view->m_title.left(120);
    Q_EMIT view->notification(title, body);
    if (!view->m_desktopNotifications)
        return;
    // The body is markup to most notification servers (links, images), so
    // the program's text is escaped; the call is fire and forget.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    call << QStringLiteral("HyprFM") << uint(0) << QStringLiteral("utilities-terminal") << title
         << body.toHtmlEscaped() << QStringList() << QVariantMap() << int(-1);
    QDBusConnection::sessionBus().send(call);
}

bool TerminalView::event(QEvent *event)
{
    // Window shortcuts (Ctrl+C copy, Ctrl+L path bar...) would otherwise
    // fire before the shell sees the key. Claim every key but the ones the
    // window keeps.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QKeySequence pressed(key->keyCombination());
        for (const QString &shortcut : std::as_const(m_windowShortcuts)) {
            if (QKeySequence(shortcut) == pressed)
                return QQuickPaintedItem::event(event);
        }
        event->accept();
        return true;
    }
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateBlinkTimer();   // also repaints: the cursor is filled only while focused
        reportFocus(event->type() == QEvent::FocusIn);
    }
    return QQuickPaintedItem::event(event);
}

// Programs that turned on focus reporting (vim, tmux, neovim) hear CSI I
// and CSI O as the terminal gains and loses focus, the window's included.
void TerminalView::reportFocus(bool focused)
{
    if (m_ptyFd < 0 || focused == m_reportedFocus)
        return;
    m_reportedFocus = focused;
    GhosttyTerminalModeConfig mode{GHOSTTY_MODE_FOCUS_EVENT, false};
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &mode);
    if (!mode.value)
        return;
    char out[8];
    size_t written = 0;
    if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, out, sizeof out, &written)
        == GHOSTTY_SUCCESS)
        writePty(out, qsizetype(written));
}

void TerminalView::sendKey(QKeyEvent *event, GhosttyKeyAction action)
{
    const GhosttyKey key = ghosttyKey(event->key());
    const QString text = action == GHOSTTY_KEY_ACTION_RELEASE ? QString() : event->text();
    // Control characters in text() are Qt's rendering of Ctrl combinations;
    // the encoder builds those itself from the key and modifiers.
    const bool printable = !text.isEmpty() && text.at(0).isPrint();
    const QByteArray utf8 = printable ? text.toUtf8() : QByteArray();

    if (key == GHOSTTY_KEY_UNIDENTIFIED) {
        // Keys Ghostty has no code for (accented letters, ü on a German
        // layout) still type their text.
        if (!utf8.isEmpty())
            writePty(utf8.constData(), utf8.size());
        return;
    }

    const GhosttyMods mods = ghosttyMods(event->modifiers());
    uint32_t unshifted = 0;
    if (event->key() >= 0x20 && event->key() < 0x7f)
        unshifted = QChar(event->key()).toLower().unicode();

    ghostty_key_encoder_setopt_from_terminal(m_keyEncoder, m_terminal);
    ghostty_key_event_set_action(m_keyEvent, action);
    ghostty_key_event_set_key(m_keyEvent, key);
    ghostty_key_event_set_mods(m_keyEvent, mods);
    ghostty_key_event_set_unshifted_codepoint(m_keyEvent, unshifted);
    ghostty_key_event_set_consumed_mods(m_keyEvent,
        unshifted && !utf8.isEmpty() ? (mods & GHOSTTY_MODS_SHIFT) : GhosttyMods(0));
    ghostty_key_event_set_utf8(m_keyEvent, utf8.isEmpty() ? nullptr : utf8.constData(), size_t(utf8.size()));

    char out[128];
    size_t written = 0;
    if (ghostty_key_encoder_encode(m_keyEncoder, m_keyEvent, out, sizeof out, &written) == GHOSTTY_SUCCESS
        && written > 0) {
        writePty(out, qsizetype(written));
    } else if (!utf8.isEmpty()) {
        writePty(utf8.constData(), utf8.size());
    }
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    // Typing shows the cursor and holds off the next blink.
    if (m_blinkTimer.isActive()) {
        m_cursorShown = true;
        m_blinkTimer.start();
    }
    if (m_ptyFd < 0) {
        event->ignore();
        return;
    }
    // Terminal copy and paste keys: Ctrl+C and Ctrl+V belong to the shell.
    const QKeyCombination combo = event->keyCombination();
    if (combo == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C)) {
        copy();
        event->accept();
        return;
    }
    if (combo == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_F)) {
        Q_EMIT searchRequested();
        event->accept();
        return;
    }
    if (combo == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T)) {
        Q_EMIT newTabRequested();
        event->accept();
        return;
    }
    if (combo == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_W)) {
        Q_EMIT closeTabRequested();
        event->accept();
        return;
    }
    if (m_tabSwitching
        && (combo == QKeyCombination(Qt::ControlModifier, Qt::Key_PageDown)
            || combo == QKeyCombination(Qt::ControlModifier, Qt::Key_PageUp))) {
        Q_EMIT switchTabRequested(event->key() == Qt::Key_PageDown ? 1 : -1);
        event->accept();
        return;
    }
    // Zoom for this session, as in Ghostty: Ctrl+= / Ctrl+- / Ctrl+0.
    const Qt::KeyboardModifiers mods = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (mods == Qt::ControlModifier) {
        const int key = event->key();
        if (key == Qt::Key_Equal || key == Qt::Key_Plus || key == Qt::Key_Minus || key == Qt::Key_0) {
            m_zoom = key == Qt::Key_0 ? 0 : m_zoom + (key == Qt::Key_Minus ? -1 : 1);
            applyFontOverrides();
            event->accept();
            return;
        }
    }
    if (combo == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_V)
        || combo == QKeyCombination(Qt::ShiftModifier, Qt::Key_Insert)) {
        paste();
        event->accept();
        return;
    }
    if (scrollKey(event)) {
        event->accept();
        return;
    }
    // A lone modifier is the start of a shortcut such as Ctrl+Shift+C, not
    // typing: keep the selection and the scrollback position for it.
    const int key = event->key();
    const bool modifierOnly = key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_Super_L
        || key == Qt::Key_Super_R || key == Qt::Key_Hyper_L || key == Qt::Key_Hyper_R;
    if (!modifierOnly) {
        if (m_hasSelection)
            clearSelection();
        // Typing jumps back to the prompt from wherever scrollback was.
        ghostty_terminal_scroll_viewport(m_terminal, GhosttyTerminalScrollViewport{
            GHOSTTY_SCROLL_VIEWPORT_BOTTOM, {}});
        updateScrollbar();
    }
    sendKey(event, event->isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    event->accept();
    update();
}

void TerminalView::keyReleaseEvent(QKeyEvent *event)
{
    // Only the kitty keyboard protocol reports releases; the encoder writes
    // nothing for them otherwise.
    if (m_ptyFd >= 0 && !event->isAutoRepeat())
        sendKey(event, GHOSTTY_KEY_ACTION_RELEASE);
    event->accept();
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    // fcitx/ibus hand over composed text here (CJK, compose sequences).
    if (!event->commitString().isEmpty())
        sendText(event->commitString());
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle) {
        uint16_t x = 0, y = 0;
        ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
        ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
        return QRectF(Padding + x * m_cellWidth, Padding + y * m_cellHeight, m_cellWidth, m_cellHeight);
    }
    if (query == Qt::ImEnabled)
        return true;
    return QQuickPaintedItem::inputMethodQuery(query);
}

GhosttyGridRef TerminalView::refAt(QPointF pos) const
{
    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = uint16_t(qBound(0, int((pos.x() - Padding) / m_cellWidth), m_cols - 1));
    point.value.coordinate.y = uint32_t(qBound(0, int((pos.y() - Padding) / m_cellHeight), m_rowCount - 1));
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    ghostty_terminal_grid_ref(m_terminal, point, &ref);
    return ref;
}

GhosttyGridRef TerminalView::refAtCell(int column, int row) const
{
    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = uint16_t(column);
    point.value.coordinate.y = uint32_t(row);
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    ghostty_terminal_grid_ref(m_terminal, point, &ref);
    return ref;
}

namespace {

QString hyperlinkUri(const GhosttyGridRef &ref)
{
    char buf[512];
    size_t len = 0;
    const GhosttyResult result =
        ghostty_grid_ref_hyperlink_uri(&ref, reinterpret_cast<uint8_t *>(buf), sizeof buf, &len);
    if (result == GHOSTTY_SUCCESS)
        return QString::fromUtf8(buf, qsizetype(len));
    if (result != GHOSTTY_OUT_OF_SPACE || len > 8192)
        return {};
    QByteArray big(qsizetype(len), Qt::Uninitialized);
    if (ghostty_grid_ref_hyperlink_uri(&ref, reinterpret_cast<uint8_t *>(big.data()), size_t(big.size()), &len)
        != GHOSTTY_SUCCESS)
        return {};
    return QString::fromUtf8(big.constData(), qsizetype(len));
}

} // namespace

// The link under `pos`: an OSC 8 hyperlink if the program made one, else a
// URL written out in the text of that row. ponytail: a URL wrapped onto
// the next row is found only up to the wrap.
TerminalView::Link TerminalView::linkAt(QPointF pos) const
{
    Link link;
    const int column = int((pos.x() - Padding) / m_cellWidth);
    const int row = int((pos.y() - Padding) / m_cellHeight);
    if (pos.x() < Padding || pos.y() < Padding || column >= m_cols || row >= m_rowCount)
        return link;

    const QString osc8 = hyperlinkUri(refAtCell(column, row));
    if (!osc8.isEmpty()) {
        link.uri = osc8;
        link.row = row;
        link.start = link.end = column;
        while (link.start > 0 && hyperlinkUri(refAtCell(link.start - 1, row)) == osc8)
            --link.start;
        while (link.end + 1 < m_cols && hyperlinkUri(refAtCell(link.end + 1, row)) == osc8)
            ++link.end;
        return link;
    }

    // The row as text, with the column each character came from.
    QString text;
    QList<int> columns;
    uint32_t codepoints[16];
    for (int c = 0; c < m_cols; ++c) {
        const GhosttyGridRef ref = refAtCell(c, row);
        size_t len = 0;
        if (ghostty_grid_ref_graphemes(&ref, codepoints, 16, &len) != GHOSTTY_SUCCESS || len == 0) {
            text += QLatin1Char(' ');
            columns.append(c);
            continue;
        }
        const QString grapheme = QString::fromUcs4(reinterpret_cast<const char32_t *>(codepoints), qsizetype(len));
        for (qsizetype i = 0; i < grapheme.size(); ++i)
            columns.append(c);
        text += grapheme;
    }
    static const QRegularExpression url(QStringLiteral(
        R"((?:https?|ftp|file)://[^\s<>"'`{}|\\^]+|mailto:[^\s<>"'`{}|\\^]+)"),
        QRegularExpression::CaseInsensitiveOption);
    for (auto it = url.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        QString found = match.captured();
        // Sentence punctuation and a closing bracket the URL did not open
        // belong to the text around it.
        while (!found.isEmpty()) {
            const QChar last = found.back();
            if (QStringLiteral(".,;:!?'\"").contains(last)) {
                found.chop(1);
            } else if ((last == QLatin1Char(')') && found.count(QLatin1Char('(')) < found.count(QLatin1Char(')')))
                       || (last == QLatin1Char(']') && found.count(QLatin1Char('[')) < found.count(QLatin1Char(']')))) {
                found.chop(1);
            } else {
                break;
            }
        }
        const qsizetype first = match.capturedStart();
        const qsizetype last = first + found.size() - 1;
        if (found.isEmpty() || columns.at(first) > column || columns.at(last) < column)
            continue;
        link.uri = found;
        link.row = row;
        link.start = columns.at(first);
        link.end = columns.at(last);
        return link;
    }
    return link;
}

void TerminalView::updateHoverLink()
{
    const Link link = m_hoverPos.x() < 0 ? Link() : linkAt(m_hoverPos);
    if (link == m_hoverLink)
        return;
    const bool uriChanged = link.uri != m_hoverLink.uri;
    m_hoverLink = link;
    setCursor(link.row >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
    update();
    if (uriChanged)
        Q_EMIT hoveredLinkChanged();
}

// Output decides what a click on it does, so only harmless kinds of link
// are opened: web and mail links by the desktop, and file:// links to this
// machine by HyprFM itself, which shows the place rather than running what
// is there. A file:// link from an ssh session names the other machine.
bool TerminalView::activateLink(const QString &uri)
{
    if (uri.size() > 8192)
        return false;
    const QUrl url(uri, QUrl::StrictMode);
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("file")) {
        const QString host = url.host();
        if (!host.isEmpty() && host != QLatin1String("localhost")
            && host.compare(QSysInfo::machineHostName(), Qt::CaseInsensitive) != 0)
            return false;
        const QFileInfo info(url.path(QUrl::FullyDecoded));
        if (!info.isAbsolute() || !info.exists())
            return false;
        Q_EMIT folderActivated(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
        return true;
    }
    static const QStringList web{QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("ftp"),
                                 QStringLiteral("mailto")};
    if (!web.contains(scheme) || (scheme != QLatin1String("mailto") && url.host().isEmpty()))
        return false;
    if (scheme == QLatin1String("mailto")) {
        // Some mail clients honour attach=/attachment= and would attach a
        // local file (~/.ssh/id_rsa) to the new message. Only the address,
        // subject, body and cc go through.
        const QUrlQuery query(url);
        QUrlQuery kept;
        for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded)) {
            const QString k = key.toLower();
            if (k == QLatin1String("subject") || k == QLatin1String("body") || k == QLatin1String("cc"))
                kept.addQueryItem(k, value);
        }
        QUrl mail;
        mail.setScheme(QStringLiteral("mailto"));
        mail.setPath(url.path(QUrl::FullyDecoded), QUrl::DecodedMode);
        mail.setQuery(kept);
        Q_EMIT linkActivated(mail.toString(QUrl::FullyEncoded));
        return true;
    }
    Q_EMIT linkActivated(url.toString(QUrl::FullyEncoded));
    return true;
}

// Runs one press, drag or release through Ghostty's gesture state machine
// (cell drag, word on double click, line on triple) and installs the
// selection it yields. Returns whether there is one.
bool TerminalView::selectionGesture(GhosttySelectionGestureEvent event, QMouseEvent *mouse)
{
    const GhosttyGridRef ref = refAt(mouse->position());
    const GhosttySurfacePosition position{mouse->position().x(), mouse->position().y()};
    ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, &ref);
    if (event != m_releaseEvent)
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &position);
    if (event == m_pressEvent) {
        const uint64_t time = uint64_t(mouse->timestamp()) * 1000000;
        const uint64_t interval = uint64_t(QGuiApplication::styleHints()->mouseDoubleClickInterval()) * 1000000;
        const double distance = QGuiApplication::styleHints()->mouseDoubleClickDistance();
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_TIME_NS, &time);
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_INTERVAL_NS, &interval);
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_DISTANCE, &distance);
    }
    if (event == m_dragEvent) {
        const GhosttySelectionGestureGeometry geometry{m_cols, uint32_t(m_cellWidth), uint32_t(Padding),
                                                       uint32_t(qMax(1.0, height()))};
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_GEOMETRY, &geometry);
        const bool rectangle = mouse->modifiers() & Qt::AltModifier;
        ghostty_selection_gesture_event_set(event, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_RECTANGLE, &rectangle);
    }

    GhosttySelection selection = GHOSTTY_INIT_SIZED(GhosttySelection);
    const GhosttyResult result = ghostty_selection_gesture_event(m_gesture, m_terminal, event, &selection);
    if (event == m_releaseEvent)
        return m_hasSelection;
    if (result == GHOSTTY_SUCCESS) {
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &selection);
        m_hasSelection = true;
    } else if (event == m_pressEvent) {
        clearSelection();
    }
    update();
    return m_hasSelection;
}

void TerminalView::clearSelection()
{
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
    m_hasSelection = false;
    update();
}

QString TerminalView::selectedText() const
{
    if (!m_hasSelection)
        return {};
    // Plain text with soft wraps joined and trailing blanks trimmed, as
    // Ghostty itself copies.
    GhosttyTerminalSelectionFormatOptions options = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectionFormatOptions);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = true;
    options.trim = true;
    uint8_t *buf = nullptr;
    size_t len = 0;
    if (ghostty_terminal_selection_format_alloc(m_terminal, nullptr, options, &buf, &len) != GHOSTTY_SUCCESS)
        return {};
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(buf), qsizetype(len));
    ghostty_free(nullptr, buf, len);
    return text;
}

void TerminalView::copy()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void TerminalView::paste(bool primarySelection)
{
    if (m_ptyFd < 0)
        return;
    const QString text = QGuiApplication::clipboard()->text(primarySelection ? QClipboard::Selection
                                                                             : QClipboard::Clipboard);
    if (text.isEmpty())
        return;
    // Without bracketed paste a line break is Enter, so a pasted line would
    // run before it can be read; Ghostty asks first too. (ESC needs no
    // question: the encoder below defuses it.)
    GhosttyTerminalModeConfig bracketed{GHOSTTY_MODE_BRACKETED_PASTE, false};
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &bracketed);
    if (!bracketed.value && (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')))) {
        Q_EMIT unsafePaste(text);
        return;
    }
    pasteText(text);
}

void TerminalView::pasteText(const QString &text)
{
    if (m_ptyFd < 0 || text.isEmpty())
        return;
    QByteArray data = text.toUtf8();
    // Ghostty's encoder strips control bytes (ESC included, so a pasted
    // end-of-paste marker cannot break out) and brackets the paste when the
    // shell turned that on, so a pasted newline does not run anything.
    GhosttyTerminalModeConfig bracketed{GHOSTTY_MODE_BRACKETED_PASTE, false};
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &bracketed);
    QByteArray out(data.size() + 16, Qt::Uninitialized);
    size_t written = 0;
    if (ghostty_paste_encode(data.data(), size_t(data.size()), bracketed.value, out.data(),
                             size_t(out.size()), &written) != GHOSTTY_SUCCESS)
        return;
    ghostty_terminal_scroll_viewport(m_terminal, GhosttyTerminalScrollViewport{
        GHOSTTY_SCROLL_VIEWPORT_BOTTOM, {}});
    updateScrollbar();
    writePty(out.constData(), qsizetype(written));
}

namespace {

GhosttyMouseButton ghosttyButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return GHOSTTY_MOUSE_BUTTON_LEFT;
    case Qt::RightButton: return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case Qt::MiddleButton: return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    default: return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
}

} // namespace

// Sends a mouse event to the program when it turned mouse tracking on.
// Shift keeps the mouse for selection, as in other terminals. Returns
// whether the program got the event.
bool TerminalView::reportMouse(GhosttyMouseAction action, GhosttyMouseButton button, QPointF pos,
                               Qt::KeyboardModifiers modifiers, Qt::MouseButtons held)
{
    bool tracking = false;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking);
    if (!tracking || m_ptyFd < 0 || (modifiers & Qt::ShiftModifier))
        return false;

    GhosttyMouseEncoderSize size = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    size.screen_width = uint32_t(width());
    size.screen_height = uint32_t(height());
    size.cell_width = uint32_t(m_cellWidth);
    size.cell_height = uint32_t(m_cellHeight);
    size.padding_top = size.padding_bottom = size.padding_left = size.padding_right = uint32_t(Padding);
    const bool anyPressed = held != Qt::NoButton;
    ghostty_mouse_encoder_setopt_from_terminal(m_mouseEncoder, m_terminal);
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyPressed);

    ghostty_mouse_event_set_action(m_mouseEvent, action);
    if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
        ghostty_mouse_event_clear_button(m_mouseEvent);
    else
        ghostty_mouse_event_set_button(m_mouseEvent, button);
    ghostty_mouse_event_set_mods(m_mouseEvent, ghosttyMods(modifiers));
    ghostty_mouse_event_set_position(m_mouseEvent, GhosttyMousePosition{float(pos.x()), float(pos.y())});

    char out[64];
    size_t written = 0;
    if (ghostty_mouse_encoder_encode(m_mouseEncoder, m_mouseEvent, out, sizeof out, &written) == GHOSTTY_SUCCESS
        && written > 0)
        writePty(out, qsizetype(written));
    return true;
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    // Ctrl+click opens a link, even under a program tracking the mouse.
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        const Link link = linkAt(event->position());
        if (!link.uri.isEmpty() && activateLink(link.uri)) {
            m_linkClicked = true;   // and its release is not the program's either
            event->accept();
            return;
        }
    }
    if (reportMouse(GHOSTTY_MOUSE_ACTION_PRESS, ghosttyButton(event->button()), event->position(), event->modifiers(),
                    event->buttons())) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
        selectionGesture(m_pressEvent, event);
    else if (event->button() == Qt::MiddleButton)
        paste(true);
    event->accept();
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    const Qt::MouseButton held = event->buttons() & Qt::LeftButton ? Qt::LeftButton
                               : event->buttons() & Qt::MiddleButton ? Qt::MiddleButton
                               : event->buttons() & Qt::RightButton ? Qt::RightButton
                                                                     : Qt::NoButton;
    if (reportMouse(GHOSTTY_MOUSE_ACTION_MOTION, ghosttyButton(held), event->position(), event->modifiers(),
                    event->buttons())) {
        event->accept();
        return;
    }
    if (event->buttons() & Qt::LeftButton)
        selectionGesture(m_dragEvent, event);
    event->accept();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (std::exchange(m_linkClicked, false) && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    if (reportMouse(GHOSTTY_MOUSE_ACTION_RELEASE, ghosttyButton(event->button()), event->position(), event->modifiers(),
                    event->buttons())) {
        event->accept();
        return;
    }
    // A finished selection becomes the primary selection, for middle click
    // here and in other apps.
    if (event->button() == Qt::LeftButton && selectionGesture(m_releaseEvent, event)) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard->supportsSelection())
            clipboard->setText(selectedText(), QClipboard::Selection);
    }
    event->accept();
}

void TerminalView::hoverLeaveEvent(QHoverEvent *event)
{
    m_hoverPos = QPointF(-1, -1);
    updateHoverLink();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void TerminalView::hoverMoveEvent(QHoverEvent *event)
{
    m_hoverPos = event->position();
    updateHoverLink();
    // Motion with no button down only reaches programs in any-event
    // tracking; the encoder drops it for the other modes.
    reportMouse(GHOSTTY_MOUSE_ACTION_MOTION, GHOSTTY_MOUSE_BUTTON_UNKNOWN, event->position(), event->modifiers(),
                Qt::NoButton);
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    // Three lines a notch, like most terminals; touchpads send fractions.
    m_scrollRemainder += event->angleDelta().y() / 120.0 * 3;
    const int lines = int(m_scrollRemainder);
    m_scrollRemainder -= lines;
    if (lines == 0) {
        event->accept();
        return;
    }

    // A program tracking the mouse gets wheel buttons 4 and 5, one press a
    // notch.
    const GhosttyMouseButton wheel = lines > 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE;
    if (reportMouse(GHOSTTY_MOUSE_ACTION_PRESS, wheel, event->position(), event->modifiers(), Qt::NoButton)) {
        for (int i = 3; i < qAbs(lines); i += 3)
            reportMouse(GHOSTTY_MOUSE_ACTION_PRESS, wheel, event->position(), event->modifiers(), Qt::NoButton);
        event->accept();
        return;
    }

    // Full-screen programs without mouse support (less, man) have no
    // scrollback to show; they get arrow keys instead, as in xterm.
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    if (screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE && m_ptyFd >= 0) {
        const int key = lines > 0 ? Qt::Key_Up : Qt::Key_Down;
        for (int i = 0; i < qAbs(lines); ++i) {
            QKeyEvent arrow(QEvent::KeyPress, key, Qt::NoModifier);
            sendKey(&arrow, GHOSTTY_KEY_ACTION_PRESS);
        }
        event->accept();
        return;
    }

    GhosttyTerminalScrollViewport scroll{GHOSTTY_SCROLL_VIEWPORT_DELTA, {}};
    scroll.value.delta = -lines;
    scrollViewport(scroll);
    event->accept();
}

void TerminalView::scrollViewport(GhosttyTerminalScrollViewport scroll)
{
    ghostty_terminal_scroll_viewport(m_terminal, scroll);
    updateScrollbar();
    updateHoverLink();
    if (!m_searchText.isEmpty())
        refreshSearch();   // the highlights follow the viewport
    update();
}

// Shift+PageUp/PageDown page through the scrollback and Shift+Home/End go
// to its ends, as in GNOME Terminal; Ctrl+Shift+PageUp/PageDown jump
// between prompts the shell marked (OSC 133: fish does it unasked, bash
// and zsh with their shell integration). Full-screen programs have no
// scrollback, so on their screen the keys are theirs.
bool TerminalView::scrollKey(QKeyEvent *event)
{
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    if (screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE)
        return false;
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    GhosttyTerminalScrollViewport scroll{GHOSTTY_SCROLL_VIEWPORT_DELTA, {}};
    if (mods == Qt::ShiftModifier && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
        const intptr_t page = qMax<intptr_t>(1, intptr_t(m_scrollbar.len) - 1);
        scroll.value.delta = key == Qt::Key_PageUp ? -page : page;
    } else if (mods == Qt::ShiftModifier && key == Qt::Key_Home) {
        scroll.tag = GHOSTTY_SCROLL_VIEWPORT_TOP;
    } else if (mods == Qt::ShiftModifier && key == Qt::Key_End) {
        scroll.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)
               && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
        scroll.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA_PROMPT;
        scroll.value.delta_prompt = key == Qt::Key_PageUp ? -1 : 1;
    } else {
        return false;
    }
    scrollViewport(scroll);
    return true;
}

void TerminalView::updateScrollbar()
{
    GhosttyTerminalScrollbar bar{0, 0, 0};
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &bar);
    if (bar.total == m_scrollbar.total && bar.offset == m_scrollbar.offset && bar.len == m_scrollbar.len)
        return;
    m_scrollbar = bar;
    Q_EMIT scrollChanged();
}

void TerminalView::scrollToRow(qreal row)
{
    GhosttyTerminalScrollViewport scroll{GHOSTTY_SCROLL_VIEWPORT_ROW, {}};
    scroll.value.row = size_t(qMax(0.0, row));
    scrollViewport(scroll);
}

void TerminalView::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    if (!m_search && !text.isEmpty())
        ghostty_search_new(nullptr, &m_search, m_terminal);
    if (m_search) {
        const QByteArray needle = text.toUtf8();
        const GhosttyString value{reinterpret_cast<const uint8_t *>(needle.constData()), size_t(needle.size())};
        ghostty_search_set(m_search, GHOSTTY_SEARCH_OPT_NEEDLE, text.isEmpty() ? nullptr : &value);
        // Typing jumps to the newest match, scrolling to it if needed.
        if (!text.isEmpty()) {
            ghostty_search_run(m_search);
            ghostty_search_set(m_search, GHOSTTY_SEARCH_OPT_SELECT_NEXT, nullptr);
        }
    }
    refreshSearch();
    Q_EMIT searchChanged();
}

void TerminalView::findNext()
{
    if (!m_search || m_searchText.isEmpty())
        return;
    ghostty_search_set(m_search, GHOSTTY_SEARCH_OPT_SELECT_NEXT, nullptr);
    refreshSearch();
}

void TerminalView::findPrevious()
{
    if (!m_search || m_searchText.isEmpty())
        return;
    ghostty_search_set(m_search, GHOSTTY_SEARCH_OPT_SELECT_PREV, nullptr);
    refreshSearch();
}

// Catches the search up with the terminal (new output, scrolling) and
// publishes the count and position for the find bar.
void TerminalView::refreshSearch()
{
    int count = 0, index = 0;
    if (m_search && !m_searchText.isEmpty()) {
        ghostty_search_run(m_search);
        size_t total = 0, selected = 0;
        ghostty_search_get(m_search, GHOSTTY_SEARCH_DATA_TOTAL_MATCHES, &total);
        if (ghostty_search_get(m_search, GHOSTTY_SEARCH_DATA_SELECTED_INDEX, &selected) == GHOSTTY_SUCCESS)
            index = int(selected) + 1;
        count = int(total);
    }
    updateScrollbar();   // selecting a match may have scrolled to it
    update();
    if (count == m_searchCount && index == m_searchIndex)
        return;
    m_searchCount = count;
    m_searchIndex = index;
    Q_EMIT searchChanged();
}

// Tints every match in view, the current one more strongly. Runs inside
// paint(), after the text, so the tint lies over it.
void TerminalView::paintSearchMatches(QPainter *painter)
{
    if (!m_search || m_searchText.isEmpty())
        return;
    // Matches are snapshots, stale after any write or resize since the
    // last feed; a feed (bounded work) brings the viewport's up to date.
    ghostty_search_feed(m_search);
    QVarLengthArray<GhosttySelection, 64> matches(64);
    GhosttySelectionBuffer buffer{matches.data(), size_t(matches.size()), 0};
    GhosttyResult result = ghostty_search_get(m_search, GHOSTTY_SEARCH_DATA_VIEWPORT_MATCHES, &buffer);
    if (result == GHOSTTY_OUT_OF_SPACE) {
        matches.resize(qsizetype(buffer.len));
        buffer = GhosttySelectionBuffer{matches.data(), size_t(matches.size()), 0};
        result = ghostty_search_get(m_search, GHOSTTY_SEARCH_DATA_VIEWPORT_MATCHES, &buffer);
    }
    if (result != GHOSTTY_SUCCESS)
        return;

    GhosttyPointCoordinate selectedStart{UINT16_MAX, UINT32_MAX};
    GhosttySelection selected = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_search_get(m_search, GHOSTTY_SEARCH_DATA_SELECTED_MATCH, &selected) == GHOSTTY_SUCCESS)
        ghostty_terminal_point_from_grid_ref(m_terminal, &selected.start, GHOSTTY_POINT_TAG_VIEWPORT,
                                             &selectedStart);
    QColor other = m_matchColor;
    other.setAlphaF(0.3);
    QColor current = m_matchColor;
    current.setAlphaF(0.65);
    for (size_t i = 0; i < buffer.len; ++i) {
        GhosttyPointCoordinate start{}, end{};
        // Matches on the viewport's pages but outside the view fail here.
        if (ghostty_terminal_point_from_grid_ref(m_terminal, &matches[qsizetype(i)].start,
                                                 GHOSTTY_POINT_TAG_VIEWPORT, &start) != GHOSTTY_SUCCESS
            || ghostty_terminal_point_from_grid_ref(m_terminal, &matches[qsizetype(i)].end,
                                                    GHOSTTY_POINT_TAG_VIEWPORT, &end) != GHOSTTY_SUCCESS
            || start.y >= m_rowCount || end.y >= m_rowCount)
            continue;
        const bool isCurrent = start.x == selectedStart.x && start.y == selectedStart.y;
        // A match can wrap onto following rows.
        for (uint32_t row = start.y; row <= end.y; ++row) {
            const int from = row == start.y ? start.x : 0;
            const int to = row == end.y ? end.x : m_cols - 1;
            painter->fillRect(QRectF(Padding + from * m_cellWidth, Padding + row * m_cellHeight,
                                     (to - from + 1) * m_cellWidth, m_cellHeight),
                              isCurrent ? current : other);
        }
    }
}

// The kitty images on screen, lowest first. Pixels are copied out of the
// storage once per image generation (a new upload or animation frame).
QList<TerminalView::Image> TerminalView::imagesToDraw()
{
    QList<Image> images;
    GhosttyKittyGraphics graphics = nullptr;
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) != GHOSTTY_SUCCESS
        || !graphics)
        return images;
    if (ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_IMAGE_ITERATOR, &m_images)
        != GHOSTTY_SUCCESS) {
        m_imageCache.clear();
        return images;
    }
    QHash<uint32_t, CachedImage> drawn;
    while (ghostty_render_state_image_iterator_next(m_images)) {
        GhosttyImageProtocol protocol = GHOSTTY_IMAGE_PROTOCOL_KITTY;
        GhosttyRenderStateImageLayer layer = GHOSTTY_RENDER_STATE_IMAGE_LAYER_ABOVE_TEXT;
        uint32_t id = 0, offsetX = 0, offsetY = 0, width = 0, height = 0;
        uint32_t srcX = 0, srcY = 0, srcWidth = 0, srcHeight = 0;
        uint64_t generation = 0;
        int32_t col = 0, row = 0, z = 0;
        GhosttyRenderStateImageData keys[] = {
            GHOSTTY_RENDER_STATE_IMAGE_DATA_PROTOCOL, GHOSTTY_RENDER_STATE_IMAGE_DATA_IMAGE_ID,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_IMAGE_GENERATION, GHOSTTY_RENDER_STATE_IMAGE_DATA_LAYER,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_Z, GHOSTTY_RENDER_STATE_IMAGE_DATA_VIEWPORT_COL,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_VIEWPORT_ROW, GHOSTTY_RENDER_STATE_IMAGE_DATA_CELL_OFFSET_X,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_CELL_OFFSET_Y, GHOSTTY_RENDER_STATE_IMAGE_DATA_PIXEL_WIDTH,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_PIXEL_HEIGHT, GHOSTTY_RENDER_STATE_IMAGE_DATA_SOURCE_X,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_SOURCE_Y, GHOSTTY_RENDER_STATE_IMAGE_DATA_SOURCE_WIDTH,
            GHOSTTY_RENDER_STATE_IMAGE_DATA_SOURCE_HEIGHT,
        };
        void *values[] = {&protocol, &id, &generation, &layer, &z, &col, &row, &offsetX, &offsetY,
                          &width, &height, &srcX, &srcY, &srcWidth, &srcHeight};
        if (ghostty_render_state_image_get_multi(m_images, std::size(keys), keys, values, nullptr)
                != GHOSTTY_SUCCESS
            || protocol != GHOSTTY_IMAGE_PROTOCOL_KITTY)
            continue;

        // Only what is on screen stays cached, so the copies never add up
        // to more than the images in view.
        CachedImage cached = m_imageCache.value(id);
        if (cached.pixels.isNull() || cached.generation != generation) {
            cached = {generation, imagePixels(graphics, id)};
            if (cached.pixels.isNull())
                continue;
        }
        drawn.insert(id, cached);
        Image image;
        image.layer = layer;
        image.z = z;
        image.pixels = cached.pixels;
        image.target = QRectF(Padding + col * m_cellWidth + offsetX, Padding + row * m_cellHeight + offsetY,
                              width, height);
        image.source = QRectF(srcX, srcY, srcWidth, srcHeight);
        images.append(image);
    }
    m_imageCache = std::move(drawn);
    std::stable_sort(images.begin(), images.end(),
                     [](const Image &a, const Image &b) { return a.z < b.z; });
    return images;
}

QImage TerminalView::imagePixels(GhosttyKittyGraphics graphics, uint32_t id)
{
    GhosttyKittyGraphicsImage image = ghostty_kitty_graphics_image(graphics, id);
    if (!image)
        return {};
    uint32_t width = 0, height = 0;
    GhosttyKittyImageFormat format = GHOSTTY_KITTY_IMAGE_FORMAT_RGBA;
    const uint8_t *pixels = nullptr;
    size_t length = 0;
    GhosttyKittyGraphicsImageData keys[] = {
        GHOSTTY_KITTY_IMAGE_DATA_WIDTH, GHOSTTY_KITTY_IMAGE_DATA_HEIGHT, GHOSTTY_KITTY_IMAGE_DATA_FORMAT,
        GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR, GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN,
    };
    void *values[] = {&width, &height, &format, &pixels, &length};
    if (ghostty_kitty_graphics_image_get_multi(image, std::size(keys), keys, values, nullptr) != GHOSTTY_SUCCESS
        || !pixels || width == 0 || height == 0)
        return {};
    QImage::Format qtFormat = QImage::Format_RGBA8888;
    int depth = 4;
    switch (format) {
    case GHOSTTY_KITTY_IMAGE_FORMAT_RGB: qtFormat = QImage::Format_RGB888; depth = 3; break;
    case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY: qtFormat = QImage::Format_Grayscale8; depth = 1; break;
    case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA: {
        // Qt has no grey+alpha format; spread it out to RGBA.
        if (length < size_t(width) * height * 2)
            return {};
        QImage rgba(int(width), int(height), QImage::Format_RGBA8888);
        if (rgba.isNull())
            return {};
        for (uint32_t y = 0; y < height; ++y) {
            uchar *line = rgba.scanLine(int(y));
            const uint8_t *in = pixels + size_t(y) * width * 2;
            for (uint32_t x = 0; x < width; ++x) {
                line[4 * x] = line[4 * x + 1] = line[4 * x + 2] = in[2 * x];
                line[4 * x + 3] = in[2 * x + 1];
            }
        }
        return rgba;
    }
    default: break;
    }
    const size_t stride = size_t(width) * depth;
    if (length < stride * height)
        return {};
    // A copy in the stored format, so no bigger than what the storage
    // limit allows: the storage owns the pixels and may free them on the
    // next write.
    return QImage(pixels, int(width), int(height), qsizetype(stride), qtFormat).copy();
}

void TerminalView::paint(QPainter *painter)
{
    // Kitty animations: move to the due frame before the render state is
    // read, and wake for the next one. This is the render thread, so the
    // timer is started on the item's own.
    GhosttyKittyGraphics graphics = nullptr;
    uint64_t next = 0;
    const uint64_t now = uint64_t(m_clock.elapsed());
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) == GHOSTTY_SUCCESS
        && graphics && ghostty_kitty_graphics_animation_tick(graphics, now, &next) == GHOSTTY_SUCCESS) {
        const int wait = int(qBound<uint64_t>(8, next > now ? next - now : 0, 60000));
        QMetaObject::invokeMethod(this, [this, wait] { m_animationTimer.start(wait); }, Qt::QueuedConnection);
    }
    if (ghostty_render_state_update(m_renderState, m_terminal) != GHOSTTY_SUCCESS)
        return;
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);
    // Source, not over: the default background carries the view's alpha.
    QColor background = qcolor(colors.background);
    background.setAlpha(m_background.alpha());
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(boundingRect(), background);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QList<Image> images = imagesToDraw();
    // ponytail: below-text images go under cell backgrounds too; kitty
    // keeps those apart, which needs the backgrounds in a pass of their own.
    for (const Image &image : images) {
        if (image.layer != GHOSTTY_RENDER_STATE_IMAGE_LAYER_ABOVE_TEXT)
            painter->drawImage(image.target, image.pixels, image.source);
    }
    if (ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m_rows)
        != GHOSTTY_SUCCESS)
        return;

    // Neighbouring cells with the same look are drawn with one drawText;
    // per cell it was ~5 ms for a full 145x25 screen on a slow laptop.
    struct Run {
        QString text;
        qreal x = 0;
        int cells = 0;
        QRgb color = 0;
        int font = 0;
        int underline = GHOSTTY_SGR_UNDERLINE_NONE;
        QRgb underlineColor = 0;
        bool strikethrough = false;
        bool overline = false;
    } run;
    qreal y = Padding;
    const qreal thickness = qMax(1.0, std::round(m_cellHeight / 16.0));
    const auto flush = [&] {
        if (run.cells == 0)
            return;
        const QColor color = QColor::fromRgba(run.color);
        painter->setPen(color);
        painter->setFont(m_cellFonts[run.font]);
        painter->drawText(QPointF(run.x, y + m_ascent), run.text);
        const qreal end = run.x + run.cells * m_cellWidth;
        if (run.underline != GHOSTTY_SGR_UNDERLINE_NONE)
            drawUnderline(painter, run.underline, QColor::fromRgba(run.underlineColor), run.x, end,
                          y + m_ascent, y + m_cellHeight, thickness, m_cellWidth);
        if (run.strikethrough)
            painter->fillRect(QRectF(run.x, y + m_cellHeight / 2 - thickness / 2, end - run.x, thickness), color);
        // Just above the capitals: at the cell's top edge it reads as an
        // underline of the row above.
        if (run.overline)
            painter->fillRect(QRectF(run.x, y + qMax(0.0, m_ascent - m_capHeight - 2 * thickness), end - run.x,
                                     thickness),
                              color);
        run.text.clear();
        run.cells = 0;
    };

    QVarLengthArray<uint32_t, 16> codepoints;
    while (ghostty_render_state_row_iterator_next(m_rows)) {
        if (ghostty_render_state_row_get(m_rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m_cells)
            != GHOSTTY_SUCCESS) {
            y += m_cellHeight;
            continue;
        }
        GhosttyRenderStateRowSelection selected = GHOSTTY_INIT_SIZED(GhosttyRenderStateRowSelection);
        const bool rowSelected = ghostty_render_state_row_get(m_rows, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION,
                                                              &selected) == GHOSTTY_SUCCESS;
        int column = 0;
        qreal x = Padding;
        while (ghostty_render_state_row_cells_next(m_cells)) {
            uint32_t graphemeLen = 0;
            ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                                               &graphemeLen);
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            GhosttyColorRgb fg = colors.foreground;
            ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
            GhosttyColorRgb bg = colors.background;
            bool hasBg = ghostty_render_state_row_cells_get(
                             m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;
            // Selected cells are drawn inverted, so the selection shows in
            // any theme and over any colours the program chose.
            const bool cellSelected = rowSelected && column >= selected.start_x && column <= selected.end_x;
            if (style.inverse != cellSelected) {
                std::swap(fg, bg);
                hasBg = true;
            }

            // A wide character draws across its own cell and the spacer after it.
            GhosttyCell raw = 0;
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            if (ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw)
                == GHOSTTY_SUCCESS)
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            const qreal cellWidth = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 * m_cellWidth : m_cellWidth;

            // Backgrounds go down as the row is walked, before the run that
            // covers them is drawn at its end.
            if (hasBg)
                painter->fillRect(QRectF(x, y, cellWidth, m_cellHeight), qcolor(bg));

            const bool drawn = graphemeLen > 0 && !style.invisible;
            if (drawn) {
                // The shell decides the length (any number of combining
                // marks), so the buffer is sized to it, not fixed.
                codepoints.resize(graphemeLen);
                ghostty_render_state_row_cells_get(m_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                                                   codepoints.data());
            }
            // Kitty image placeholders: the image list draws these cells.
            if (drawn && codepoints[0] != 0x10EEEE) {
                QColor color = qcolor(fg);
                if (style.faint)
                    color.setAlphaF(0.6);
                const int font = (style.bold ? 1 : 0) | (style.italic ? 2 : 0);
                // Only plain ASCII is known to take exactly one cell in the
                // fitted font; anything else (fallback fonts, wide and
                // combining characters) is drawn on its own.
                const bool joinable = graphemeLen == 1 && codepoints[0] < 0x80 && wide == GHOSTTY_CELL_WIDE_NARROW;
                // SGR 58 colours the underline on its own; unset, it follows the text.
                QRgb underlineColor = color.rgba();
                if (style.underline_color.tag == GHOSTTY_STYLE_COLOR_PALETTE)
                    underlineColor = qcolor(colors.palette[style.underline_color.value.palette]).rgba();
                else if (style.underline_color.tag == GHOSTTY_STYLE_COLOR_RGB)
                    underlineColor = qcolor(style.underline_color.value.rgb).rgba();
                if (!joinable || run.color != color.rgba() || run.font != font
                    || run.underline != style.underline || run.underlineColor != underlineColor
                    || run.strikethrough != style.strikethrough || run.overline != style.overline
                    || (run.cells > 0 && run.x + run.cells * m_cellWidth != x))
                    flush();
                if (run.cells == 0) {
                    run.x = x;
                    run.color = color.rgba();
                    run.font = font;
                    run.underline = style.underline;
                    run.underlineColor = underlineColor;
                    run.strikethrough = style.strikethrough;
                    run.overline = style.overline;
                }
                run.text += QString::fromUcs4(reinterpret_cast<const char32_t *>(codepoints.data()),
                                              codepoints.size());
                run.cells += wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1;
                if (!joinable)
                    flush();
            }
            x += m_cellWidth;
            ++column;
        }
        flush();
        y += m_cellHeight;
    }

    for (const Image &image : images) {
        if (image.layer == GHOSTTY_RENDER_STATE_IMAGE_LAYER_ABOVE_TEXT)
            painter->drawImage(image.target, image.pixels, image.source);
    }

    paintSearchMatches(painter);

    if (m_hoverLink.row >= 0) {
        const qreal lineY = Padding + m_hoverLink.row * m_cellHeight + m_ascent + thickness;
        painter->fillRect(QRectF(Padding + m_hoverLink.start * m_cellWidth, lineY,
                                 (m_hoverLink.end - m_hoverLink.start + 1) * m_cellWidth, thickness),
                          qcolor(colors.foreground));
    }

    if (m_bellTimer.isActive()) {
        QColor tint = qcolor(colors.foreground);
        tint.setAlphaF(0.12);
        painter->fillRect(boundingRect(), tint);
    }

    bool cursorVisible = false;
    bool cursorInView = false;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursorVisible);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorInView);
    bool cursorBlinking = false;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &cursorBlinking);
    if (cursorBlinking && !m_cursorShown && hasActiveFocus())
        cursorVisible = false;
    if (cursorVisible && cursorInView) {
        uint16_t cx = 0, cy = 0;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
        QColor cursor = colors.cursor_has_value ? qcolor(colors.cursor) : qcolor(colors.foreground);
        const QRectF cell(Padding + cx * m_cellWidth, Padding + cy * m_cellHeight, m_cellWidth, m_cellHeight);
        GhosttyRenderStateCursorVisualStyle shape = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &shape);
        const qreal thickness = qMax(2.0, std::round(m_cellWidth / 6.0));
        if (hasActiveFocus() && shape == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR) {
            painter->fillRect(QRectF(cell.left(), cell.top(), thickness, cell.height()), cursor);
        } else if (hasActiveFocus() && shape == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE) {
            painter->fillRect(QRectF(cell.left(), cell.bottom() - thickness, cell.width(), thickness), cursor);
        } else if (hasActiveFocus() && shape == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK) {
            cursor.setAlphaF(0.5);
            painter->fillRect(cell, cursor);
        } else {
            painter->setPen(cursor);
            painter->drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
}
