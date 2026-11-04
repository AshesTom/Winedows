// SPDX-License-Identifier: LGPL-2.1-or-later
// Taskbar and Start Menu Properties: explorer.exe's sheet (string 810) with its Taskbar page (dialog 6)
// and its Start Menu page (dialog 205), their pictures as explorer.exe picks them, and the settings
// this taskbar has: grouping similar buttons and showing the clock. What it does not have (auto-hide,
// Quick Launch, hiding inactive icons, the classic Start menu, unlocking) is greyed, in the state it is in.
#pragma once

#include <QDialog>
#include <QHash>

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
    bool clock = true;
    bool autoHide = false;   // StuckRects2's ABS_AUTOHIDE
    static TaskbarSettings load();
    void save() const;
};

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
    QLabel *m_taskbarPreview = nullptr, *m_notificationPreview = nullptr;
};
