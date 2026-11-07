// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Internet Explorer 6 as a program of its own (iexplore): browseui's frame, as XP's folder
// windows have it, with IE's menus (browselc.dll's menu 267, SP2's additions from xpsp2res.dll), its
// toolbar (shell32.dll's strips 204 and 205), the Address and Links bands and its status bar. Qt
// WebEngine shows the pages; IE's own pages (res://shdoclc.dll/dnserror.htm...) come from the user's
// shdoclc.dll and xpsp2res.dll.
#pragma once

#include <QMainWindow>
#include <QUrl>

class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QToolButton;
class QWebEngineView;
namespace bands { class BrandMenuBar; }

class InternetExplorer : public QMainWindow
{
    Q_OBJECT
public:
    // res://, before the QApplication exists
    static void registerSchemes();
    // A new window on ADDRESS, or on the home page
    static InternetExplorer *open(const QString &address = QString());
    // XP's IE 6 home page (ie.inf's START_PAGE), or the one Internet Options chose
    static QString homePage();

    explicit InternetExplorer(const QString &address);
    void navigate(const QString &address);
    QWebEngineView *view() const { return m_view; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildMenus();
    void buildToolbars();
    void buildStatusBar();
    void showExplorerBar(int which);
    void fillFavorites(QMenu *menu, const QString &folder);
    void showError(const QUrl &failed, const QString &page);
    void updateTitle();
    void viewSource();
    void sendMail(bool page);
    void about();

    QWebEngineView *m_view = nullptr;
    bands::BrandMenuBar *m_menuBar = nullptr;
    QComboBox *m_address = nullptr;
    QToolButton *m_backButton = nullptr, *m_forwardButton = nullptr;
    QAction *m_stop = nullptr;
    QLabel *m_statusIcon = nullptr, *m_statusText = nullptr, *m_zone = nullptr;
    QProgressBar *m_progress = nullptr;
    QMenu *m_toolbarsMenu = nullptr;
    QDockWidget *m_bar = nullptr;
    QListWidget *m_barList = nullptr;
    int m_barShown = -1;
    // The address a failed page was for: its error page shows it in the Address bar
    QUrl m_failed;
};
