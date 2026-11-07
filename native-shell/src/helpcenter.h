// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Help and Support Center (helpctr) as a program of its own. XP's is HTML hosted by helpctr.exe
// with its own object model; here the frame is native and the pages are made from the user's files:
// the toolbar from HCpan_06.xml and helpctr.exe's pictures, the home page's words from HCsys_02.htm,
// the topics' titles and descriptions from the help database (HCdat_w3.edb) and its pictures, all of
// the media's pchdt_w3.cab. Qt WebEngine shows the pages.
#pragma once

#include <QMainWindow>

class QLineEdit;
class QToolButton;
class QWebEngineView;

class HelpCenter : public QMainWindow
{
    Q_OBJECT
public:
    // hcp://, before the QApplication exists
    static void registerSchemes();
    // TOPIC: an hcp:// address, or the home page without one
    explicit HelpCenter(const QString &topic = QString());
    void navigate(const QString &address);

private:
    void buildToolbar();
    QWidget *buildSearchBand();
    // What a page's link does here: an action of the frame, or another page
    bool follow(const QUrl &url);

    QWebEngineView *m_view = nullptr;
    QToolButton *m_backButton = nullptr, *m_forwardButton = nullptr;
    QLineEdit *m_search = nullptr;
};
