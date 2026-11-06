// SPDX-License-Identifier: LGPL-2.1-or-later
// What a folder's bag keeps (XP's ShellNoRoam\Bags): its type, view and Folders bar, and what shell32's
// Customize page (dialog 1124, 0x7cae1dd5) gives it. That page writes FolderType and Logo to the folder's
// view-state bag (SHGetViewStatePropertyBag "Shell"), or with "Also apply this template to all
// subfolders" to the bag its subfolders inherit (SHGVSPB_INHERIT), and the icon through
// ICustomIconManager, which XP keeps in desktop.ini and the bag keeps here.
#pragma once

#include <QString>

#include <optional>

namespace foldercustomize {

QString bagsPath();
QString bagKey(const QString &folder, const char *value);

// Whether the folder has the page (0x7caa3d31): a folder of the file system, not a drive's root nor one
// of XP's special folders (its CSIDL list at 0x7c9df1d0: Windows, System, Program Files, My Documents,
// My Music, My Pictures, My Videos and their shared ones), which are /usr, /opt and the user's here
bool customizable(const QString &folder);

// The templates of shell32's table at 0x7c9d7020 after its UseLegacyHTT row, which only a folder with
// a desktop.ini Web view template is offered: their FolderType names and their strings (29984-29990)
struct Template
{
    const char *name;
    int string;
};
extern const Template TEMPLATES[7];

// The folder's FolderType: its own, else the one its own or a parent's inherit bag gives; "" for none
QString folderType(const QString &folder);
// The check box: the folder's inherit bag has the type it shows (0x7cae12c3)
bool typeInherited(const QString &folder);
// Logo as the bags give it (as folderType), "" for none
QString logo(const QString &folder);
// The folder's thumbnail picture (0x7ca39615): Logo, against the folder when relative, else its
// folder.jpg or folder.gif; "" for none (its first pictures then). Without WITH_LOGO, those two only.
QString picture(const QString &folder, bool withLogo = true);
// Apply (0x7cae1976, 0x7cae0f9c, 0x7cae184d). A new type drops the bag's view (Mode and Vid), so the
// folder opens in its type's. With SUBFOLDERS the folder's own FolderType, Logo and view go first
// (0x7cae10f7) and the inherit bag takes them. The folder's time is touched for its thumbnail (0x7cae1071).
void apply(const QString &folder, const QString &type, const QString &logo, bool subfolders);

// Change Icon's choice: an icon group of a file of XP's
struct Icon
{
    QString file, group;
};
std::optional<Icon> icon(const QString &folder);
void setIcon(const QString &folder, const std::optional<Icon> &icon);

} // namespace foldercustomize
