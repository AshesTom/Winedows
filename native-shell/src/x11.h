// SPDX-License-Identifier: LGPL-2.1-or-later
// The few X requests the shell makes itself. Windows of other programs can go
// at any moment, so each request here tolerates that: no X error reaches Qt's log.
#pragma once

#include <QByteArray>
#include <QList>
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

} // namespace x11
