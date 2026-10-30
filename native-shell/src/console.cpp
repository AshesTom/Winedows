// SPDX-License-Identifier: LGPL-2.1-or-later
#include "console.h"

#include "xpfiles.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QtEndian>

#include <cerrno>
#include <csignal>
#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// XP's screen buffer is 300 lines, of which the window shows 25.
const int COLUMNS = 80, ROWS = 25, BUFFER = 300, HISTORY = BUFFER - ROWS, BLINK_MS = 530, SCROLL_BAR = 17, WHEEL_LINES = 3;
// XP's small cursor: a quarter of the cell, at its foot
const int CURSOR_LINES = 3;
// The console's 16 colours, in the ANSI order
const QRgb PALETTE[16] = {0xff000000, 0xff800000, 0xff008000, 0xff808000, 0xff000080, 0xff800080, 0xff008080, 0xffc0c0c0,
                          0xff808080, 0xffff0000, 0xff00ff00, 0xffffff00, 0xff0000ff, 0xffff00ff, 0xff00ffff, 0xffffffff};
const char DEFAULT_FG = 7, DEFAULT_BG = 0;
// Code page 437 as Unicode: its pictures below 0x20, 0x7f, and its upper half
const char16_t CP437_LOW[] = u"\0☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼";
const char16_t CP437_HIGH[] = u"ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";
// DEC's line drawing set, which programs ask for with ESC ( 0
const char16_t LINE_DRAWING[] = u"◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·";

char32_t codeToUnicode(int code)
{
    if (code < 0x20)
        return CP437_LOW[code];
    if (code == 0x7f)
        return U'⌂';
    if (code < 0x80)
        return char32_t(code);
    return CP437_HIGH[code - 0x80];
}

quint16 u16(const QByteArray &data, qint64 at)
{
    return at + 2 <= data.size() ? qFromLittleEndian<quint16>(data.constData() + at) : 0;
}

// The glyphs of a .fon holding one Windows 2.x raster font, as masks by their Unicode character.
QHash<char32_t, QImage> readFont(const QString &path, int *cellWidth, int *cellHeight)
{
    QHash<char32_t, QImage> glyphs;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return glyphs;
    const QByteArray data = file.readAll();
    if (data.size() < 0x40)
        return glyphs;
    const qint64 ne = qFromLittleEndian<quint32>(data.constData() + 0x3c);
    const qint64 table = ne + u16(data, ne + 0x24);
    const int shift = u16(data, table);
    qint64 at = table + 2, start = -1;
    while (start < 0 && u16(data, at)) {
        const int kind = u16(data, at), count = u16(data, at + 2);
        at += 8;
        for (int i = 0; i < count; ++i, at += 12)
            if (kind == 0x8008 && start < 0)
                start = qint64(u16(data, at)) << shift;
    }
    if (start < 0 || u16(data, start) != 0x200)
        return glyphs;
    const int width = u16(data, start + 86), height = u16(data, start + 88);
    const int first = uchar(data.at(start + 95)), last = uchar(data.at(start + 96));
    if (!width || !height || width > 32 || height > 64)
        return glyphs;
    for (int code = first; code <= last; ++code) {
        const qint64 entry = start + 118 + (code - first) * 4;
        const int glyphWidth = u16(data, entry);
        const qint64 bits = start + u16(data, entry + 2);
        const int columns = (glyphWidth + 7) / 8;
        if (bits + columns * height > data.size())
            continue;
        QImage mask(glyphWidth, height, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < height; ++row) {
                const uchar byte = uchar(data.at(bits + column * height + row));
                for (int x = 0; x < 8 && column * 8 + x < glyphWidth; ++x)
                    if (byte & (0x80 >> x))
                        mask.scanLine(row)[column * 8 + x] = 255;
            }
        glyphs.insert(codeToUnicode(code), mask);
    }
    *cellWidth = width;
    *cellHeight = height;
    return glyphs;
}

