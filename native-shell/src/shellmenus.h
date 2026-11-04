// SPDX-License-Identifier: LGPL-2.1-or-later
// What shell32.dll's shortcut menus have that both folder windows and the desktop show: its menus by
// their IDs, Send To (menu 223) with XP's SendTo folder's targets, and shortcuts named as string 4152
// has them ("Shortcut to ...", links here).
#pragma once

#include "msstyles.h"

#include <QStringList>

#include <optional>

class QMenu;
class QWidget;

namespace shellmenus {

// shell32.dll's menus and their commands
enum {
    MENU_ITEM = 210, MENU_PROPERTIES = 211, MENU_BACKGROUND = 215, MENU_DESKTOP = 218, MENU_SEND_TO = 223,
    MENU_RESTORE = 226, MENU_EMPTY_BIN = 227,
    CMD_CUT = 24, CMD_COPY = 25, CMD_PASTE = 26, CMD_SHORTCUT = 16, CMD_DELETE = 17, CMD_RENAME = 18, CMD_PROPERTIES = 19,
    CMD_SEND_TO = 79, CMD_EMPTY_BIN = 141,
    CMD_VIEW = 28674, CMD_ARRANGE = 28673, CMD_GROUPS = 30209, CMD_AUTO_ARRANGE = 28753, CMD_ALIGN = 28756, CMD_REFRESH = 28931,
    CMD_CUSTOMIZE = 28722, CMD_PASTE_HERE = 28698, CMD_PASTE_SHORTCUT = 28700, CMD_UNDO = 28699,
    CMD_FILMSTRIP = 28719, CMD_THUMBNAILS = 28717, CMD_TILES = 28718, CMD_ICONS = 28713, CMD_LIST = 28715, CMD_DETAILS = 28716,
    CMD_SHOW_ICONS = 29698, CMD_LOCK_WEB = 29699, CMD_CLEANUP = 29700,
};
// The verbs' words: Open, Explore, Search...
enum { STRING_OPEN = 8496, STRING_EXPLORE = 8502, STRING_SEARCH = 8503 };

const std::optional<w4w::PeResources> &shell32();
// The items of a file folder's Arrange Icons By: shell32's words and their columns
struct Order
{
    int string;
    const char *fallback;
    int column;   // 0 Name, 1 Size, 2 Type, 3 Date Modified
};
const QList<Order> &orders();

QString shortcutName(const QString &folder, const QString &target);
void makeShortcuts(const QStringList &paths, const QString &folder);
// zipfldr.dll's Send To: a .zip beside them, named after the first
void compressToZip(const QStringList &paths);
// Menu 223, its Send To filled with XP's targets for PATHS.
void addSendTo(QMenu *menu, const QStringList &paths, QWidget *parent);
// The file type's own verbs before Send To: text files' Edit and Print (Notepad and lpr), media's Play.
void addFileVerbs(QMenu *menu, const QStringList &paths, QWidget *parent);

} // namespace shellmenus
