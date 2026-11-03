// SPDX-License-Identifier: LGPL-2.1-or-later
// The Control Panel's applets, as Linux dialogs: System, Date and Time, Sounds,
// Mouse, Keyboard, User Accounts, Folder Options, Add or Remove Programs, Network,
// Security Center.
#pragma once

#include <QImage>
#include <QString>

namespace applets {

void open(const QString &id);
// shell32's About box for a program (ShellAbout): its name and its 32-pixel icon.
void about(const QString &program, const QImage &icon);
// What the Mouse applet chose, again at the start of a session.
void applyInputSettings();

} // namespace applets
