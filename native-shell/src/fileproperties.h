// SPDX-License-Identifier: LGPL-2.1-or-later
// A file's or a folder's Properties, as shell32's property sheet: its General page (dialog 1041 for a
// file, 1044 for a folder, 1042 for several items) and, for a program file with version information,
// its Version page (dialog 1043), and for a folder its Customize page (dialog 1124), with shell32's words.
#pragma once

#include <QStringList>

class QWidget;

namespace fileproperties {

enum Page { GeneralPage, CustomizePage };
// START the page shown first (a folder's Customize from Customize This Folder)
void show(const QStringList &paths, QWidget *parent, Page start = GeneralPage);

} // namespace fileproperties
