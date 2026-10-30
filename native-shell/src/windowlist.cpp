// SPDX-License-Identifier: LGPL-2.1-or-later
#include "windowlist.h"

#include "x11.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QIcon>

using x11::atom;

// How long after losing the activation a press on the taskbar still counts as made while active.
static const qint64 PRESS_WINDOW_MS = 500;
// ICCCM's IconicState, for WM_CHANGE_STATE
static const quint32 ICONIC_STATE = 3;

WindowList::WindowList(int iconSize)
    : m_iconSize(iconSize)
{
    x11::watchProperties(x11::root());
    qApp->installNativeEventFilter(this);
    readList();
    readActive();
}

xcb_window_t WindowList::activeAtPress() const
{
    if (m_active)
        return m_active;
    return QDateTime::currentMSecsSinceEpoch() - m_activeLost < PRESS_WINDOW_MS ? m_previousActive : 0;
}

bool WindowList::shown(xcb_window_t window) const
{
    const QList<quint32> types = x11::property32(window, atom("_NET_WM_WINDOW_TYPE"));
    // The first type it knows counts; Qt puts KDE's own before the standard one.
    for (quint32 type : types) {
        if (type == atom("_KDE_NET_WM_WINDOW_TYPE_OVERRIDE"))
            continue;
        if (type != atom("_NET_WM_WINDOW_TYPE_NORMAL") && type != atom("_NET_WM_WINDOW_TYPE_DIALOG"))
            return false;
        break;
    }
    // Owned windows have no button of their own on XP; those "owned" by what is no
    // client (Qt's dialogs without a parent name its hidden group leader) are not.
    const QList<quint32> owner = x11::property32(window, XCB_ATOM_WM_TRANSIENT_FOR);
    if (!owner.isEmpty() && x11::property32(x11::root(), atom("_NET_CLIENT_LIST")).contains(owner.first()))
        return false;
    if (x11::property32(window, atom("_NET_WM_STATE")).contains(atom("_NET_WM_STATE_SKIP_TASKBAR")))
        return false;
    // Wine maps some of its tooltips as managed windows.
    const QByteArray win32 = x11::property(window, atom("_WIN4WINE_CLASS")).toLower();
    return !win32.startsWith("tooltips_class32");
}

void WindowList::readTitle(Task &task) const
{
    QByteArray name = x11::property(task.window, atom("_NET_WM_NAME"));
    task.title = !name.isEmpty() ? QString::fromUtf8(name) : QString::fromLocal8Bit(x11::property(task.window, XCB_ATOM_WM_NAME));
}

void WindowList::readState(Task &task) const
{
    const QList<quint32> states = x11::property32(task.window, atom("_NET_WM_STATE"));
    task.minimized = states.contains(atom("_NET_WM_STATE_HIDDEN"));
    task.attention = states.contains(atom("_NET_WM_STATE_DEMANDS_ATTENTION"));
}

void WindowList::readIcon(Task &task) const
{
    task.icon = iconOf(task.window, m_iconSize);
}

QImage WindowList::iconOf(xcb_window_t window, int size)
{
    const QList<quint32> data = x11::property32(window, atom("_NET_WM_ICON"));
    // width, height, then its pixels, for each size: the smallest that is large enough.
    int best = -1, bestWidth = 0, bestHeight = 0;
    for (qsizetype at = 0; at + 2 <= data.size();) {
        const int width = int(data[at]), height = int(data[at + 1]);
        if (width <= 0 || height <= 0 || at + 2 + qsizetype(width) * height > data.size())
            break;
        const bool better = best < 0 || (width >= size && (bestWidth < size || width < bestWidth))
            || (bestWidth < size && width > bestWidth);
        if (better) {
            best = int(at + 2);
            bestWidth = width;
            bestHeight = height;
        }
        at += 2 + qsizetype(width) * height;
    }
    if (best >= 0) {
        const QImage image(reinterpret_cast<const uchar *>(data.constData() + best), bestWidth, bestHeight, QImage::Format_ARGB32);
        // (.copy(): scaled() of the same size would still point into DATA.)
        return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();
    }
    // WM_CLASS: "instance\0class\0"; the icon theme often knows the application.
    const QList<QByteArray> names = x11::property(window, XCB_ATOM_WM_CLASS).split('\0');
    for (const QByteArray &name : names) {
        const QIcon themed = name.isEmpty() ? QIcon() : QIcon::fromTheme(QString::fromLocal8Bit(name).toLower());
        if (!themed.isNull())
            return themed.pixmap(size, size).toImage();
    }
    return {};
}

