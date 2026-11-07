// SPDX-License-Identifier: LGPL-2.1-or-later
// What every program of the shell does at its start (explorer, notepad, calc, cmd, shimgvw):
// X11 with XP's hinting, then XP's fonts, its look and its icons from the user's files.
#pragma once

class QApplication;
class QString;

namespace xpapp {

// Before the QApplication exists. WEB_PAGES: a program that shows web pages (iexplore, helpctr) keeps
// the session's fontconfig rules, which smooth each face as its gasp table says: Chromium reads its
// fonts' smoothing from fontconfig in this process, and XP's own rules would smooth all of it.
void prepare(bool webPages = false);
// After it: NAME is the program's WM_CLASS, as XP's taskbar tells programs apart by their file.
// False, after saying so, when luna.msstyles was never extracted.
bool start(QApplication &app, const char *name);
// A program beside this one (.../bin/NAME)
QString sibling(const QString &name);

} // namespace xpapp
