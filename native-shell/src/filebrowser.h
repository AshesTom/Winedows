// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's folder window (the native port of its Explorer): the menus, the toolbar,
// the address bar, the Common Tasks pane or the Folders tree, and the folder in
// Thumbnails, Tiles, Icons, List or Details. It shows real folders and XP's
// virtual ones: My Computer, the Recycle Bin, Control Panel, search results.
#pragma once

#include <QAbstractItemView>
#include <QFileInfo>
#include <QMainWindow>
#include <QModelIndex>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>

class QAction;
class QActionGroup;
class QComboBox;
class QFileSystemModel;
class QLineEdit;
class QListView;
class QMenu;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QStandardItemModel;
class QStandardItem;
class QTimer;
class QToolButton;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
class QWebEngineView;
class SearchCompanion;
struct SearchQuery;
class FolderModel;
class TaskPane;
class ControlPanelCategories;
class SystemFolderWarning;

// Addresses of the virtual folders
namespace places {
extern const QString Computer, RecycleBin, ControlPanel, Search;
bool isWeb(const QString &location);
QString title(const QString &location);
QIcon icon(const QString &location);
}

class FileBrowser : public QMainWindow
{
    Q_OBJECT
public:
    // A window on LOCATION, a path or a virtual folder; the one already there if there is.
    static void open(const QString &location);
    // Folder Options changed: every window reads them again.
    static void settingsChanged();

    FileBrowser(const QString &location);
    void navigate(const QString &location, bool remember = true);
    QString location() const { return m_location; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum View { Thumbnails, Tiles, Icons, List, Details };

    void buildMenus();
    void buildToolbars();
    void buildPanes();
    void setView(View view);
    void setFoldersShown(bool shown);
    void showControlPanelCategory(int category);
    void updatePane();
    void updateActions();
    void updateStatus();
    void showVirtual(const QString &location);
    void fillComputer();
    void fillControlPanel();
    void addVirtual(const QString &text, const QIcon &icon, std::function<void()> action, const QString &detail = QString());
    void addVirtualHeader(const QString &text);
    void activate(const QModelIndex &index);
    void openPath(const QString &path);
    void showContextMenu(QPoint global, bool onItem);
    void addHistoryMenu(QToolButton *button, bool back);
    QAbstractItemView *currentView() const;
    QStringList selectedPaths() const;
    QString currentFolder() const;     // empty for a virtual folder
    void newFolder();
    void newTextDocument();
    void renameSelected();
    void deleteSelected(bool permanent);
    void showProperties();
    void moveOrCopyTo(bool move);
    void startSearch(const SearchQuery &query);
    void continueSearch();
    void finishSearch();
    void showSearchBar(bool shown);
    void showWebPage(const QString &address);
    void searchTheWeb(const QString &text, const QString &engine);
    void fillTreeItem(QTreeWidgetItem *item);
    void selectInTree(const QString &location);
    QMenu *buildFavoritesMenu();
    void reloadSettings();
    void placeEdgeLines();
    bool blocked() const;
    void setSystemFolderShown(bool shown);
    void renamePending();

    QString m_location;
    QStringList m_back, m_forward;
    View m_view = Tiles;
    bool m_foldersShown = false, m_searchShown = false, m_noTasks = false, m_singleClick = false;
    QString m_pendingRename, m_searchFrom;
    QList<std::function<void()>> m_actions;   // of the virtual list's items

    QFileSystemModel *m_files = nullptr;
    FolderModel *m_model = nullptr;
    QStandardItemModel *m_virtual = nullptr;
    QStackedWidget *m_pages = nullptr, *m_left = nullptr;
    QListView *m_list = nullptr, *m_virtualList = nullptr;
    QTreeView *m_details = nullptr, *m_results = nullptr;
    QTreeWidget *m_tree = nullptr;
    QStandardItemModel *m_resultsModel = nullptr;
    SearchCompanion *m_searchCompanion = nullptr;
    QWebEngineView *m_web = nullptr;
    QMenu *m_toolbarsMenu = nullptr;
    QWidget *m_categoryPage = nullptr;
    QScrollArea *m_categoryArea = nullptr;
    SystemFolderWarning *m_warning = nullptr;
    QWidget *m_bottomLine = nullptr, *m_leftLine = nullptr;
    QSplitter *m_splitter = nullptr;
    TaskPane *m_pane = nullptr;
    QScrollArea *m_paneArea = nullptr;
    QComboBox *m_address = nullptr;
    QToolButton *m_backButton = nullptr, *m_forwardButton = nullptr;
    QAction *m_upAction = nullptr, *m_foldersAction = nullptr, *m_searchAction = nullptr, *m_deleteAction = nullptr, *m_moveAction = nullptr, *m_copyToAction = nullptr;
    QAction *m_cutAction = nullptr, *m_copyAction = nullptr, *m_pasteAction = nullptr, *m_renameAction = nullptr, *m_propertiesAction = nullptr, *m_newFolderAction = nullptr;
    QAction *m_statusAction = nullptr, *m_newTextAction = nullptr;
    QActionGroup *m_viewGroup = nullptr;
    QMenu *m_viewsMenu = nullptr;

    // A running search: the folders left to read
    QStringList m_searchPending;
    std::shared_ptr<SearchQuery> m_query;
    int m_searchFound = 0;
    QTimer *m_searchTimer = nullptr;
};
