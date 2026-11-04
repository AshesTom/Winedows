// SPDX-License-Identifier: LGPL-2.1-or-later
#include "arrange.h"

#include "captionanimation.h"
#include "theme.h"
#include "windowlist.h"
#include "x11.h"

#include <QGuiApplication>
#include <QScreen>

using x11::atom;

namespace arrange {
namespace {

// SM_CXSIZE: the visual styles leave the caption buttons' width to the scheme, whose is 18.
const int CAPTION_WIDTH = 18;
// How long the frames go without their caption animation once explorer.exe has turned it off: the
// window manager answers the requests a little later than XP's synchronous SC_MINIMIZE.
const int MINIMIZE_ALL_MS = 1000;

bool maximized(xcb_window_t window)
{
    const QList<quint32> states = x11::property32(window, atom("_NET_WM_STATE"));
    return states.contains(atom("_NET_WM_STATE_MAXIMIZED_VERT")) && states.contains(atom("_NET_WM_STATE_MAXIMIZED_HORZ"));
}

// Records where each window is, unmaximizes it and gives it its place.
Undo place(const QList<QPair<xcb_window_t, QRect>> &places)
{
    Undo undo;
    for (const auto &[window, rect] : places) {
        const QRect frame = x11::frameGeometry(window);
        if (frame.isNull())
            continue;
        const bool wasMaximized = maximized(window);
        undo.append({window, frame, wasMaximized});
        if (wasMaximized)
            WindowList::maximize(window, false);
        x11::moveFrame(window, rect);
    }
    return undo;
}

} // namespace

QRect workArea()
{
    const QRect screen = QGuiApplication::primaryScreen()->geometry();
    const QList<quint32> area = x11::property32(x11::root(), atom("_NET_WORKAREA"));
    if (area.size() < 4)
        return screen;
    return QRect(int(area[0]), int(area[1]), int(area[2]), int(area[3])).intersected(screen);
}

Undo cascade(const QList<xcb_window_t> &windows)
{
    // Each window SM_CXFRAME + SM_CXSIZE right of the one under it and SM_CYFRAME + SM_CYSIZE lower;
    // as many steps as a third of the height holds, all the same size, the bottom one at the corner.
    const FrameStyle &frame = ShellTheme::instance()->frame();
    const int dx = frame.border + ShellTheme::instance()->scaled(CAPTION_WIDTH);
    const int dy = frame.top - 1;   // SM_CYFRAME + SM_CYSIZE
    const QRect area = workArea();
    const int steps = area.height() / (3 * dy);
    const QSize size(area.width() - steps * dx, area.height() - steps * dy);
    QList<QPair<xcb_window_t, QRect>> places;
    for (qsizetype k = 0; k < windows.size(); ++k) {
        const int i = int(k % (steps + 1));
        places.append({windows[windows.size() - 1 - k], QRect(area.topLeft() + QPoint(i * dx, i * dy), size)});
    }
    return place(places);
}

Undo tile(const QList<xcb_window_t> &windows, bool horizontal)
{
    const int n = int(windows.size());
    if (!n)
        return {};
    // S is one less than the smallest root over N's square root: 2 under 4 windows, 3 under 9...
    int root = 2;
    if (n >= 4)
        while (root * root <= n)
            ++root;
    const int s = root - 1;
    const int columns = horizontal ? s : n / s;
    const int rows = horizontal ? n / s : s;
    // The last REMAINDER columns have a row more.
    const int remainder = n % s;
    const QRect area = workArea();
    const int width = area.width() / columns;
    QList<QPair<xcb_window_t, QRect>> places;
    int next = 0;
    for (int column = 0; column < columns; ++column) {
        const int left = columns - column;
        const int here = rows + (left <= remainder ? 1 : 0);
        const int height = area.height() / here;
        for (int row = 0; row < here && next < n; ++row)
            places.append({windows[next++], QRect(area.x() + column * width, area.y() + row * height, width, height)});
    }
    return place(places);
}

Undo remember(const QList<xcb_window_t> &windows)
{
    Undo undo;
    for (xcb_window_t window : windows) {
        const QRect frame = x11::frameGeometry(window);
        if (!frame.isNull())
            undo.append({window, frame, maximized(window)});
    }
    return undo;
}

Undo minimizeAll(const QList<xcb_window_t> &windows)
{
    Undo undo;
    for (xcb_window_t window : windows) {
        const QRect frame = x11::frameGeometry(window);
        if (!frame.isNull())
            undo.append({window, frame, maximized(window)});
    }
    captionanimation::pause(MINIMIZE_ALL_MS);
    // the taskband's items from its last button to its first
    for (auto it = windows.crbegin(); it != windows.crend(); ++it)
        WindowList::minimize(*it);
    return undo;
}

void undo(const Undo &placements)
{
    for (const Placement &p : placements) {
        x11::moveFrame(p.window, p.frame);
        if (p.maximized)
            WindowList::maximize(p.window, true);
    }
    // the list from its end, the bottom window first: the top one is shown last, over the others
    captionanimation::pause(MINIMIZE_ALL_MS);
    for (auto it = placements.crbegin(); it != placements.crend(); ++it)
        if (x11::property32(it->window, atom("_NET_WM_STATE")).contains(atom("_NET_WM_STATE_HIDDEN")))
            WindowList::activate(it->window);
}

} // namespace arrange
