// SPDX-License-Identifier: LGPL-2.1-or-later
// Taskbar and Start Menu Properties: explorer.exe's sheet (string 810) with its Taskbar page (dialog 6)
// and its Start Menu page (dialog 205), their pictures as explorer.exe picks them, and the settings
// this taskbar has: grouping similar buttons and showing the clock. What it does not have (auto-hide,
// Quick Launch, hiding inactive icons, the classic Start menu, unlocking) is greyed, in the state it is in.
#pragma once

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QString>
#include <QImage>

#include <functional>

class QAbstractButton;
class QCheckBox;
class QLabel;
class QPushButton;

namespace xpdialog {
class SheetTabs;
}

// The taskbar's own settings, as explorer.exe keeps them in the registry (TaskbarGlomming, HideClock),
// here in native-explorer.conf.
struct TaskbarSettings
{
    bool group = true;
    int groupSize = 0;   // TaskbarGroupSize: 0 groups when the buttons no longer fit; 1 the most windows first; 2+ every program with that many
    bool clock = true;
    bool autoHide = false;   // StuckRects2's ABS_AUTOHIDE
    bool quickLaunch = false;
    bool desktopBand = false;   // the Toolbars menu's Desktop band
    QString customToolbar;      // the Toolbars menu's New Toolbar... folder
    bool linksBand = false;     // the Toolbars menu's Links band
    bool locked = true;
    int position = 0;   // Taskbar/Position: 0 bottom, 1 top (left and right are not laid out)
    bool onTop = true;
    bool quickLaunchText = false, quickLaunchTitle = false;   // the band's Show Text and Show Title
    bool quickLaunchLargeIcons = false;   // its View menu's Large Icons (32-pixel icons)
    bool classicStartMenu = false;
    bool classicFavorites = false, classicLogOff = true, classicRun = true, classicSmallIcons = false;
    // The Start panel's Customize (dialogs 1135 and 1036): places not in panelHidden, Favorites,
    // program count, icon size, recent documents and hover-open.
    QStringList panelHidden;
    bool panelFavorites = false, panelSmallIcons = false, panelRecentDocs = true, panelHoverOpen = false;
    int panelMinMFU = 6;
    // explorer.exe's StartButtonBalloonTip: 0 until the Start button is first used, 2 once it has been
    int startButtonBalloonTip = 0;   // the Start menu page's Classic Start menu (SHELLSTATE's fStartPanelOn off)
    bool hideInactive = true;   // Hide inactive icons (EnableAutoTray), on as XP sets it up    // Keep the taskbar on top of other windows (ABS_ALWAYSONTOP)   // Lock the Taskbar (explorer.exe's TaskbarSizeMove, inverted)   // the Quick Launch band in the taskbar's rebar
    static TaskbarSettings load();
    void save() const;
};

// The notification area's items as Customize Notifications lists them: those shown now (Current
// Items) and those seen before (Past Items), by their tooltips, and each one's behaviour.
namespace traynotify {
enum Behavior { HideWhenInactive = 0, AlwaysHide = 1, AlwaysShow = 2 };
Behavior behavior(const QString &name);
void setCurrent(const QStringList &names);
// The icon of an item, as the notification area has it (Past Items have none).
void setIconLookup(std::function<QImage(const QString &)> lookup);
// explorer.exe's dialog 7, over PARENT
void customize(QWidget *parent);
} // namespace traynotify

// Tells the taskbar the sheet has applied its settings.
class TaskbarSettingsNotifier : public QObject
{
    Q_OBJECT
public:
    static TaskbarSettingsNotifier *instance();
signals:
    void changed();
};

class TaskbarProperties : public QDialog
{
public:
    enum Tab { TaskbarTab, StartMenuTab };
    // The one sheet, on TAB.
    static void showTab(Tab tab);

private:
    TaskbarProperties();
    QWidget *taskbarPage();
    QWidget *startMenuPage();
    void updatePreviews();
    void apply();

    xpdialog::SheetTabs *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;
    QHash<int, QCheckBox *> m_checks;   // by control ID
    QAbstractButton *m_startRadio = nullptr, *m_classicRadio = nullptr;
    QLabel *m_taskbarPreview = nullptr, *m_notificationPreview = nullptr;
};
