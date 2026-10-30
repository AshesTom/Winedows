// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Command Prompt window as a Linux program: 80 x 25 in XP's raster font (vgaoem.fon of
// the user's media) and the console's 16 colours, 300 lines of history, running bash with
// session/xp-cmd.bashrc, the same prompt as the Wine session's. It understands what Linux's
// programs send to an xterm.
#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QTimer>
#include <QWidget>
#include <sys/types.h>

class QScrollBar;
class QSocketNotifier;

class Console : public QWidget
{
public:
    explicit Console(const QString &folder);
    ~Console() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Cell
    {
        char32_t ch = U' ';
        uchar fg = 7, bg = 0;
    };
    using Line = QList<Cell>;
    enum State { Ground, Escape, Csi, Osc, OscEscape, Charset };

    void start(const QString &folder);
    void readOutput();
    void feed(const char *data, qint64 size);
    void control(char32_t ch);
    void escape(char32_t ch);
    void csi(char32_t final);
    void sgr(const QList<int> &arguments);
    void privateMode(const QList<int> &arguments, bool on);
    void put(char32_t ch);
    void lineFeed();
    void reverseLineFeed();
    void scrollUp(int count);
    void scrollDown(int count);
    void erase(int line, int from, int to);
    Line blankLine() const;
    void setAlternate(bool on);
    void send(const QByteArray &bytes);
    QPoint cellAt(QPoint position) const;
    bool selected(int line, int column) const;
    QString selectionText() const;
    void clearSelection();
    Line &lineAt(int line);
    const Line &lineAt(int line) const;

    int m_master = -1;
    pid_t m_child = -1;
    QSocketNotifier *m_notifier = nullptr;
    QScrollBar *m_scrollBar = nullptr;
    QTimer m_blink;
    bool m_cursorOn = true;

    QList<Line> m_screen, m_saved, m_history;
    Line m_empty;
    int m_row = 0, m_column = 0, m_savedRow = 0, m_savedColumn = 0, m_top = 0, m_bottom = 0;
    bool m_wrapPending = false, m_alternate = false, m_cursorVisible = true, m_applicationCursor = false, m_autoWrap = true, m_lineDrawing = false;
    uchar m_fg = 7, m_bg = 0;
    bool m_bold = false, m_inverse = false;

    State m_state = Ground;
    QByteArray m_sequence;
    QByteArray m_utf8;
    char32_t m_pending = 0;
    int m_pendingBytes = 0;

    bool m_selecting = false, m_hasSelection = false;
    QPoint m_selectionStart, m_selectionEnd;   // (column, line in history + screen)

    QHash<char32_t, QImage> m_glyphs;
    int m_glyphWidth = 8, m_glyphHeight = 12;
};
