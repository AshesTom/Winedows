// SPDX-License-Identifier: LGPL-2.1-or-later
// What every program of the shell does at its start (explorer, notepad, calc, cmd, shimgvw):
// X11 with XP's hinting, then XP's fonts, its look and its icons from the user's files.
#pragma once

class QApplication;
class QString;

namespace xpapp {

// Before the QApplication exists
void prepare();
// After it: NAME is the program's WM_CLASS, as XP's taskbar tells programs apart by their file.
// False, after saying so, when luna.msstyles was never extracted.
bool start(QApplication &app, const char *name);
// A program beside this one (.../bin/NAME)
QString sibling(const QString &name);

} // namespace xpapp
