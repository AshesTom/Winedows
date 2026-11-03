// SPDX-License-Identifier: LGPL-2.1-or-later
// Cascade and Tile, as XP's user32 lays windows out for the taskbar's menus
// (CascadeWindows and TileWindows on the work area; docs/native-shell.md).
#pragma once

#include <QList>
#include <QRect>
#include <xcb/xcb.h>

namespace arrange {

// Where a window was, for the taskbar menu's Undo.
struct Placement
{
    xcb_window_t window = 0;
    QRect frame;
    bool maximized = false;
};
using Undo = QList<Placement>;

// The primary monitor's work area (_NET_WORKAREA).
QRect workArea();
// WINDOWS are listed from the top of the stack down.
Undo cascade(const QList<xcb_window_t> &windows);
// HORIZONTAL: MDITILE_HORIZONTAL, the windows one above the other.
Undo tile(const QList<xcb_window_t> &windows, bool horizontal);
void undo(const Undo &placements);

} // namespace arrange
