// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Desktop Cleanup: explorer.exe's check every 60 days, fldrclnr.dll's "There are unused icons on
// your desktop" reminder and its Desktop Cleanup Wizard, which moves the shortcuts chosen to an
// Unused Desktop Shortcuts folder on the desktop. Words, pictures and pages are fldrclnr.dll's.
#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QString>

namespace desktopcleanup {

// A shortcut of the desktop: a desktop entry of its folder, or Internet Explorer (the default
// browser) when the desktop shows it
struct Shortcut
{
    QString path;         // the entry's file, or the desktop item's key
    QString name;
    QImage icon;          // 16 pixels
    QDateTime lastUsed;   // invalid if never run
    bool unused = false;  // not run for the days between cleanups
};

// explorer.exe's test at logon (sub_100f010): not turned off (CleanupWiz\NoRun) and the days between
// cleanups (60 unless set) gone since the last one. The first time counts that last one 30 days back.
bool due();
// Display Properties' "Run Desktop Cleanup Wizard every 60 days"
bool enabled();
void setEnabled(bool enabled);
// The desktop's shortcuts (fldrclnr's sub_693d3bef): only the unused ones unless ALL, as Clean Desktop
// Now lists them
QList<Shortcut> shortcuts(bool all);
// What fldrclnr writes once it has run, whatever was chosen: the time (Last used time) and the days
void stamp();
// The reminder's notification icon (fldrclnr's 100), its title (string 6) and its text (7)
QImage reminderIcon(int size);
QString reminderTitle();
QString reminderText();
// The wizard, a window of its own as rundll32's would be: SHORTCUTS listed, the unused ones ticked
void showWizard(const QList<Shortcut> &shortcuts);

} // namespace desktopcleanup
