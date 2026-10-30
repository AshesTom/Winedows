// SPDX-License-Identifier: LGPL-2.1-or-later
// An XP dialog resource as Qt widgets: the controls, their texts and their
// places come from the user's XP file, in the language of that XP.
#pragma once

#include "msstyles.h"

#include <QHash>
#include <QWidget>

namespace xpdialog {

struct Page
{
    QString caption;
    QHash<int, QWidget *> controls;   // by the control's ID
};

// Fills PARENT with dialog ID of PE (an extended template, DLGTEMPLATEEX) and sizes it.
Page build(const w4w::PeResources &pe, int id, QWidget *parent);

} // namespace xpdialog
