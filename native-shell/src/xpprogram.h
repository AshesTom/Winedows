// SPDX-License-Identifier: LGPL-2.1-or-later
// What XP's accessories share: their menu bar and their keys, from the program's own resources
// (RT_MENU and RT_ACCELERATOR of the user's notepad.exe or calc.exe).
#pragma once

#include "msstyles.h"

#include <QHash>
#include <QKeySequence>
#include <QList>

class QAction;
class QKeyEvent;
class QMenu;
class QMenuBar;

namespace xpprogram {

// The popups and items of menu ID in BAR, the items' actions by their command. An item's words
// after the tab are XP's name for its key; checked and greyed items start that way.
QHash<int, QAction *> fillMenuBar(QMenuBar *bar, const w4w::PeResources &pe, int id);
// A shortcut menu: the items of menu ID's first popup, in MENU or MENUEX form, added to MENU.
QHash<int, QAction *> fillMenu(QMenu *menu, const w4w::PeResources &pe, int id);

// An entry of an accelerator table: a virtual key with modifiers, or a character.
struct Accelerator
{
    int command = 0;
    bool virtualKey = false;
    int key = 0;                      // Qt::Key, or the character's code
    Qt::KeyboardModifiers modifiers;  // virtual keys only
};
QList<Accelerator> accelerators(const w4w::PeResources &pe, const QString &table);
// The virtual keys of COMMAND as Qt's shortcuts.
QList<QKeySequence> shortcuts(const QList<Accelerator> &table, int command);
// The command a key press means, or 0.
int command(const QList<Accelerator> &table, const QKeyEvent *event);

} // namespace xpprogram
