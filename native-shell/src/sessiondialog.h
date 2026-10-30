// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's "Turn off computer": its dialog from msgina.dll over the screen fading to grey,
// and "Log Off Windows". Both only ask: Taskbar::endSession does what was chosen.
#pragma once

class QString;
class QWidget;

namespace sessiondialog {

enum Choice { Cancelled, StandBy, TurnOff, Restart };

Choice askTurnOff(bool canStandBy);
bool askLogOff();
// The blue picture with TEXT on it, in the middle of the screen, while a theme changes. Null
// without msgina.dll's picture. The caller deletes it.
QWidget *showPleaseWait(const QString &text);

} // namespace sessiondialog
