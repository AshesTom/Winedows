// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's window frames for Linux windows, in place of Wine's proxy.exe. It speaks
// proxy.exe's side of bridge-wm's IPC files (docs/architecture.md): a frame is
// an X window of class Win4WineProxy, which the WM keeps just under its client,
// moving each with the other, so bridge-wm/wm.py is the same for both.
#pragma once

#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>

// What a frame shows. Used by the frames themselves and by Display Properties' previews.
enum class FrameButton { None, System, Minimize, Maximize, Close, Help };

struct FrameLook
{
    QString title;
    QImage icon;
    bool active = false, maximized = false, canMaximize = true, canMinimize = true, canHelp = false;
    FrameButton hot = FrameButton::None, pressed = FrameButton::None;
};

// The frame in RECT of PAINTER, its client left unpainted.
void drawFrame(QPainter &painter, const QRect &rect, const FrameStyle &style, const FrameLook &look);
QRect frameButtonRect(FrameButton button, const QRect &rect, const FrameStyle &style, bool maximized);
// Where the frame's art is see-through: its rounded top corners, as the X shape of the window.
QRegion frameShape(const QSize &size, const FrameStyle &style, bool maximized);

class FrameManager;

class FrameWindow : public QWidget
{
    Q_OBJECT
public:
    FrameWindow(FrameManager *manager, quint32 client, const QString &title);
    quint32 client() const { return m_client; }
    const QString &title() const { return m_look.title; }
    bool maximized() const { return m_look.maximized; }
    bool busy() const { return m_drag != Drag::None; }
    void setTitle(const QString &title);
    void setActive(bool active);
    void setMaximized(bool maximized);
    void setIcon(const QImage &icon);
    void themeChanged();
    // The frame around the client's rectangle
    void place(const QRect &client);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Drag { None, Move, Left = 1 << 4, Right = 1 << 5, Top = 1 << 6, Bottom = 1 << 7, Button };
    int edgesAt(QPoint at) const;
    FrameButton buttonAt(QPoint at) const;
    void readHints();
    void showSystemMenu(QPoint at);

    FrameManager *m_manager;
    quint32 m_client;
    FrameLook m_look;
    Drag m_drag = Drag::None;
    int m_edges = 0;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;
    QSize m_minimum;
    bool m_fixedSize = false;
};

class FrameManager : public QObject
{
    Q_OBJECT
public:
    explicit FrameManager(const QString &ipc);
    void command(const char *op, quint32 client);
    void geometryCommand(quint32 client, const QRect &rect);
    void framesChanged();

private:
    void sync();
    void readWindows();
    void readGeometry();
    void readIcons();

    QDir m_ipc;
    QFileSystemWatcher m_watcher;
    QHash<quint32, FrameWindow *> m_frames;
    QHash<quint32, QDateTime> m_iconTimes;
    QByteArray m_windows, m_geometry, m_written;
    int m_commands = 0;
};
