// SPDX-License-Identifier: LGPL-2.1-or-later
// The shortcuts XP's setup puts in the Start menu (syssetup.inf's [StartMenuItems], [StartMenuCommon] and
// its groups, and the components' ProfileItems), each named and drawn from the file the INF gives:
// "DisplayResource" strings and icons of the user's own files.
#pragma once

#include "startmenu.h"

namespace xpshortcuts {

// The Start Menu's own (above All Programs' line), Programs', or a group's under Programs by its path
// ("Accessories", "Accessories/System Tools", "Games", "Startup"); those whose program is missing here
// still show, as dummies, so the menu is XP's.
QList<MenuEntry> entries(const QString &group, int iconSize);
// The groups XP has under GROUP ("" for Programs), in its words
QStringList groups(const QString &group);
// A group's name as XP shows it (shell32's words)
QString groupName(const QString &group);

} // namespace xpshortcuts