// The nearest of the console's colours for a colour of the 256 or an RGB one
int nearestColour(int r, int g, int b)
{
    int best = 0, bestDistance = INT_MAX;
    for (int i = 0; i < 16; ++i) {
        const int dr = qRed(PALETTE[i]) - r, dg = qGreen(PALETTE[i]) - g, db = qBlue(PALETTE[i]) - b;
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int colourOf256(int index)
{
    if (index < 16)
        return index;
    if (index >= 232) {
        const int level = 8 + (index - 232) * 10;
        return nearestColour(level, level, level);
    }
    index -= 16;
    const auto level = [](int value) { return value ? 55 + value * 40 : 0; };
    return nearestColour(level(index / 36), level(index / 6 % 6), level(index % 6));
}

} // namespace

Console::Console(const QString &folder)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QObject::tr("Command Prompt"));
    if (const auto cmd = xpfiles::resources(xpfiles::find(QStringLiteral("cmd.exe"))))
        setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*cmd, cmd->names(QStringLiteral("14")).value(0), 32))));
    m_glyphs = readFont(xpfiles::find(QStringLiteral("vgaoem.fon")), &m_glyphWidth, &m_glyphHeight);

    m_bottom = ROWS - 1;
    for (int i = 0; i < ROWS; ++i)
        m_screen.append(blankLine());

    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    // Always the whole buffer, as XP's: its thumb is there from the first line, at the top.
    m_scrollBar->setRange(0, HISTORY);
    m_empty = blankLine();
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this] { update(); });
    // 80 x 25 at least, and it stays that: more room is left black, with the scroll bar at its edge.
    setMinimumSize(COLUMNS * m_glyphWidth + SCROLL_BAR, ROWS * m_glyphHeight);
    resize(minimumSize());
    m_scrollBar->setGeometry(width() - SCROLL_BAR, 0, SCROLL_BAR, height());
    m_scrollBar->setPageStep(ROWS);
    m_scrollBar->setFocusPolicy(Qt::NoFocus);

    m_blink.setInterval(BLINK_MS);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        update();
    });
    m_blink.start();
    start(folder);
}

Console::~Console()
{
    if (m_child > 0)
        ::kill(m_child, SIGHUP);
    if (m_master >= 0)
        ::close(m_master);
}

void Console::start(const QString &folder)
{
    struct winsize size {};
    size.ws_row = ROWS;
    size.ws_col = COLUMNS;
    size.ws_xpixel = ushort(COLUMNS * m_glyphWidth);
    size.ws_ypixel = ushort(ROWS * m_glyphHeight);
    // (Prepared before the fork: only what is safe runs between it and exec.)
    const QByteArray directory = QFile::encodeName(folder.isEmpty() ? QDir::homePath() : folder);
    const QByteArray profile = QFile::encodeName(QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../../../session/xp-cmd.bashrc")));
    const QByteArray title = windowTitle().toUtf8();
    m_child = forkpty(&m_master, nullptr, nullptr, &size);
    if (m_child < 0) {
        close();
        return;
    }
    if (m_child == 0) {
        if (chdir(directory.constData()) != 0 && chdir("/") != 0)
            _exit(127);
        setenv("TERM", "xterm", 1);
        setenv("XP_CMD_TITLE", title.constData(), 1);
        // No drives here: the root of the system is C:\\, in the prompt and for cd.
        setenv("XP_CMD_ROOT_IS_C", "1", 1);
        unsetenv("COLUMNS");
        unsetenv("LINES");
        // This program's own, not for what runs in it
        unsetenv("QT_QPA_PLATFORMTHEME");
        execlp("bash", "bash", "--rcfile", profile.constData(), "-i", static_cast<char *>(nullptr));
        _exit(127);
    }
    m_notifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Console::readOutput);
}

