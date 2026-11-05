// SPDX-License-Identifier: LGPL-2.1-or-later
// The "?" of XP's dialogs: a click on it, then on a control, shows that control's popup from an
// XP help file, as WinHelp's HELP_WM_HELP draws it.
#pragma once

#include "xpdialog.h"

#include <QList>
#include <QPair>
#include <QString>

namespace contexthelp {

// A dialog's aHelpIds table: each control ID with its help number in FILE (-1: the control has
// none, IDH_NO_HELP). The file is looked for among the user's XP files.
void attach(const xpdialog::Page &page, const QList<QPair<int, int>> &ids, const QString &file = QStringLiteral("windows.hlp"));

// The popup of help number ID in FILE at GLOBALPOS (its upper left corner); false if FILE has none.
bool show(const QString &file, int id, const QPoint &globalPos);

} // namespace contexthelp
