// SPDX-License-Identifier: LGPL-2.1-or-later
// shell32.dll's Folder Options: its General (dialog 29959), View (29960) and File Types (1099)
// pages, the View page's Advanced settings tree filled from shell32's own REGINST resource as
// browseui's RegTreeOptions fills it from the registry, and the settings the folder windows read.
#pragma once

#include <QString>

#include <functional>

class QFileInfo;
class QWidget;

namespace folderoptions {

// What the Advanced settings and the General page decide (native-explorer.conf)
bool hideKnownExtensions();      // Advanced\HideFileExt
bool rememberFolderViews();      // Advanced\ClassicViewState, inverted
bool fullPathInTitle();          // CabinetState\FullPath
bool fullPathInAddress();        // CabinetState\FullPathAddress
bool showSystemFolders();        // Advanced\WebViewBarricade
bool separateWindows();          // General: Open each folder in its own window

// A file's name as folder windows and the desktop list it: without its extension when the
// extension's type is known and HideFileExt is on, as shell32 shows it.
QString shownName(const QFileInfo &info);
// The name to give a renamed item: NAME with the extension that was hidden while it was edited.
QString renamedName(const QFileInfo &info, const QString &name);

// The sheet. APPLY_TO_ALL is the folder window's own Apply to All Folders (null: the button is
// greyed, as XP has it when the sheet is not opened from a folder).
void show(QWidget *parent, std::function<void()> applyToAll = {});

} // namespace folderoptions
