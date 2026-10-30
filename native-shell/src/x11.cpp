// SPDX-License-Identifier: LGPL-2.1-or-later
#include "x11.h"

#include <QGuiApplication>
#include <QHash>

#include <cstdlib>
#include <cstring>

namespace x11 {

xcb_connection_t *connection()
{
    return qApp->nativeInterface<QNativeInterface::QX11Application>()->connection();
}

xcb_window_t root()
{
    static const xcb_window_t window = xcb_setup_roots_iterator(xcb_get_setup(connection())).data->root;
    return window;
}

xcb_atom_t atom(const char *name)
{
    static QHash<QByteArray, xcb_atom_t> atoms;
    const QByteArray key(name);
    auto it = atoms.constFind(key);
    if (it != atoms.cend())
        return *it;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        connection(), xcb_intern_atom(connection(), 0, key.size(), name), nullptr);
    const xcb_atom_t value = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
    free(reply);
    atoms.insert(key, value);
    return value;
}

static xcb_get_property_reply_t *read(xcb_window_t window, xcb_atom_t name)
{
    xcb_generic_error_t *error = nullptr;
    // 4 MB: the largest icon lists stay well under it.
    auto *reply = xcb_get_property_reply(
        connection(), xcb_get_property(connection(), 0, window, name, XCB_ATOM_ANY, 0, 1 << 20), &error);
    free(error);
    return reply;
}

QByteArray property(xcb_window_t window, xcb_atom_t name)
{
    auto *reply = read(window, name);
    if (!reply)
        return {};
    QByteArray out(static_cast<const char *>(xcb_get_property_value(reply)), xcb_get_property_value_length(reply));
    free(reply);
    return out;
}

QList<quint32> property32(xcb_window_t window, xcb_atom_t name)
{
    QList<quint32> out;
    auto *reply = read(window, name);
    if (!reply)
        return out;
    if (reply->format == 32) {
        out.resize(xcb_get_property_value_length(reply) / 4);
        if (!out.isEmpty())
            memcpy(out.data(), xcb_get_property_value(reply), out.size() * 4);
    }
    free(reply);
    return out;
}

void setProperty32(xcb_window_t window, const char *name, xcb_atom_t type, const QList<quint32> &values)
{
    xcb_change_property(connection(), XCB_PROP_MODE_REPLACE, window, atom(name), type, 32, values.size(), values.constData());
}

void setString(xcb_window_t window, const char *name, const QByteArray &value)
{
    xcb_change_property(connection(), XCB_PROP_MODE_REPLACE, window, atom(name), XCB_ATOM_STRING, 8, value.size(), value.constData());
}

void watchProperties(xcb_window_t window)
{
    // Added to what this client already selects: on the shell's own windows and on the
    // root, that is Qt's, which would lose its Expose events and all.
    xcb_generic_error_t *error = nullptr;
    auto *attributes = xcb_get_window_attributes_reply(connection(), xcb_get_window_attributes(connection(), window), &error);
    free(error);
    if (!attributes)
        return;
    const quint32 mask = attributes->your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE;
    free(attributes);
    xcb_discard_reply(connection(),
                      xcb_change_window_attributes_checked(connection(), window, XCB_CW_EVENT_MASK, &mask).sequence);
}

void sendToManager(xcb_window_t window, const char *type, const QList<quint32> &data)
{
    xcb_client_message_event_t event;
    memset(&event, 0, sizeof(event));
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = window;
    event.type = atom(type);
    for (int i = 0; i < data.size() && i < 5; ++i)
        event.data.data32[i] = data[i];
    xcb_send_event(connection(), 0, root(), XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(connection());
}

} // namespace x11
