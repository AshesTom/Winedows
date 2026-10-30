// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's "Turn off computer": its dialog from msgina.dll over the screen fading to grey,
// and "Log Off Windows". Both only ask: Taskbar::endSession does what was chosen.
#pragma once

namespace sessiondialog {

enum Choice { Cancelled, StandBy, TurnOff, Restart };

Choice askTurnOff(bool canStandBy);
bool askLogOff();

} // namespace sessiondialog
