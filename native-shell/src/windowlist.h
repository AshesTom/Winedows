// SPDX-License-Identifier: LGPL-2.1-or-later
// The windows that get a taskbar button, from the window manager's EWMH
// properties. It only wakes for X events of those windows.
#pragma once

#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QList>
#include <QObject>
#include <xcb/xcb.h>

struct Task
{
    xcb_window_t window = 0;
    QString title;
    QImage icon;       // 16x16 at the shell's DPI; null for the default one
    bool minimized = false;
    bool attention = false;
};

class WindowList : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit WindowList(int iconSize);
    const QList<Task> &tasks() const { return m_tasks; }
    xcb_window_t active() const { return m_active; }
    // The window that was active when the user pressed on the taskbar: a press
    // first takes the activation away from it (bridge-wm gives the keyboard to
    // the taskbar), and a click on its button is then what minimizes it.
    xcb_window_t activeAtPress() const;

    // The top window with a button, other than EXCEPT: it takes over from a minimized one.
    void activateTopmost(xcb_window_t except) const;
    // The window's _NET_WM_ICON, or its application's icon in the icon theme; may be null.
    static QImage iconOf(xcb_window_t window, int size);
    static void activate(xcb_window_t window);
    static void minimize(xcb_window_t window);
    static void maximize(xcb_window_t window, bool on);
    static void close(xcb_window_t window);

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

Q_SIGNALS:
    void changed();
    void workAreaChanged();

private:
    void readList();
    void readActive();
    bool shown(xcb_window_t window) const;
    void readTitle(Task &task) const;
    void readState(Task &task) const;
    void readIcon(Task &task) const;

    QList<Task> m_tasks;
    xcb_window_t m_active = 0, m_previousActive = 0;
    qint64 m_activeLost = 0;
    int m_iconSize;
};
