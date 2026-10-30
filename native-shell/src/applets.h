// SPDX-License-Identifier: LGPL-2.1-or-later
// The Control Panel's applets, as Linux dialogs: System, Date and Time, Sounds,
// Mouse, Keyboard, User Accounts, Folder Options, Add or Remove Programs, Network.
#pragma once

#include <QString>

namespace applets {

void open(const QString &id);
// What the Mouse applet chose, again at the start of a session.
void applyInputSettings();

} // namespace applets
