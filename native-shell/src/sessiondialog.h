// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's "Turn off computer": its dialog from msgina.dll over the screen fading to grey,
// and "Log Off Windows". Both only ask: Taskbar::endSession does what was chosen.
#pragma once

class QString;
class QWidget;

namespace sessiondialog {

enum Choice { Cancelled, StandBy, TurnOff, Restart, Hibernate, SwitchUser, LogOff };

// msgina's Turn Off Computer: Stand By, Turn Off, Restart (Hibernate with Shift, or alone)
Choice askTurnOff(bool canStandBy, bool canHibernate);
// shell32's Log Off Windows (Switch User, Log Off) when CANSWITCH, else msgina's plain question
Choice askLogOff(bool canSwitch);
// The blue picture with TEXT on it, in the middle of the screen, while a theme changes. Null
// without msgina.dll's picture. The caller deletes it.
QWidget *showPleaseWait(const QString &text);

} // namespace sessiondialog