void WindowList::readList()
{
    const QList<quint32> list = x11::property32(x11::root(), atom("_NET_CLIENT_LIST"));
    QList<Task> tasks;
    // The buttons keep the order the windows came in, as on XP.
    for (const Task &task : std::as_const(m_tasks))
        if (list.contains(task.window))
            tasks.append(task);
    for (quint32 window : list) {
        const bool known = std::any_of(tasks.cbegin(), tasks.cend(), [&](const Task &t) { return t.window == window; });
        if (known || !shown(window))
            continue;
        Task task;
        task.window = window;
        x11::watchProperties(window);
        readTitle(task);
        readState(task);
        readIcon(task);
        tasks.append(task);
    }
    m_tasks = tasks;
    Q_EMIT changed();
}

void WindowList::readActive()
{
    const QList<quint32> values = x11::property32(x11::root(), atom("_NET_ACTIVE_WINDOW"));
    const xcb_window_t active = values.isEmpty() ? 0 : values.first();
    if (active == m_active)
        return;
    if (!active) {
        m_previousActive = m_active;
        m_activeLost = QDateTime::currentMSecsSinceEpoch();
    }
    m_active = active;
    Q_EMIT changed();
}

bool WindowList::nativeEventFilter(const QByteArray &type, void *message, qintptr *)
{
    if (type != "xcb_generic_event_t")
        return false;
    auto *event = static_cast<xcb_generic_event_t *>(message);
    if ((event->response_type & 0x7f) != XCB_PROPERTY_NOTIFY)
        return false;
    auto *property = reinterpret_cast<xcb_property_notify_event_t *>(event);
    if (property->window == x11::root()) {
        if (property->atom == atom("_NET_CLIENT_LIST"))
            readList();
        else if (property->atom == atom("_NET_ACTIVE_WINDOW"))
            readActive();
        else if (property->atom == atom("_NET_WORKAREA"))
            Q_EMIT workAreaChanged();
        return false;
    }
    for (Task &task : m_tasks) {
        if (task.window != property->window)
            continue;
        if (property->atom == atom("_NET_WM_NAME") || property->atom == XCB_ATOM_WM_NAME)
            readTitle(task);
        else if (property->atom == atom("_NET_WM_STATE"))
            readState(task);
        else if (property->atom == atom("_NET_WM_ICON"))
            readIcon(task);
        else
            break;
        Q_EMIT changed();
        break;
    }
    return false;
}

void WindowList::activateTopmost(xcb_window_t except) const
{
    const QList<quint32> stack = x11::property32(x11::root(), atom("_NET_CLIENT_LIST_STACKING"));
    for (auto it = stack.crbegin(); it != stack.crend(); ++it)
        for (const Task &task : m_tasks)
            if (task.window == *it && task.window != except && !task.minimized) {
                activate(task.window);
                return;
            }
}

void WindowList::activate(xcb_window_t window)
{
    x11::sendToManager(window, "_NET_ACTIVE_WINDOW", {2, XCB_CURRENT_TIME, 0});
}

void WindowList::minimize(xcb_window_t window)
{
    x11::sendToManager(window, "WM_CHANGE_STATE", {ICONIC_STATE});
}

void WindowList::maximize(xcb_window_t window, bool on)
{
    x11::sendToManager(window, "_NET_WM_STATE", {quint32(on ? 1 : 0), atom("_NET_WM_STATE_MAXIMIZED_VERT"), atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2});
}

void WindowList::close(xcb_window_t window)
{
    x11::sendToManager(window, "_NET_CLOSE_WINDOW", {XCB_CURRENT_TIME, 2});
}