void Console::readOutput()
{
    char buffer[65536];
    const ssize_t got = ::read(m_master, buffer, sizeof buffer);
    if (got <= 0) {
        if (got < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        // The shell has gone: as XP's window, this one goes with it.
        m_notifier->setEnabled(false);
        m_child = -1;
        close();
        return;
    }
    feed(buffer, got);
    m_cursorOn = true;
    update();
}

void Console::send(const QByteArray &bytes)
{
    if (m_master < 0)
        return;
    qint64 done = 0;
    while (done < bytes.size()) {
        const ssize_t wrote = ::write(m_master, bytes.constData() + done, size_t(bytes.size() - done));
        if (wrote < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        done += wrote;
    }
}

Console::Line Console::blankLine() const
{
    Cell cell;
    cell.fg = m_fg;
    cell.bg = m_bg;
    return Line(COLUMNS, cell);
}

Console::Line &Console::lineAt(int line)
{
    return line < m_history.size() ? m_history[line] : line - m_history.size() < ROWS ? m_screen[line - m_history.size()] : m_empty;
}

const Console::Line &Console::lineAt(int line) const
{
    // Below what was written, the buffer is empty.
    return line < m_history.size() ? m_history[line] : line - m_history.size() < ROWS ? m_screen[line - m_history.size()] : m_empty;
}

void Console::feed(const char *data, qint64 size)
{
    // Looking at where the output goes, the window follows it.
    const bool following = m_scrollBar->value() == m_history.size();
    for (qint64 i = 0; i < size; ++i) {
        const uchar byte = uchar(data[i]);
        char32_t ch;
        // UTF-8, whatever size the reads cut it in
        if (m_pendingBytes) {
            if ((byte & 0xc0) != 0x80) {
                m_pendingBytes = 0;
                --i;
                continue;
            }
            m_pending = (m_pending << 6) | (byte & 0x3f);
            if (--m_pendingBytes)
                continue;
            ch = m_pending;
        } else if (byte < 0x80) {
            ch = byte;
        } else if ((byte & 0xe0) == 0xc0) {
            m_pending = byte & 0x1f;
            m_pendingBytes = 1;
            continue;
        } else if ((byte & 0xf0) == 0xe0) {
            m_pending = byte & 0x0f;
            m_pendingBytes = 2;
            continue;
        } else if ((byte & 0xf8) == 0xf0) {
            m_pending = byte & 0x07;
            m_pendingBytes = 3;
            continue;
        } else {
            continue;
        }

        switch (m_state) {
        case Ground:
            if (ch == 0x1b)
                m_state = Escape;
            else if (ch < 0x20 || ch == 0x7f)
                control(ch);
            else
                put(ch);
            break;
        case Escape:
            escape(ch);
            break;
        case Csi:
            if (ch >= 0x40 && ch <= 0x7e) {
                m_state = Ground;
                csi(ch);
            } else if (ch < 0x20) {
                control(ch);
            } else {
                m_sequence.append(char(ch));
            }
            break;
        case Osc:
            if (ch == 0x07) {
                m_state = Ground;
                if (m_sequence.startsWith("0;") || m_sequence.startsWith("2;"))
                    setWindowTitle(QString::fromUtf8(m_sequence.mid(2)));
            } else if (ch == 0x1b) {
                m_state = OscEscape;
            } else if (ch < 0x80 && m_sequence.size() < 4096) {
                m_sequence.append(char(ch));
            } else if (m_sequence.size() < 4096) {
                m_sequence.append(QString::fromUcs4(&ch, 1).toUtf8());
            }
            break;
        case OscEscape:
            m_state = Ground;
            if (m_sequence.startsWith("0;") || m_sequence.startsWith("2;"))
                setWindowTitle(QString::fromUtf8(m_sequence.mid(2)));
            break;
        case Charset:
            m_state = Ground;
            if (m_sequence == "(")
                m_lineDrawing = ch == U'0';
            break;
        }
    }
    // A full-screen program has no buffer to move in.
    m_scrollBar->setEnabled(!m_alternate);
    if (following || m_alternate)
        m_scrollBar->setValue(int(m_history.size()));
}

void Console::control(char32_t ch)
{
    switch (ch) {
    case 0x08:
        if (m_column > 0)
            --m_column;
        m_wrapPending = false;
        break;
    case 0x09:
        m_column = qMin(COLUMNS - 1, (m_column / 8 + 1) * 8);
        m_wrapPending = false;
        break;
    case 0x0a:
    case 0x0b:
    case 0x0c:
        lineFeed();
        break;
    case 0x0d:
        m_column = 0;
        m_wrapPending = false;
        break;
    case 0x0e:
    case 0x0f:
        // Shift Out and In: the line drawing set, for the programs that use G1
        m_lineDrawing = ch == 0x0e;
        break;
    default:
        break;
    }
}

void Console::escape(char32_t ch)
{
    m_state = Ground;
    switch (ch) {
    case U'[':
        m_state = Csi;
        m_sequence.clear();
        break;
    case U']':
        m_state = Osc;
        m_sequence.clear();
        break;
    case U'(':
    case U')':
    case U'*':
    case U'+':
        m_state = Charset;
        m_sequence = QByteArray(1, char(ch));
        break;
    case U'7':
        m_savedRow = m_row;
        m_savedColumn = m_column;
        break;
    case U'8':
        m_row = m_savedRow;
        m_column = m_savedColumn;
        m_wrapPending = false;
        break;
    case U'D':
        lineFeed();
        break;
    case U'E':
        m_column = 0;
        lineFeed();
        break;
    case U'M':
        reverseLineFeed();
        break;
    case U'c':
        m_fg = DEFAULT_FG;
        m_bg = DEFAULT_BG;
        m_bold = m_inverse = false;
        m_top = 0;
        m_bottom = ROWS - 1;
        m_row = m_column = 0;
        for (Line &line : m_screen)
            line = blankLine();
        break;
    default:
        break;
    }
}

void Console::csi(char32_t final)
{
    QByteArray body = m_sequence;
    const bool isPrivate = body.startsWith('?');
    if (isPrivate)
        body.remove(0, 1);
    // ">" and the like: queries of what this is not
    if (body.startsWith('>') || body.startsWith('='))
        return;
    QList<int> arguments;
    for (const QByteArray &field : body.split(';'))
        arguments.append(field.toInt());
    const auto argument = [&](int i, int fallback) {
        const int value = arguments.value(i, 0);
        return value ? value : fallback;
    };
    const int n = argument(0, 1);
    m_wrapPending = false;
    switch (final) {
    case U'A':
        m_row = qMax(m_row >= m_top ? m_top : 0, m_row - n);
        break;
    case U'B':
    case U'e':
        m_row = qMin(m_row <= m_bottom ? m_bottom : ROWS - 1, m_row + n);
        break;
    case U'C':
    case U'a':
        m_column = qMin(COLUMNS - 1, m_column + n);
        break;
    case U'D':
        m_column = qMax(0, m_column - n);
        break;
    case U'E':
        m_row = qMin(ROWS - 1, m_row + n);
        m_column = 0;
        break;
    case U'F':
        m_row = qMax(0, m_row - n);
        m_column = 0;
        break;
    case U'G':
    case U'`':
        m_column = qBound(0, n - 1, COLUMNS - 1);
        break;
    case U'd':
        m_row = qBound(0, n - 1, ROWS - 1);
        break;
    case U'H':
    case U'f':
        m_row = qBound(0, argument(0, 1) - 1, ROWS - 1);
        m_column = qBound(0, argument(1, 1) - 1, COLUMNS - 1);
        break;
    case U'J': {
        const int mode = arguments.value(0, 0);
        if (mode == 0) {
            erase(m_row, m_column, COLUMNS);
            for (int row = m_row + 1; row < ROWS; ++row)
                erase(row, 0, COLUMNS);
        } else if (mode == 1) {
            for (int row = 0; row < m_row; ++row)
                erase(row, 0, COLUMNS);
            erase(m_row, 0, m_column + 1);
        } else if (mode == 2) {
            for (int row = 0; row < ROWS; ++row)
                erase(row, 0, COLUMNS);
        } else if (mode == 3) {
            m_history.clear();
            clearSelection();
        }
        break;
    }
    case U'K': {
        const int mode = arguments.value(0, 0);
        if (mode == 0)
            erase(m_row, m_column, COLUMNS);
        else if (mode == 1)
            erase(m_row, 0, m_column + 1);
        else
            erase(m_row, 0, COLUMNS);
        break;
    }
    case U'L':
        if (m_row >= m_top && m_row <= m_bottom) {
            const int top = m_top;
            m_top = m_row;
            scrollDown(n);
            m_top = top;
        }
        break;
    case U'M':
        if (m_row >= m_top && m_row <= m_bottom) {
            const int top = m_top;
            m_top = m_row;
            // (Inside the screen: nothing of it goes to the history.)
            const bool alternate = m_alternate;
            m_alternate = true;
            scrollUp(n);
            m_alternate = alternate;
            m_top = top;
        }
        break;
    case U'@': {
        Line &line = m_screen[m_row];
        for (int i = 0; i < n && m_column < COLUMNS; ++i) {
            line.insert(m_column, blankLine().first());
            line.removeLast();
        }
        break;
    }
    case U'P': {
        Line &line = m_screen[m_row];
        for (int i = 0; i < n && m_column < COLUMNS; ++i) {
            line.remove(m_column);
            line.append(blankLine().first());
        }
        break;
    }
    case U'X':
        erase(m_row, m_column, qMin(COLUMNS, m_column + n));
        break;
    case U'S':
        scrollUp(n);
        break;
    case U'T':
        scrollDown(n);
        break;
    case U'm':
        if (!isPrivate)
            sgr(arguments);
        break;
    case U'r':
        m_top = qBound(0, argument(0, 1) - 1, ROWS - 1);
        m_bottom = qBound(0, argument(1, ROWS) - 1, ROWS - 1);
        if (m_top >= m_bottom) {
            m_top = 0;
            m_bottom = ROWS - 1;
        }
        m_row = m_top;
        m_column = 0;
        break;
    case U'h':
    case U'l':
        if (isPrivate)
            privateMode(arguments, final == U'h');
        break;
    case U's':
        m_savedRow = m_row;
        m_savedColumn = m_column;
        break;
    case U'u':
        m_row = m_savedRow;
        m_column = m_savedColumn;
        break;
    case U'n':
        if (arguments.value(0) == 6)
            send(QStringLiteral("\x1b[%1;%2R").arg(m_row + 1).arg(m_column + 1).toLatin1());
        else if (arguments.value(0) == 5)
            send("\x1b[0n");
        break;
    case U'c':
        send("\x1b[?1;2c");
        break;
    default:
        break;
    }
}

void Console::sgr(const QList<int> &arguments)
{
    for (int i = 0; i < arguments.size(); ++i) {
        const int code = arguments[i];
        if (code == 0) {
            m_fg = DEFAULT_FG;
            m_bg = DEFAULT_BG;
            m_bold = m_inverse = false;
        } else if (code == 1) {
            m_bold = true;
        } else if (code == 22) {
            m_bold = false;
        } else if (code == 7) {
            m_inverse = true;
        } else if (code == 27) {
            m_inverse = false;
        } else if (code >= 30 && code <= 37) {
            m_fg = uchar(code - 30);
        } else if (code == 39) {
            m_fg = DEFAULT_FG;
        } else if (code >= 40 && code <= 47) {
            m_bg = uchar(code - 40);
        } else if (code == 49) {
            m_bg = DEFAULT_BG;
        } else if (code >= 90 && code <= 97) {
            m_fg = uchar(code - 90 + 8);
        } else if (code >= 100 && code <= 107) {
            m_bg = uchar(code - 100 + 8);
        } else if ((code == 38 || code == 48) && i + 1 < arguments.size()) {
            int colour = -1;
            if (arguments[i + 1] == 5 && i + 2 < arguments.size()) {
                colour = colourOf256(arguments[i + 2]);
                i += 2;
            } else if (arguments[i + 1] == 2 && i + 4 < arguments.size()) {
                colour = nearestColour(arguments[i + 2], arguments[i + 3], arguments[i + 4]);
                i += 4;
            }
            if (colour >= 0)
                (code == 38 ? m_fg : m_bg) = uchar(colour);
        }
    }
}

void Console::privateMode(const QList<int> &arguments, bool on)
{
    for (int mode : arguments)
        switch (mode) {
        case 1:
            m_applicationCursor = on;
            break;
        case 7:
            m_autoWrap = on;
            break;
        case 25:
            m_cursorVisible = on;
            break;
        case 47:
        case 1047:
        case 1049:
            setAlternate(on);
            break;
        default:
            break;
        }
}

// Full-screen programs draw on a screen of their own, and the prompt's comes back after them.
void Console::setAlternate(bool on)
{
    if (on == m_alternate)
        return;
    m_alternate = on;
    if (on) {
        m_saved = m_screen;
        m_savedRow = m_row;
        m_savedColumn = m_column;
        for (Line &line : m_screen)
            line = blankLine();
        m_row = m_column = 0;
    } else {
        m_screen = m_saved;
        m_row = m_savedRow;
        m_column = m_savedColumn;
    }
    m_top = 0;
    m_bottom = ROWS - 1;
    m_wrapPending = false;
    clearSelection();
}

void Console::put(char32_t ch)
{
    if (m_lineDrawing && ch >= 0x60 && ch <= 0x7e)
        ch = LINE_DRAWING[ch - 0x60];
    if (m_wrapPending) {
        m_wrapPending = false;
        if (m_autoWrap) {
            m_column = 0;
            lineFeed();
        }
    }
    uchar fg = m_fg, bg = m_bg;
    // Bold is the bright one of the eight, as the console has no other.
    if (m_bold && fg < 8)
        fg = uchar(fg + 8);
    if (m_inverse)
        std::swap(fg, bg);
    Cell &cell = m_screen[m_row][m_column];
    cell.ch = ch;
    cell.fg = fg;
    cell.bg = bg;
    if (m_column == COLUMNS - 1)
        m_wrapPending = true;
    else
        ++m_column;
}

void Console::lineFeed()
{
    m_wrapPending = false;
    if (m_row == m_bottom)
        scrollUp(1);
    else if (m_row < ROWS - 1)
        ++m_row;
}

void Console::reverseLineFeed()
{
    m_wrapPending = false;
    if (m_row == m_top)
        scrollDown(1);
    else if (m_row > 0)
        --m_row;
}

void Console::scrollUp(int count)
{
    for (int i = 0; i < count && i <= m_bottom - m_top; ++i) {
        Line line = m_screen.takeAt(m_top);
        // What leaves the top of the whole screen stays in the history, as XP's 300 lines.
        if (m_top == 0 && !m_alternate) {
            m_history.append(line);
            if (m_history.size() > HISTORY) {
                m_history.removeFirst();
                clearSelection();
            }
        }
        m_screen.insert(m_bottom, blankLine());
    }
}

void Console::scrollDown(int count)
{
    for (int i = 0; i < count && i <= m_bottom - m_top; ++i) {
        m_screen.removeAt(m_bottom);
        m_screen.insert(m_top, blankLine());
    }
}

void Console::erase(int line, int from, int to)
{
    const Cell blank = blankLine().first();
    for (int column = from; column < to && column < COLUMNS; ++column)
        m_screen[line][column] = blank;
}

void Console::paintEvent(QPaintEvent *)
{
    QImage frame(COLUMNS * m_glyphWidth, ROWS * m_glyphHeight, QImage::Format_RGB32);
    const int firstLine = m_scrollBar->value();
    const bool bottom = firstLine == m_history.size();
    const QImage *unknown = m_glyphs.isEmpty() ? nullptr : &m_glyphs[U'?'];
    for (int row = 0; row < ROWS; ++row) {
        const int line = firstLine + row;
        const Line &cells = lineAt(line);
        for (int column = 0; column < COLUMNS; ++column) {
            const Cell &cell = cells[column];
            QRgb fg = PALETTE[cell.fg & 15], bg = PALETTE[cell.bg & 15];
            if (selected(line, column))
                std::swap(fg, bg);
            const auto found = m_glyphs.constFind(cell.ch);
            const QImage *glyph = found != m_glyphs.constEnd() ? &*found : unknown;
            const int x0 = column * m_glyphWidth, y0 = row * m_glyphHeight;
            for (int y = 0; y < m_glyphHeight; ++y) {
                auto *pixels = reinterpret_cast<QRgb *>(frame.scanLine(y0 + y)) + x0;
                const uchar *mask = glyph && y < glyph->height() ? glyph->constScanLine(y) : nullptr;
                const bool space = cell.ch == U' ';
                for (int x = 0; x < m_glyphWidth; ++x)
                    pixels[x] = !space && mask && x < glyph->width() && mask[x] ? fg : bg;
            }
        }
    }
    if (bottom && m_cursorVisible && m_cursorOn) {
        const QRgb colour = PALETTE[m_screen[m_row][m_column].fg & 15];
        for (int y = m_glyphHeight - CURSOR_LINES; y < m_glyphHeight; ++y) {
            auto *pixels = reinterpret_cast<QRgb *>(frame.scanLine(m_row * m_glyphHeight + y)) + m_column * m_glyphWidth;
            for (int x = 0; x < m_glyphWidth; ++x)
                pixels[x] = colour;
        }
    }
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.drawImage(0, 0, frame);
}

void Console::resizeEvent(QResizeEvent *)
{
    m_scrollBar->setGeometry(width() - SCROLL_BAR, 0, SCROLL_BAR, height());
}

bool Console::event(QEvent *event)
{
    // Tab is for the shell here, not for moving between widgets.
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            keyPressEvent(key);
            return true;
        }
    }
    return QWidget::event(event);
}

