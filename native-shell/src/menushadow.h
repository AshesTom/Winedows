// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's drop shadow under menus, drop-down lists, tooltips and the taskband's group list: win32k's
// xxxAddShadow puts a layered SysShadow window behind a CS_DROPSHADOW popup (docs/reverse-status.md,
// section 13), five pixels right of and below it. Here a second override-redirect window does it.
#pragma once

namespace menushadow {

// Watches the application's popups and gives each one its shadow. Call once, after QApplication.
void install();

} // namespace menushadow
