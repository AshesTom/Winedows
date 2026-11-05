// SPDX-License-Identifier: LGPL-2.1-or-later
// The few X requests the shell makes itself. Windows of other programs can go
// at any moment, so each request here tolerates that: no X error reaches Qt's log.
#pragma once

#include <QByteArray>
#include <QList>
#include <QRect>
#include <xcb/xcb.h>

namespace x11 {

xcb_connection_t *connection();
xcb_window_t root();
xcb_atom_t atom(const char *name);

QByteArray property(xcb_window_t window, xcb_atom_t name);
QList<quint32> property32(xcb_window_t window, xcb_atom_t name);
void setProperty32(xcb_window_t window, const char *name, xcb_atom_t type, const QList<quint32> &values);
void setString(xcb_window_t window, const char *name, const QByteArray &value);
// The properties of WINDOW (not ours) start to reach the application's event filter.
void watchProperties(xcb_window_t window);
// A client message to the window manager, as EWMH asks for it.
void sendToManager(xcb_window_t window, const char *type, const QList<quint32> &data);
// A client's rectangle on the root with its frame (_NET_FRAME_EXTENTS); null if it has gone.
QRect frameGeometry(xcb_window_t window);
// The key of KEYSYM with MODIFIERS on the root, whatever Caps Lock and Num Lock: its presses come to
// this client (RegisterHotKey). Returns its keycode, or 0 when no key has that keysym.
xcb_keycode_t grabKey(quint32 keysym, quint16 modifiers);
void ungrabKey(xcb_keycode_t key, quint16 modifiers);
// WINDOW's parent; 0 if it has gone.
xcb_window_t parent(xcb_window_t window);
// Moves and sizes a client so that its frame fills OUTER (_NET_MOVERESIZE_WINDOW).
void moveFrame(xcb_window_t window, const QRect &outer);
// How long the user has not touched the keyboard or the mouse (MIT-SCREEN-SAVER); 0 if the server cannot tell.
quint32 idleMilliseconds();

} // namespace x11
