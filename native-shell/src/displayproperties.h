// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Display Properties: the Themes and Appearance pages from themeui.dll's
// dialogs, the Desktop page in the same place, with previews of the choice.
// Only Apply changes the session (native-look.conf, then ShellTheme::changed).
#pragma once

#include "theme.h"

#include <QDialog>
#include <QImage>

class QComboBox;
class QListWidget;
class QPushButton;
namespace xpdialog { class SheetTabs; }
class QSlider;
class QSpinBox;
class QLabel;

class ThemePreview : public QWidget
{
    Q_OBJECT
public:
    explicit ThemePreview(QWidget *parent);
    // The Themes tab shows one window and the desktop, Appearance a window of each kind.
    enum Mode { ThemeSample, AppearanceSample };
    void setMode(Mode mode) { m_mode = mode; }
    void setChoice(const LookChoice &choice);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Mode m_mode = ThemeSample;
    LookChoice m_choice;
    FrameStyle m_style;
    QImage m_wallpaper;
    QString m_wallpaperPath;
};

class MonitorPreview : public QWidget
{
    Q_OBJECT
public:
    explicit MonitorPreview(QWidget *parent);
    void setChoice(const LookChoice &choice);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_monitor, m_wallpaper;
    QString m_wallpaperPath;
    LookChoice m_choice;
};

class DisplayProperties : public QDialog
{
    Q_OBJECT
public:
    enum Tab { Themes, Desktop, ScreenSaver, Appearance, Settings, LinuxPrograms };
    // The one window of them, on TAB.
    static void showTab(Tab tab);
    // The screen saver chosen here, when the session starts.
    static void restoreScreenSaver();

private:
    DisplayProperties();
    QWidget *themesPage();
    QWidget *desktopPage();
    QWidget *appearancePage();
    QWidget *screenSaverPage();
    QWidget *settingsPage();
    void applyScreenSaver();
    void fillThemes();
    void importTheme();
    void deleteTheme();
    void applyResolution();
    QWidget *linuxPage();
    void showIconSamples();
    void applyLinuxLook();
    void fillAppearance();
    void selectWallpaper();
    void changed();
    void apply();

    LookChoice m_choice;
    bool m_filling = false;
    xpdialog::SheetTabs *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;
    QComboBox *m_theme = nullptr, *m_style = nullptr, *m_colour = nullptr, *m_size = nullptr, *m_position = nullptr;
    QListWidget *m_wallpapers = nullptr;
    QPushButton *m_backgroundButton = nullptr;
    QComboBox *m_linuxStyle = nullptr, *m_iconTheme = nullptr;
    QWidget *m_iconSamples = nullptr;
    bool m_rebuild = false;
    ThemePreview *m_themePreview = nullptr, *m_appearancePreview = nullptr;
    MonitorPreview *m_monitor = nullptr, *m_saverMonitor = nullptr, *m_settingsMonitor = nullptr;
    QComboBox *m_saver = nullptr;
    QSpinBox *m_saverWait = nullptr;
    QSlider *m_resolution = nullptr;
    QPushButton *m_deleteTheme = nullptr;
    QLabel *m_resolutionText = nullptr;
    QList<QSize> m_modes;
    QSize m_mode;
};
