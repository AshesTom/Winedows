// SPDX-License-Identifier: LGPL-2.1-or-later
// The icons of files: XP's for folders, places and unknown files, the icon
// theme's for the rest, cached by type. For the GUI thread only.
#pragma once

#include <QFileInfo>
#include <QIcon>
#include <QImage>

namespace fileicons {

QIcon forFile(const QFileInfo &file);
// An icon of shell32.dll with its 16, 32 and 48 pixel images
QIcon shell(int id);
QImage image(const QFileInfo &file, int size);
// XP's words for a file's type: "File Folder", the type's description otherwise
QString typeName(const QFileInfo &file);
// Bytes as XP's Size column says them: "1,234 KB"
QString sizeText(qint64 bytes);

// shell32.dll's icons by name
enum { Document = 1, Application = 3, Folder = 4, FolderOpen = 5, Drive = 9, Removable = 8, CdRom = 12, Computer = 16, Printer = 17, Search = 23,
       ControlPanel = 22, Help = 24, RecycleEmpty = 32, RecycleFull = 33, Desktop = 35, Documents = 235, Pictures = 236, Music = 237, Go = 290,
       // The Common Tasks' pictures of View system information and Add or remove programs (XP's My Computer)
       SystemInfo = 253, AddRemove = 271,
       // The File and Folder Tasks' and System Tasks' pictures, as XP's task pane shows them
       ShowContents = 20, NewFolder = 319, DeleteTask = 240, MoveTask = 241, RenameTask = 242, CopyTask = 243 };

} // namespace fileicons