void Console::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    // Shift+Page Up and Down: the history, not the program
    if ((modifiers & Qt::ShiftModifier) && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
        m_scrollBar->setValue(m_scrollBar->value() + (event->key() == Qt::Key_PageUp ? -ROWS : ROWS));
        return;
    }
    QByteArray out;
    const QByteArray cursorIntro = m_applicationCursor ? "\x1bO" : "\x1b[";
    switch (event->key()) {
    case Qt::Key_Up: out = cursorIntro + 'A'; break;
    case Qt::Key_Down: out = cursorIntro + 'B'; break;
    case Qt::Key_Right: out = cursorIntro + 'C'; break;
    case Qt::Key_Left: out = cursorIntro + 'D'; break;
    case Qt::Key_Home: out = cursorIntro + 'H'; break;
    case Qt::Key_End: out = cursorIntro + 'F'; break;
    case Qt::Key_PageUp: out = "\x1b[5~"; break;
    case Qt::Key_PageDown: out = "\x1b[6~"; break;
    case Qt::Key_Insert: out = "\x1b[2~"; break;
    case Qt::Key_Delete: out = "\x1b[3~"; break;
    case Qt::Key_Backspace: out = "\x7f"; break;
    case Qt::Key_Return:
    case Qt::Key_Enter: out = "\r"; break;
    case Qt::Key_Tab: out = "\t"; break;
    case Qt::Key_Backtab: out = "\x1b[Z"; break;
    case Qt::Key_Escape: out = "\x1b"; break;
    case Qt::Key_F1: out = "\x1bOP"; break;
    case Qt::Key_F2: out = "\x1bOQ"; break;
    case Qt::Key_F3: out = "\x1bOR"; break;
    case Qt::Key_F4: out = "\x1bOS"; break;
    case Qt::Key_F5: out = "\x1b[15~"; break;
    case Qt::Key_F6: out = "\x1b[17~"; break;
    case Qt::Key_F7: out = "\x1b[18~"; break;
    case Qt::Key_F8: out = "\x1b[19~"; break;
    case Qt::Key_F9: out = "\x1b[20~"; break;
    case Qt::Key_F10: out = "\x1b[21~"; break;
    case Qt::Key_F11: out = "\x1b[23~"; break;
    case Qt::Key_F12: out = "\x1b[24~"; break;
    default:
        if ((modifiers & Qt::ControlModifier) && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
            out = QByteArray(1, char(event->key() - Qt::Key_A + 1));
        else
            out = event->text().toUtf8();
        break;
    }
    if (out.isEmpty())
        return;
    if (modifiers & Qt::AltModifier)
        out.prepend('\x1b');
    m_scrollBar->setValue(int(m_history.size()));
    send(out);
}

void Console::wheelEvent(QWheelEvent *event)
{
    m_scrollBar->setValue(m_scrollBar->value() - event->angleDelta().y() / 120 * WHEEL_LINES);
}

QPoint Console::cellAt(QPoint position) const
{
    return QPoint(qBound(0, position.x() / m_glyphWidth, COLUMNS - 1),
                  qBound(0, position.y() / m_glyphHeight, ROWS - 1) + m_scrollBar->value());
}

bool Console::selected(int line, int column) const
{
    if (!m_hasSelection)
        return false;
    QPoint start = m_selectionStart, end = m_selectionEnd;
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x()))
        std::swap(start, end);
    if (line < start.y() || line > end.y())
        return false;
    if (line == start.y() && column < start.x())
        return false;
    return line != end.y() || column <= end.x();
}

QString Console::selectionText() const
{
    QString out;
    const int lines = int(m_history.size()) + ROWS;
    for (int line = 0; line < lines; ++line) {
        QString text;
        bool any = false;
        for (int column = 0; column < COLUMNS; ++column)
            if (selected(line, column)) {
                any = true;
                const char32_t ch = lineAt(line)[column].ch;
                text += QString::fromUcs4(&ch, 1);
            }
        if (any) {
            while (text.endsWith(QLatin1Char(' ')))
                text.chop(1);
            out += (out.isEmpty() ? QString() : QStringLiteral("\n")) + text;
        }
    }
    return out;
}

void Console::clearSelection()
{
    m_hasSelection = m_selecting = false;
}

// As XP's QuickEdit: drag to mark, a right click copies what is marked or else pastes.
void Console::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selectionStart = m_selectionEnd = cellAt(event->position().toPoint());
        update();
    } else if (event->button() == Qt::RightButton) {
        if (m_hasSelection) {
            QApplication::clipboard()->setText(selectionText());
            clearSelection();
            update();
        } else {
            send(QApplication::clipboard()->text().toUtf8());
        }
    }
}

void Console::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting)
        return;
    m_selectionEnd = cellAt(event->position().toPoint());
    m_hasSelection = m_selectionEnd != m_selectionStart;
    update();
}

void Console::mouseReleaseEvent(QMouseEvent *)
{
    m_selecting = false;
}

void Console::closeEvent(QCloseEvent *event)
{
    if (m_child > 0) {
        ::kill(m_child, SIGHUP);
        m_child = -1;
    }
    event->accept();
}
