// SPDX-License-Identifier: LGPL-2.1-or-later
#include "internetexplorer.h"

#include "browserbands.h"
#include "programs.h"
#include "theme.h"
#include "xpapp.h"
#include "xpfiles.h"
#include "xpprogram.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeDatabase>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineHistory>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#ifdef XP_PRINTING
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrinter>
#endif

#include <QtEndian>

using namespace bands;

namespace {

// browselc.dll: IE's menu bar (MENUEX 267), the Mail button's menu (262), the toolbars' (264), and words
const int MENU_IE = 267, MENU_MAIL = 262;
const int STRING_IE = 12613, STRING_ADDRESS = 12896, STRING_LINKS = 12385, STRING_GO = 12656, STRING_SEARCH = 12897;
const int STRING_BACK = 58689, STRING_FORWARD = 58690, STRING_FAVORITES = 58770;
// Their tooltips (browselc.dll)
const int TIP_BACK = 9281, TIP_FORWARD = 9282, TIP_HOME = 9285, TIP_STOP = 9530, TIP_REFRESH = 9536, TIP_MAIL = 9290, TIP_EDIT = 9062;
// shdoclc.dll: the status bar's words and the page's shortcut menus (MENU 24641, a popup per context)
const int STRING_DONE = 8169, STRING_OPENING = 8157, MENU_CONTEXT = 24641;
// xpsp2res.dll: SP2's Pop-up Blocker and Manage Add-ons in the Tools menu
const int STRING_POPUP_BLOCKER = 10207, STRING_POPUP_OFF = 10209, STRING_POPUP_SETTINGS = 10206, STRING_ADDONS = 10300;
// shell32.dll's strip of IE's 24-pixel pictures (204, hot 205)
const int IE_STRIP = 204, IE_STRIP_HOT = 205;
enum { PIC_BACK, PIC_FORWARD, PIC_STOP, PIC_REFRESH, PIC_HOME, PIC_SEARCH, PIC_FAVORITES, PIC_PRINT, PIC_FONTS, PIC_EDIT, PIC_DISCUSS,
       PIC_MEDIA, PIC_HISTORY, PIC_MAIL, PIC_FULLSCREEN };
// shell32.dll's 20-pixel Go arrow, and its hot picture
const int BITMAP_GO = 230, BITMAP_GO_HOT = 231;
// The commands of browselc's menu 267 (and shdocvw's) that this window carries out
enum Command {
    CMD_NEW_WINDOW = 275, CMD_NEW_MESSAGE = 279, CMD_OPEN = 256, CMD_EDIT = 41030, CMD_SAVE = 257, CMD_SAVE_AS = 258, CMD_PAGE_SETUP = 259,
    CMD_PRINT = 260, CMD_PRINT_PREVIEW = 277, CMD_SEND_PAGE = 282, CMD_SEND_LINK = 283, CMD_SEND_SHORTCUT = 284, CMD_PROPERTIES = 262,
    CMD_WORK_OFFLINE = 40998, CMD_CLOSE = 40993, CMD_CUT = 41025, CMD_COPY = 41026, CMD_PASTE = 41027, CMD_SELECT_ALL = 41028,
    CMD_FIND = 1091, CMD_STATUS_BAR = 41474, CMD_BAR_SEARCH = 41521, CMD_BAR_FAVORITES = 41522, CMD_BAR_MEDIA = 41527,
    CMD_BAR_HISTORY = 41523, CMD_BAR_FOLDERS = 41525, CMD_BACK = 41249, CMD_FORWARD = 41250, CMD_UP = 40994, CMD_HOME = 41253,
    CMD_STOP = 41498, CMD_REFRESH = 41504, CMD_TEXT_LARGEST = 3169, CMD_TEXT_SMALLEST = 3165, CMD_SOURCE = 3163, CMD_PRIVACY = 41507,
    CMD_FULL_SCREEN = 41499, CMD_JAVA = 41500, CMD_ADD_FAVORITE = 41331, CMD_ORGANIZE = 41330, CMD_FAVORITES_EMPTY = 41342,
    CMD_READ_MAIL = 41258, CMD_NEW_MAIL = 41261, CMD_MAIL_LINK = 41262, CMD_MAIL_PAGE = 41263, CMD_READ_NEWS = 41259,
    CMD_SYNCHRONIZE = 41334, CMD_WINDOWS_UPDATE = 342, CMD_TOOLS_EXTENSIONS = 36864, CMD_RESET_WEB = 41257, CMD_OPTIONS = 41251,
    CMD_HELP_CONTENTS = 337, CMD_TIP = 41219, CMD_NETSCAPE = 351, CMD_SUPPORT = 344, CMD_FEEDBACK = 345, CMD_HELP_EXTENSIONS = 36866,
    CMD_ABOUT = 336,
};
// The context menus' commands (shdoclc.dll's MENU 24641)
enum ContextCommand {
    CTX_COPY = 15, CTX_CUT = 16, CTX_PASTE = 26, CTX_PRINT = 27, CTX_PROPERTIES = 28, CTX_SELECT_ALL = 31, CTX_OPEN = 2136, CTX_OPEN_NEW = 2137,
    CTX_VIEW_SOURCE = 2139, CTX_ADD_FAVORITE = 2261, CTX_COPY_SHORTCUT = 2262, CTX_SET_BACKGROUND = 2264, CTX_SAVE_TARGET = 2268,
    CTX_SHOW_PICTURE = 2269, CTX_SAVE_PICTURE = 2270, CTX_BACK = 2282, CTX_FORWARD = 2283, CTX_REFRESH = 6042,
};
// Text Size: Largest to Smallest, as IE's zoom of the page's text (approximated by the page's zoom)
const qreal TEXT_SIZES[] = {0.8, 0.9, 1.0, 1.2, 1.4};
// The Windows Update site wupdmgr.exe and Tools > Windows Update open
const char WINDOWS_UPDATE[] = "http://windowsupdate.microsoft.com/";
// The Favorites IE 6 starts with on XP SP3 (ie.inf, measured on XP SP3): the Links folder's and the menu's
const struct { const char *folder, *name, *url; } DEFAULT_FAVORITES[] = {
    {"Links", "Customize Links", "http://www.microsoft.com/isapi/redir.dll?prd=ie&pver=6&ar=CLinks"},
    {"Links", "Free Hotmail", "http://www.microsoft.com/isapi/redir.dll?prd=ie&ar=hotmail"},
    {"Links", "Windows", "http://www.microsoft.com/isapi/redir.dll?prd=ie&ar=windows"},
    {"Links", "Windows Marketplace", "http://go.microsoft.com/fwlink/?LinkId=30857&clcid=0x409"},
    {"Links", "Windows Media", "http://www.microsoft.com/isapi/redir.dll?prd=ie&ar=windowsmedia"},
    {"", "MSN.com", "http://www.microsoft.com/isapi/redir.dll?prd=ie&ar=IEMSN"},
    {"", "Radio Station Guide", "http://www.microsoft.com/isapi/redir.dll?prd=ie&ar=radio"},
};

const std::optional<w4w::PeResources> &peOf(const char *name)
{
    static QHash<QString, std::optional<w4w::PeResources>> cache;
    const QString key = QLatin1String(name);
    if (!cache.contains(key))
        cache.insert(key, xpfiles::resources(xpfiles::find(key)));
    return cache[key];
}

QString word(const char *file, int id, const QString &fallback)
{
    const auto &pe = peOf(file);
    const QString text = pe ? xpfiles::string(*pe, id) : QString();
    return text.isEmpty() ? fallback : text;
}

QIcon stripIcon(int index)
{
    QIcon icon;
    const auto &shell32 = peOf("shell32.dll");
    if (!shell32)
        return icon;
    static const QImage normal = xpfiles::bitmap(*shell32, IE_STRIP), hot = xpfiles::bitmap(*shell32, IE_STRIP_HOT);
    const int side = normal.height();
    icon.addPixmap(QPixmap::fromImage(normal.copy(index * side, 0, side, side)), QIcon::Normal);
    icon.addPixmap(QPixmap::fromImage(hot.copy(index * side, 0, side, side)), QIcon::Active);
    return icon;
}

// iexplore.exe's own icon, and the page's (its document) at 16 pixels
QIcon ieIcon(int size)
{
    const auto &pe = peOf("iexplore.exe");
    if (!pe)
        return {};
    return QIcon(QPixmap::fromImage(xpfiles::icon(*pe, xpfiles::iconGroupAt(*pe, 0), size)));
}

QString favoritesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Favorites");
}

// IE's favorites are the folder windows' (.place files hold an address); the first time, XP's own.
void seedFavorites()
{
    const QString folder = favoritesFolder();
    if (QFileInfo::exists(folder))
        return;
    for (const auto &favorite : DEFAULT_FAVORITES) {
        const QString in = folder + (*favorite.folder ? QLatin1Char('/') + QLatin1String(favorite.folder) : QString());
        QDir().mkpath(in);
        QFile link(in + QLatin1Char('/') + QLatin1String(favorite.name) + QStringLiteral(".place"));
        if (link.open(QIODevice::WriteOnly))
            link.write(favorite.url);
    }
}

QString placeTarget(const QFileInfo &entry)
{
    if (entry.suffix() != QLatin1String("place"))
        return {};
    QFile link(entry.filePath());
    return link.open(QIODevice::ReadOnly) ? QString::fromUtf8(link.readAll()).trimmed() : QString();
}

// res://FILE/NAME: an HTML page, a picture or a dialog of the user's XP file, as IE's res: protocol
// (mshtml's) gives them. SP2's xpsp2res.dll holds newer copies of shdoclc.dll's pages, and IE takes those.
class ResHandler : public QWebEngineUrlSchemeHandler
{
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    static QByteArray find(const QString &file, const QString &name)
    {
        QStringList files{file.toLower()};
        if (files.first() == QLatin1String("shdoclc.dll"))
            files.prepend(QStringLiteral("xpsp2res.dll"));
        for (const QString &candidate : std::as_const(files)) {
            const auto &pe = peOf(candidate.toLatin1().constData());
            if (!pe)
                continue;
            for (const QString &type : {QStringLiteral("23"), QStringLiteral("2110"), QStringLiteral("HTML")}) {
                const QByteArray data = pe->find(type, name.toUpper());
                if (!data.isEmpty())
                    return data;
            }
        }
        return {};
    }
    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QUrl url = job->requestUrl();
        const QString name = url.path().mid(1).section(QLatin1Char('/'), -1);
        const QByteArray data = find(url.host(), name);
        if (data.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        QString type = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
        if (name.endsWith(QLatin1String(".dlg"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive))
            type = QStringLiteral("text/html");
        auto *buffer = new QBuffer(job);
        buffer->setData(data);
        job->reply(type.toLatin1(), buffer);
    }
};

// iexplore.exe's FileVersion (VS_FIXEDFILEINFO), which About shows as IE's version
QString ieVersion()
{
    const auto &pe = peOf("iexplore.exe");
    if (!pe)
        return {};
    const QByteArray info = pe->find(QStringLiteral("16"), QStringLiteral("1"));
    const int at = info.indexOf(QByteArray::fromHex("bd04effe"));
    if (at < 0 || at + 16 > info.size())
        return {};
    const quint32 high = qFromLittleEndian<quint32>(info.constData() + at + 8), low = qFromLittleEndian<quint32>(info.constData() + at + 12);
    return QStringLiteral("%1.%2.%3.%4").arg(high >> 16).arg(high & 0xffff).arg(low >> 16).arg(low & 0xffff);
}

// A MENUEX of the user's file as a menu bar, as browseui loads IE's: placeholders without words
// (where IE merges its extensions) are left for the caller, by their command.
QHash<int, QAction *> fillMenuBarEx(QMenuBar *bar, const w4w::PeResources &pe, int id, QHash<int, QMenu *> *placeholders)
{
    QHash<int, QAction *> actions;
    const QByteArray data = pe.find(QStringLiteral("4"), QString::number(id));
    if (data.size() < 4 || qFromLittleEndian<quint16>(data.constData()) != 1)
        return actions;
    int at = 4 + qFromLittleEndian<quint16>(data.constData() + 2);
    const auto u32 = [&] { const quint32 v = at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : 0; at += 4; return v; };
    const auto u16 = [&] { const quint16 v = at + 2 <= data.size() ? qFromLittleEndian<quint16>(data.constData() + at) : 0; at += 2; return v; };
    const auto text = [&] {
        QString s;
        while (at + 2 <= data.size()) {
            const quint16 c = u16();
            if (!c)
                break;
            s.append(QChar(c));
        }
        return s;
    };
    std::function<void(QMenu *)> items = [&](QMenu *into) {
        while (at < data.size()) {
            const quint32 type = u32(), state = u32(), command = u32();
            const quint16 flags = u16();
            const QString words = text();
            at = (at + 3) & ~3;
            if (flags & 0x01) {
                u32();
                QMenu *popup = into ? into->addMenu(words) : bar->addMenu(words);
                actions.insert(int(command), popup->menuAction());
                items(popup);
            } else if (into) {
                if (type & 0x800) {
                    into->addSeparator();
                } else if (words.isEmpty()) {
                    if (placeholders)
                        placeholders->insert(int(command), into);
                } else {
                    QAction *action = into->addAction(words);
                    action->setEnabled(!(state & 0x03));
                    action->setCheckable(state & 0x08);
                    action->setChecked(state & 0x08);
                    actions.insert(int(command), action);
                }
            }
            if (flags & 0x80)
                return;
        }
    };
    items(nullptr);
    return actions;
}

// A popup of a MENU by its name (shdoclc's context menus: Default, Image, Anchor...)
QHash<int, QAction *> fillContextMenu(QMenu *menu, const QString &popup)
{
    QHash<int, QAction *> actions;
    const auto &pe = peOf("shdoclc.dll");
    if (!pe)
        return actions;
    QMenuBar bar;
    const QHash<int, QAction *> read = xpprogram::fillMenuBar(&bar, *pe, MENU_CONTEXT);
    for (QAction *top : bar.actions()) {
        if (!top->menu() || top->text() != popup)
            continue;
        for (QAction *action : top->menu()->actions()) {
            if (action->isSeparator()) {
                if (!menu->actions().isEmpty() && !menu->actions().last()->isSeparator())
                    menu->addSeparator();
                continue;
            }
            if (action->text().isEmpty())
                continue;
            QAction *copy = menu->addAction(action->text());
            copy->setEnabled(false);
            actions.insert(read.key(action), copy);
        }
    }
    return actions;
}

QList<InternetExplorer *> &windows()
{
    static QList<InternetExplorer *> list;
    return list;
}

} // namespace

void InternetExplorer::registerSchemes()
{
    QWebEngineUrlScheme scheme("res");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

InternetExplorer *InternetExplorer::open(const QString &address)
{
    auto *window = new InternetExplorer(address.isEmpty() ? homePage() : address);
    window->show();
    return window;
}

QString InternetExplorer::homePage()
{
    const QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/iexplore.conf"), QSettings::IniFormat);
    return settings.value(QStringLiteral("Main/Start Page"), QStringLiteral("http://www.microsoft.com/isapi/redir.dll?prd=ie&pver=6&ar=msnhome")).toString();
}

InternetExplorer::InternetExplorer(const QString &address)
{
    setAttribute(Qt::WA_DeleteOnClose);
    windows().append(this);
    connect(this, &QObject::destroyed, [this] { windows().removeAll(this); });
    static bool handlerInstalled = false;
    if (!handlerInstalled) {
        QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("res", new ResHandler(qApp));
        handlerInstalled = true;
    }
    seedFavorites();
    setWindowIcon(ieIcon(32));
    // As browseui opens a window it has no saved place for: 800 x 600, frame included (as a folder's)
    {
        const QRect work = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1024, 738);
        const FrameStyle &frame = ShellTheme::instance()->frame();
        resize(qMin(ShellTheme::instance()->scaled(800), work.width()) - 2 * frame.border,
               qMin(ShellTheme::instance()->scaled(600), work.height()) - frame.top - frame.border);
    }

    // A link that asks for a new window gets a new IE window, as IE 6 does.
    class Page : public QWebEngineView
    {
    public:
        using QWebEngineView::QWebEngineView;

    protected:
        QWebEngineView *createWindow(QWebEnginePage::WebWindowType) override
        {
            auto *window = new InternetExplorer(QString());
            window->show();
            return window->view();
        }
    };
    m_view = new Page(this);
    m_view->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    setCentralWidget(m_view);

    buildMenus();
    buildToolbars();
    buildStatusBar();

    connect(m_view, &QWebEngineView::titleChanged, this, &InternetExplorer::updateTitle);
    connect(m_view, &QWebEngineView::iconChanged, this, [this](const QIcon &icon) {
        m_address->setItemIcon(0, icon.isNull() ? ieIcon(16) : icon);
    });
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        const QUrl shown = url.scheme() == QLatin1String("res") && m_failed.isValid() ? m_failed : url;
        if (url.scheme() != QLatin1String("res"))
            m_failed.clear();
        m_address->setItemText(0, shown.toString());
        m_backButton->setEnabled(m_view->history()->canGoBack());
        m_forwardButton->setEnabled(m_view->history()->canGoForward());
        updateTitle();
    });
    connect(m_view->page(), &QWebEnginePage::loadingChanged, this, [this](const QWebEngineLoadingInfo &info) {
        switch (info.status()) {
        case QWebEngineLoadingInfo::LoadStartedStatus:
            m_menuBar->setAnimating(true);
            m_stop->setEnabled(true);
            m_progress->setValue(0);
            m_progress->show();
            m_statusText->setText(word("shdoclc.dll", STRING_OPENING, tr("Opening page <0s>...")).replace(QLatin1String("<0s>"), info.url().toString()));
            break;
        case QWebEngineLoadingInfo::LoadFailedStatus:
            // IE's own page for a server it cannot reach, or for a page the server has not
            if (info.errorDomain() == QWebEngineLoadingInfo::HttpStatusCodeDomain) {
                const int code = info.errorCode();
                const QString page = (code == 400 || code == 403 || code == 404 || code == 410 || code == 500 || code == 501)
                                         ? QStringLiteral("http_%1.htm").arg(code) : QStringLiteral("http_gen.htm");
                showError(info.url(), page);
            } else if (info.errorDomain() != QWebEngineLoadingInfo::NoErrorDomain && info.errorCode() != -3 /* aborted */) {
                showError(info.url(), QStringLiteral("dnserror.htm"));
            }
            Q_FALLTHROUGH();
        default:
            m_menuBar->setAnimating(false);
            m_stop->setEnabled(false);
            m_progress->hide();
            m_statusText->setText(word("shdoclc.dll", STRING_DONE, tr("Done")));
            break;
        }
    });
    connect(m_view, &QWebEngineView::loadProgress, this, [this](int progress) { m_progress->setValue(progress); });
    connect(m_view->page(), &QWebEnginePage::linkHovered, this, [this](const QString &link) {
        m_statusText->setText(link.isEmpty() ? word("shdoclc.dll", STRING_DONE, tr("Done")) : link);
    });
    connect(m_view->page(), &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest request) {
        request.accept();
        request.toggleOn() ? showFullScreen() : showNormal();
    });
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
        QWebEngineContextMenuRequest *request = m_view->lastContextMenuRequest();
        if (!request)
            return;
        QMenu menu(this);
        const QUrl link = request->linkUrl(), media = request->mediaUrl();
        const bool image = request->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage;
        const QHash<int, QAction *> items = fillContextMenu(&menu, image ? QStringLiteral("Image") : link.isValid() ? QStringLiteral("Anchor") : QStringLiteral("Default"));
        const auto on = [&](int id, std::function<void()> run) {
            if (QAction *action = items.value(id)) {
                action->setEnabled(true);
                connect(action, &QAction::triggered, this, std::move(run));
            }
        };
        const QUrl target = link.isValid() ? link : media;
        on(CTX_OPEN, [this, target] { m_view->load(target); });
        on(CTX_OPEN_NEW, [target] { InternetExplorer::open(target.toString()); });
        on(CTX_SAVE_TARGET, [this] { m_view->triggerPageAction(QWebEnginePage::DownloadLinkToDisk); });
        on(CTX_SAVE_PICTURE, [this] { m_view->triggerPageAction(QWebEnginePage::DownloadImageToDisk); });
        on(CTX_SHOW_PICTURE, [this] { m_view->triggerPageAction(QWebEnginePage::Reload); });
        on(CTX_COPY_SHORTCUT, [target] { QApplication::clipboard()->setText(target.toString()); });
        on(CTX_COPY, [this, image] { m_view->triggerPageAction(image ? QWebEnginePage::CopyImageToClipboard : QWebEnginePage::Copy); });
        on(CTX_CUT, [this] { m_view->triggerPageAction(QWebEnginePage::Cut); });
        on(CTX_PASTE, [this] { m_view->triggerPageAction(QWebEnginePage::Paste); });
        on(CTX_SELECT_ALL, [this] { m_view->triggerPageAction(QWebEnginePage::SelectAll); });
        on(CTX_BACK, [this] { m_view->back(); });
        on(CTX_FORWARD, [this] { m_view->forward(); });
        on(CTX_REFRESH, [this] { m_view->reload(); });
        on(CTX_VIEW_SOURCE, [this] { viewSource(); });
        on(CTX_ADD_FAVORITE, [this] {
            if (QAction *add = findChild<QAction *>(QStringLiteral("addFavorite")))
                add->trigger();
        });
        if (QAction *back = items.value(CTX_BACK))
            back->setEnabled(m_view->history()->canGoBack());
        if (QAction *forward = items.value(CTX_FORWARD))
            forward->setEnabled(m_view->history()->canGoForward());
        if (!menu.isEmpty())
            menu.exec(m_view->mapToGlobal(at));
    });

    m_view->installEventFilter(this);
    if (!address.isEmpty())
        navigate(address);
    else
        updateTitle();
}

void InternetExplorer::navigate(const QString &address)
{
    QString text = address.trimmed();
    if (text.isEmpty())
        return;
    QUrl url = QUrl::fromUserInput(text, QDir::homePath(), QUrl::AssumeLocalFile);
    // A word alone is no address: IE searches for it (autosearch).
    if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char(':')) && !text.contains(QLatin1Char('/')))
        url = QUrl(QStringLiteral("https://www.bing.com/search?q=") + QString::fromUtf8(QUrl::toPercentEncoding(text)));
    m_failed.clear();
    m_view->load(url);
    m_address->setItemText(0, url.toString());
}

bool InternetExplorer::eventFilter(QObject *watched, QEvent *event)
{
    // Backspace goes back, as in IE, when nothing in the page takes it
    if (watched == m_view && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && m_stop->isEnabled()) {
            m_view->stop();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void InternetExplorer::showError(const QUrl &failed, const QString &page)
{
    const QByteArray html = ResHandler::find(QStringLiteral("shdoclc.dll"), page);
    if (html.isEmpty())
        return;
    m_failed = failed;
    m_view->setHtml(QString::fromLatin1(html), QUrl(QStringLiteral("res://shdoclc.dll/") + page));
}

void InternetExplorer::updateTitle()
{
    QString title = m_view->title();
    const QUrl url = m_view->url();
    if (title.isEmpty() || title == url.toString() || title == url.toString(QUrl::RemoveScheme).mid(2))
        title = m_failed.isValid() ? m_failed.toString() : url.isEmpty() ? QStringLiteral("about:blank") : url.toString();
    setWindowTitle(title + QStringLiteral(" - ") + word("browselc.dll", STRING_IE, QStringLiteral("Microsoft Internet Explorer")));
}

void InternetExplorer::viewSource()
{
    // IE writes the source in its cache and opens it in Notepad.
    const QString name = m_view->url().fileName().isEmpty() ? QStringLiteral("page") : QFileInfo(m_view->url().fileName()).completeBaseName();
    m_view->page()->toHtml([name](const QString &html) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/Temporary Internet Files");
        QDir().mkpath(folder);
        QFile file(folder + QLatin1Char('/') + name + QStringLiteral("[1].htm"));
        if (file.open(QIODevice::WriteOnly))
            file.write(html.toUtf8());
        file.close();
        programs::openNotepad(file.fileName());
    });
}

void InternetExplorer::sendMail(bool page)
{
    const QString subject = m_view->title(), link = m_view->url().toString();
    programs::open(QStringLiteral("mailto:?subject=%1&body=%2").arg(QString::fromUtf8(QUrl::toPercentEncoding(subject)),
                                                                 QString::fromUtf8(QUrl::toPercentEncoding(page ? subject + QStringLiteral("\n") + link : link))));
}

void InternetExplorer::about()
{
    // shdoclc's (SP2: xpsp2res's) ABOUT.DLG, an HTML dialog, with the arguments IE gives it: its
    // version, the cipher strength and the product ID, separated by ~.
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("About Internet Explorer"));
    auto *view = new QWebEngineView(dialog);
    QWebEngineScript arguments;
    arguments.setInjectionPoint(QWebEngineScript::DocumentCreation);
    arguments.setWorldId(QWebEngineScript::MainWorld);
    arguments.setSourceCode(QStringLiteral("window.dialogArguments = '%1~~~168~%2~~';").arg(ieVersion(), tr("Unknown")));
    view->page()->scripts().insert(arguments);
    connect(view->page(), &QWebEnginePage::windowCloseRequested, dialog, &QDialog::close);
    view->load(QUrl(QStringLiteral("res://shdoclc.dll/about.dlg")));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view);
    dialog->resize(413, 362);
    dialog->show();
}

void InternetExplorer::fillFavorites(QMenu *menu, const QString &folder)
{
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    ShellTheme *theme = ShellTheme::instance();
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            QMenu *sub = menu->addMenu(QIcon(QPixmap::fromImage(theme->shellIcon(4, 16))), entry.fileName());
            connect(sub, &QMenu::aboutToShow, this, [this, sub, path = entry.filePath()] {
                sub->clear();
                fillFavorites(sub, path);
                if (sub->isEmpty())
                    sub->addAction(tr("(Empty)"))->setEnabled(false);
            });
            sub->addAction(tr("(Empty)"))->setEnabled(false);
            continue;
        }
        const QString target = placeTarget(entry);
        if (target.isEmpty())
            continue;
        menu->addAction(ieIcon(16), entry.completeBaseName(), this, [this, target] { navigate(target); });
    }
}

void InternetExplorer::buildMenus()
{
    m_menuBar = new BrandMenuBar(this);
    setMenuBar(m_menuBar);
    const auto &browselc = peOf("browselc.dll");
    if (!browselc)
        return;
    QHash<int, QMenu *> placeholders;
    const QHash<int, QAction *> actions = fillMenuBarEx(m_menuBar, *browselc, MENU_IE, &placeholders);
    const auto on = [&](int id, std::function<void()> run, QKeySequence key = {}) {
        QAction *action = actions.value(id);
        if (!action)
            return static_cast<QAction *>(nullptr);
        action->setEnabled(true);
        connect(action, &QAction::triggered, this, std::move(run));
        // The words after the tab are the key's name; the key itself is the window's
        if (!key.isEmpty()) {
            action->setShortcut(key);
            action->setShortcutContext(Qt::WindowShortcut);
        }
        return action;
    };
    const auto hide = [&](int id) {
        if (QAction *action = actions.value(id))
            action->setVisible(false);
    };

    // File
    on(CMD_NEW_WINDOW, [] { InternetExplorer::open(); }, QKeySequence(Qt::CTRL | Qt::Key_N));
    on(CMD_NEW_MESSAGE, [] { programs::open(QStringLiteral("mailto:")); });
    for (int id : {387, 388, 389, 390, 391, 392, 393, 394, 395})
        hide(id);   // Outlook's (Post, Appointment...) and NetMeeting's: as XP shows File > New without them
    on(CMD_OPEN, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Open"), tr("Type the Internet address of a document or folder, and\nInternet Explorer will open it for you."),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok)
            navigate(text);
    }, QKeySequence(Qt::CTRL | Qt::Key_O));
    on(CMD_SAVE_AS, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Web Page"), QDir::homePath() + QLatin1Char('/') + m_view->title() + QStringLiteral(".htm"),
                                                          tr("Web Page, complete (*.htm;*.html);;Web Archive, single file (*.mht)"));
        if (!path.isEmpty())
            m_view->page()->save(path, path.endsWith(QLatin1String(".mht")) ? QWebEngineDownloadRequest::MimeHtmlSaveFormat : QWebEngineDownloadRequest::CompleteHtmlSaveFormat);
    });
#ifdef XP_PRINTING
    const auto print = [this] {
        auto *printer = new QPrinter(QPrinter::HighResolution);
        QPrintDialog dialog(printer, this);
        if (dialog.exec() != QDialog::Accepted) {
            delete printer;
            return;
        }
        connect(m_view, &QWebEngineView::printFinished, this, [printer] { delete printer; }, Qt::SingleShotConnection);
        m_view->print(printer);
    };
    if (QAction *printAction = on(CMD_PRINT, print, QKeySequence(Qt::CTRL | Qt::Key_P)))
        printAction->setObjectName(QStringLiteral("print"));
    on(CMD_PRINT_PREVIEW, print);
    on(CMD_PAGE_SETUP, [this] {
        QPrinter printer;
        QPageSetupDialog(&printer, this).exec();
    });
#endif
    // Edit opens the page in an HTML editor: XP has none until Office or FrontPage is installed
    if (QAction *edit = actions.value(CMD_EDIT))
        edit->setEnabled(false);
    on(CMD_SEND_PAGE, [this] { sendMail(true); });
    on(CMD_SEND_LINK, [this] { sendMail(false); });
    on(CMD_SEND_SHORTCUT, [this] {
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QFile link(desktop + QLatin1Char('/') + (m_view->title().isEmpty() ? m_view->url().host() : m_view->title()) + QStringLiteral(".place"));
        if (link.open(QIODevice::WriteOnly))
            link.write(m_view->url().toString().toUtf8());
    });
    if (QAction *offline = actions.value(CMD_WORK_OFFLINE))
        offline->setCheckable(true);
    on(CMD_CLOSE, [this] { close(); });

    // Edit
    on(CMD_CUT, [this] { m_view->triggerPageAction(QWebEnginePage::Cut); });
    on(CMD_COPY, [this] { m_view->triggerPageAction(QWebEnginePage::Copy); });
    on(CMD_PASTE, [this] { m_view->triggerPageAction(QWebEnginePage::Paste); });
    on(CMD_SELECT_ALL, [this] { m_view->triggerPageAction(QWebEnginePage::SelectAll); });
    on(CMD_FIND, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Find"), tr("Fi&nd what:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !text.isEmpty())
            m_view->findText(text);
    }, QKeySequence(Qt::CTRL | Qt::Key_F));

    // View
    if (QAction *toolbars = actions.value(41473); toolbars && toolbars->menu())
        m_toolbarsMenu = toolbars->menu();
    if (QAction *status = on(CMD_STATUS_BAR, [] {})) {
        status->setCheckable(true);
        status->setChecked(true);
        connect(status, &QAction::toggled, this, [this](bool shown) { statusBar()->setVisible(shown); });
    }
    on(CMD_BAR_SEARCH, [this] { showExplorerBar(0); }, QKeySequence(Qt::CTRL | Qt::Key_E));
    on(CMD_BAR_FAVORITES, [this] { showExplorerBar(1); }, QKeySequence(Qt::CTRL | Qt::Key_I));
    on(CMD_BAR_HISTORY, [this] { showExplorerBar(2); }, QKeySequence(Qt::CTRL | Qt::Key_H));
    on(CMD_BACK, [this] { m_view->back(); }, QKeySequence(Qt::ALT | Qt::Key_Left));
    on(CMD_FORWARD, [this] { m_view->forward(); }, QKeySequence(Qt::ALT | Qt::Key_Right));
    hide(CMD_UP);
    on(CMD_HOME, [this] { navigate(homePage()); }, QKeySequence(Qt::ALT | Qt::Key_Home));
    if (QAction *stop = on(CMD_STOP, [this] { m_view->stop(); }))
        stop->setShortcut(QKeySequence());
    on(CMD_REFRESH, [this] { m_view->reload(); }, QKeySequence(Qt::Key_F5));
    auto *sizes = new QActionGroup(this);
    for (int id = CMD_TEXT_SMALLEST; id <= CMD_TEXT_LARGEST; ++id) {
        const qreal zoom = TEXT_SIZES[id - CMD_TEXT_SMALLEST];
        if (QAction *size = on(id, [this, zoom] { m_view->setZoomFactor(zoom); })) {
            size->setCheckable(true);
            size->setChecked(zoom == 1.0);
            sizes->addAction(size);
        }
    }
    on(CMD_SOURCE, [this] { viewSource(); });
    on(CMD_FULL_SCREEN, [this] { isFullScreen() ? showNormal() : showFullScreen(); }, QKeySequence(Qt::Key_F11));
    hide(CMD_JAVA);   // no Java VM on XP SP3: IE hides it

    // Favorites: the user's, after Add and Organize
    if (QAction *favorites = actions.value(33136); favorites && favorites->menu()) {
        QMenu *menu = favorites->menu();
        QAction *add = actions.value(CMD_ADD_FAVORITE), *organize = actions.value(CMD_ORGANIZE);
        if (add) {
            add->setObjectName(QStringLiteral("addFavorite"));
            add->setEnabled(true);
            connect(add, &QAction::triggered, this, [this] {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Add Favorite"), tr("&Name:"), QLineEdit::Normal, m_view->title(), &ok);
                if (!ok || name.isEmpty())
                    return;
                QDir().mkpath(favoritesFolder());
                QFile link(favoritesFolder() + QLatin1Char('/') + QString(name).replace(QLatin1Char('/'), QLatin1Char('-')) + QStringLiteral(".place"));
                if (link.open(QIODevice::WriteOnly))
                    link.write(m_view->url().toString().toUtf8());
            });
        }
        if (organize) {
            organize->setEnabled(true);
            connect(organize, &QAction::triggered, this, [] {
                QDir().mkpath(favoritesFolder());
                programs::open(favoritesFolder());
            });
        }
        connect(menu, &QMenu::aboutToShow, this, [this, menu, add, organize] {
            for (QAction *action : menu->actions())
                if (action != add && action != organize)
                    menu->removeAction(action);
            menu->addSeparator();
            fillFavorites(menu, favoritesFolder());
        });
    }

    // Tools: XP SP2's Pop-up Blocker and Manage Add-ons after Mail and News; where IE merges its
    // extensions, Windows Messenger's and the network diagnosis's (words of no extracted file)
    if (QAction *mail = actions.value(41106); mail && mail->menu()) {
        QMenu *tools = actions.value(32960) ? actions.value(32960)->menu() : nullptr;
        on(CMD_READ_MAIL, [] { programs::open(QStringLiteral("mailto:")); });
        on(CMD_NEW_MAIL, [] { programs::open(QStringLiteral("mailto:")); });
        on(CMD_MAIL_LINK, [this] { sendMail(false); });
        on(CMD_MAIL_PAGE, [this] { sendMail(true); });
        if (tools) {
            QAction *after = tools->actions().value(tools->actions().indexOf(mail) + 1);
            auto *popups = new QMenu(word("xpsp2res.dll", STRING_POPUP_BLOCKER, tr("&Pop-up Blocker")), tools);
            QAction *turnOff = popups->addAction(word("xpsp2res.dll", STRING_POPUP_OFF, tr("Turn Off Pop-up &Blocker")));
            turnOff->setEnabled(false);
            popups->addAction(word("xpsp2res.dll", STRING_POPUP_SETTINGS, tr("&Pop-up Blocker Settings...")))->setEnabled(false);
            tools->insertMenu(after, popups);
            tools->insertAction(after, new QAction(word("xpsp2res.dll", STRING_ADDONS, tr("Manage &Add-ons...")), tools));
            if (QMenu *extensions = placeholders.value(CMD_TOOLS_EXTENSIONS)) {
                QAction *before = actions.value(CMD_RESET_WEB);
                extensions->insertSeparator(before);
                extensions->insertAction(before, new QAction(tr("Windows Messenger"), extensions));
                extensions->insertAction(before, new QAction(tr("Diagnose Connection Problems..."), extensions));
                extensions->insertSeparator(before);
            }
        }
    }
    hide(CMD_RESET_WEB);   // only for a browser IE is not the default of; XP's Tools has none
    on(CMD_WINDOWS_UPDATE, [this] { navigate(QLatin1String(WINDOWS_UPDATE)); });
    on(CMD_OPTIONS, [] {});

    // Help
    if (QMenu *extensions = placeholders.value(CMD_HELP_EXTENSIONS))
        extensions->insertSeparator(actions.value(CMD_ABOUT));
    on(CMD_ABOUT, [this] { about(); });
    on(CMD_HELP_CONTENTS, [] { QProcess::startDetached(xpapp::sibling(QStringLiteral("helpctr")), {}); });
}

void InternetExplorer::buildToolbars()
{
    // As a folder window's: 38 pixels tall, the first button 2 from the band's left
    const int BUTTON_HEIGHT = 38;
    auto *toolbar = new RebarToolBar(tr("Standard Buttons"), this, true);
    addToolBar(toolbar);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    auto *lead = new QWidget(toolbar);
    lead->setFixedSize(2, BUTTON_HEIGHT);
    toolbar->addWidget(lead);

    const auto historyButton = [&](int picture, const QString &text, const QString &tip, bool back) {
        auto *button = new BandButton(toolbar, BUTTON_HEIGHT, 24, BandButton::Split);
        button->setIcon(stripIcon(picture));
        button->setText(text);
        button->setToolTip(tip);
        button->setToolButtonStyle(back ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        button->setEnabled(false);
        auto *menu = new QMenu(button);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, back] {
            menu->clear();
            QWebEngineHistory *history = m_view->history();
            const QList<QWebEngineHistoryItem> items = back ? history->backItems(9) : history->forwardItems(9);
            for (int i = 0; i < items.size(); ++i) {
                const QWebEngineHistoryItem item = back ? items[items.size() - 1 - i] : items[i];
                menu->addAction(item.title().isEmpty() ? item.url().toString() : item.title(), this, [history, item] { history->goToItem(item); });
            }
        });
        button->setMenu(menu);
        connect(button, &QToolButton::clicked, this, [this, back] { back ? m_view->back() : m_view->forward(); });
        toolbar->addWidget(button);
        return button;
    };
    m_backButton = historyButton(PIC_BACK, word("browselc.dll", STRING_BACK, tr("Back")), word("browselc.dll", TIP_BACK, tr("Back")), true);
    m_forwardButton = historyButton(PIC_FORWARD, word("browselc.dll", STRING_FORWARD, tr("Forward")), word("browselc.dll", TIP_FORWARD, tr("Forward")), false);
    const auto button = [&](int picture, const QString &text, const QString &tip, bool words, std::function<void()> run) {
        auto *action = new QAction(stripIcon(picture), text, this);
        action->setToolTip(tip.isEmpty() ? text : tip);
        connect(action, &QAction::triggered, this, std::move(run));
        auto *band = new BandButton(toolbar, BUTTON_HEIGHT, 24);
        band->setToolButtonStyle(words ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        band->setDefaultAction(action);
        toolbar->addWidget(band);
        return action;
    };
    // XP's IE: selective text on the right, as its folder windows; tooltips are browselc's descriptions
    m_stop = button(PIC_STOP, tr("Stop"), word("browselc.dll", TIP_STOP, QString()), false, [this] { m_view->stop(); });
    m_stop->setEnabled(false);
    button(PIC_REFRESH, tr("Refresh"), word("browselc.dll", TIP_REFRESH, QString()), false, [this] { m_view->reload(); });
    button(PIC_HOME, tr("Home"), word("browselc.dll", TIP_HOME, QString()), false, [this] { navigate(homePage()); });
    toolbar->addWidget(new BandSeparator(toolbar, BUTTON_HEIGHT));
    button(PIC_SEARCH, word("browselc.dll", STRING_SEARCH, tr("Search")), QString(), true, [this] { showExplorerBar(0); });
    button(PIC_FAVORITES, word("browselc.dll", STRING_FAVORITES, tr("Favorites")), QString(), true, [this] { showExplorerBar(1); });
    button(PIC_HISTORY, tr("History"), QString(), false, [this] { showExplorerBar(2); });
    toolbar->addWidget(new BandSeparator(toolbar, BUTTON_HEIGHT));
    // Mail: its whole button opens browselc's menu 262
    auto *mail = new BandButton(toolbar, BUTTON_HEIGHT, 24, BandButton::WholeDropDown);
    mail->setIcon(stripIcon(PIC_MAIL));
    mail->setToolTip(word("browselc.dll", TIP_MAIL, tr("Mail")));
    auto *mailMenu = new QMenu(mail);
    if (const auto &browselc = peOf("browselc.dll")) {
        const QHash<int, QAction *> items = xpprogram::fillMenu(mailMenu, *browselc, MENU_MAIL);
        for (auto it = items.cbegin(); it != items.cend(); ++it) {
            const int id = it.key();
            connect(it.value(), &QAction::triggered, this, [this, id] {
                if (id == CMD_MAIL_LINK || id == CMD_MAIL_PAGE)
                    sendMail(id == CMD_MAIL_PAGE);
                else
                    programs::open(QStringLiteral("mailto:"));
            });
        }
    }
    mail->setMenu(mailMenu);
    toolbar->addWidget(mail);
#ifdef XP_PRINTING
    button(PIC_PRINT, tr("Print"), QString(), false, [this] {
        if (QAction *print = findChild<QAction *>(QStringLiteral("print")))
            print->trigger();
        else
            m_view->page()->printToPdf(QDir::homePath() + QStringLiteral("/page.pdf"));
    });
#else
    button(PIC_PRINT, tr("Print"), QString(), false, [] {})->setEnabled(false);
#endif
    button(PIC_EDIT, tr("Edit"), word("browselc.dll", TIP_EDIT, QString()), false, [this] { viewSource(); });
    // Windows Messenger's button, which its setup adds to IE's toolbar
    if (const auto &messenger = peOf("msmsgs.exe")) {
        auto *action = new QAction(QIcon(QPixmap::fromImage(xpfiles::icon(*messenger, xpfiles::iconGroupAt(*messenger, 0), 24))), tr("Windows Messenger"), this);
        auto *band = new BandButton(toolbar, BUTTON_HEIGHT, 24);
        band->setToolButtonStyle(Qt::ToolButtonIconOnly);
        band->setDefaultAction(action);
        toolbar->addWidget(band);
    }

    // The Address band, with Go, and the Links band beside it
    addToolBarBreak();
    auto *addressBar = new RebarToolBar(tr("Address Bar"), this, false);
    addToolBar(addressBar);
    addressBar->setMovable(false);
    const int ROW_HEIGHT = 22;
    auto *row = new QWidget(addressBar);
    row->setFixedHeight(ROW_HEIGHT);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 0, 2, 0);
    rowLayout->setSpacing(4);
    QPalette bandText = row->palette();
    {
        ShellTheme *theme = ShellTheme::instance();
        const w4w::Renderer *renderer = theme->renderer();
        bandText.setColor(QPalette::WindowText, renderer ? renderer->colour(w4w::Part{QStringLiteral("rebar"), {}, {}, 0, {}}, QStringLiteral("textcolor")).value_or(QColor(127, 124, 115))
                                                         : theme->system(QStringLiteral("btntext"), Qt::black));
    }
    auto *addressLabel = new QLabel(word("browselc.dll", STRING_ADDRESS, tr("Address")), row);
    addressLabel->setPalette(bandText);
    rowLayout->addWidget(addressLabel);
    m_address = new QComboBox(row);
    m_address->setEditable(true);
    m_address->setInsertPolicy(QComboBox::NoInsert);
    m_address->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_address->setMinimumContentsLength(10);
    m_address->setFixedHeight(ROW_HEIGHT);
    m_address->addItem(ieIcon(16), QString());
    rowLayout->addWidget(m_address, 1);
    rowLayout->addSpacing(2 - rowLayout->spacing());
    auto *goButton = new BandButton(row, ROW_HEIGHT, 20);
    goButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QIcon goIcon;
    if (const auto &shell32 = peOf("shell32.dll")) {
        goIcon.addPixmap(QPixmap::fromImage(xpfiles::bitmap(*shell32, BITMAP_GO)), QIcon::Normal);
        goIcon.addPixmap(QPixmap::fromImage(xpfiles::bitmap(*shell32, BITMAP_GO_HOT)), QIcon::Active);
    }
    auto *go = new QAction(goIcon, word("browselc.dll", STRING_GO, QStringLiteral("|Go||")).section(QLatin1Char('|'), 1, 1), this);
    go->setToolTip(word("browselc.dll", 12657, tr("Go to \"%s\"")).replace(QLatin1String("%s"), m_address->currentText()));
    goButton->setDefaultAction(go);
    rowLayout->addWidget(goButton);
    connect(go, &QAction::triggered, this, [this] { navigate(m_address->currentText()); });
    connect(m_address->lineEdit(), &QLineEdit::returnPressed, go, &QAction::trigger);
    // Links: the band's name, and its chevron with the Links folder's favorites
    rowLayout->addSpacing(6);
    auto *gripper = new BandSeparator(row, ROW_HEIGHT);
    rowLayout->addWidget(gripper);
    auto *linksLabel = new QLabel(word("browselc.dll", STRING_LINKS, tr("Links")), row);
    linksLabel->setPalette(bandText);
    rowLayout->addWidget(linksLabel);
    auto *chevron = new QToolButton(row);
    chevron->setAutoRaise(true);
    chevron->setText(QStringLiteral("»"));
    chevron->setFixedSize(16, ROW_HEIGHT);
    chevron->setPopupMode(QToolButton::InstantPopup);
    chevron->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: bold; } QToolButton::menu-indicator { image: none; }"));
    auto *links = new QMenu(chevron);
    connect(links, &QMenu::aboutToShow, this, [this, links] {
        links->clear();
        fillFavorites(links, favoritesFolder() + QStringLiteral("/Links"));
        if (links->isEmpty())
            links->addAction(tr("(Empty)"))->setEnabled(false);
    });
    chevron->setMenu(links);
    rowLayout->addWidget(chevron);
    addressBar->addWidget(row);

    if (m_toolbarsMenu) {
        for (QAction *action : m_toolbarsMenu->actions())
            m_toolbarsMenu->removeAction(action);
        toolbar->toggleViewAction()->setText(tr("&Standard Buttons"));
        addressBar->toggleViewAction()->setText(tr("&Address Bar"));
        m_toolbarsMenu->addAction(toolbar->toggleViewAction());
        m_toolbarsMenu->addAction(addressBar->toggleViewAction());
        QAction *linksShown = m_toolbarsMenu->addAction(tr("&Links"));
        linksShown->setCheckable(true);
        linksShown->setChecked(true);
        connect(linksShown, &QAction::toggled, this, [gripper, linksLabel, chevron](bool shown) {
            gripper->setVisible(shown);
            linksLabel->setVisible(shown);
            chevron->setVisible(shown);
        });
    }
}

void InternetExplorer::buildStatusBar()
{
    // IE's: the page's picture and words, its progress, four small panes and the zone (measured on XP
    // SP3: 24 pixels each, the zone's 150 with its grip)
    QStatusBar *status = statusBar();
    auto *left = new QWidget(status);
    auto *leftLayout = new QHBoxLayout(left);
    leftLayout->setContentsMargins(2, 0, 0, 0);
    leftLayout->setSpacing(4);
    m_statusIcon = new QLabel(left);
    m_statusIcon->setPixmap(ieIcon(16).pixmap(16, 16));
    leftLayout->addWidget(m_statusIcon);
    m_statusText = new QLabel(word("shdoclc.dll", STRING_DONE, tr("Done")), left);
    leftLayout->addWidget(m_statusText, 1);
    status->addWidget(left, 1);
    m_progress = new QProgressBar(status);
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->setFixedSize(110, 12);
    m_progress->hide();
    status->addPermanentWidget(m_progress);
    for (int i = 0; i < 4; ++i) {
        auto *pane = new QLabel(status);
        pane->setFixedWidth(22);
        status->addPermanentWidget(pane);
    }
    m_zone = new QLabel(status);
    m_zone->setFixedWidth(140);
    m_zone->setTextFormat(Qt::RichText);
    QIcon globe;
    if (const auto &inetcpl = peOf("inetcpl.cpl"))
        globe = QIcon(QPixmap::fromImage(xpfiles::icon(*inetcpl, xpfiles::iconGroupAt(*inetcpl, 0), 16)));
    auto *zone = new QWidget(status);
    auto *zoneLayout = new QHBoxLayout(zone);
    zoneLayout->setContentsMargins(2, 0, 0, 0);
    zoneLayout->setSpacing(4);
    auto *zoneIcon = new QLabel(zone);
    zoneIcon->setPixmap(globe.pixmap(16, 16));
    zoneLayout->addWidget(zoneIcon);
    auto *zoneText = new QLabel(tr("Internet"), zone);
    zoneLayout->addWidget(zoneText, 1);
    zone->setFixedWidth(140);
    delete m_zone;
    m_zone = zoneText;
    status->addPermanentWidget(zone);
}

void InternetExplorer::showExplorerBar(int which)
{
    // The Explorer Bar on the window's left: Search, Favorites or History, its name and a close button
    if (m_bar && m_barShown == which && m_bar->isVisible()) {
        m_bar->hide();
        return;
    }
    if (!m_bar) {
        m_bar = new QDockWidget(this);
        m_bar->setFeatures(QDockWidget::DockWidgetClosable);
        m_bar->setAllowedAreas(Qt::LeftDockWidgetArea);
        auto *body = new QWidget(m_bar);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(0, 0, 0, 0);
        m_barList = new QListWidget(body);
        m_barList->setFrameShape(QFrame::NoFrame);
        layout->addWidget(m_barList);
        m_bar->setWidget(body);
        addDockWidget(Qt::LeftDockWidgetArea, m_bar);
        resizeDocks({m_bar}, {220}, Qt::Horizontal);
        connect(m_barList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
            const QString target = item->data(Qt::UserRole).toString();
            if (!target.isEmpty())
                navigate(target);
        });
        connect(m_barList, &QListWidget::itemClicked, m_barList, &QListWidget::itemActivated);
    }
    m_barShown = which;
    m_barList->clear();
    if (which == 0) {
        m_bar->setWindowTitle(word("browselc.dll", STRING_SEARCH, tr("Search")));
        // IE 6's Search Companion asks what to look for; here, the search engine's page in the bar's place
        auto *item = new QListWidgetItem(tr("Search the Web"), m_barList);
        item->setData(Qt::UserRole, QStringLiteral("https://www.bing.com/"));
    } else if (which == 1) {
        m_bar->setWindowTitle(word("browselc.dll", STRING_FAVORITES, tr("Favorites")));
        QDirIterator it(favoritesFolder(), {QStringLiteral("*.place")}, QDir::Files, QDirIterator::Subdirectories);
        QList<QFileInfo> found;
        while (it.hasNext())
            found.append(QFileInfo(it.next()));
        std::sort(found.begin(), found.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.filePath().compare(b.filePath(), Qt::CaseInsensitive) < 0; });
        for (const QFileInfo &entry : std::as_const(found)) {
            auto *item = new QListWidgetItem(ieIcon(16), entry.completeBaseName(), m_barList);
            item->setData(Qt::UserRole, placeTarget(entry));
        }
    } else {
        m_bar->setWindowTitle(tr("History"));
        const QList<QWebEngineHistoryItem> items = m_view->history()->items();
        for (int i = int(items.size()) - 1; i >= 0; --i) {
            auto *item = new QListWidgetItem(ieIcon(16), items[i].title().isEmpty() ? items[i].url().toString() : items[i].title(), m_barList);
            item->setData(Qt::UserRole, items[i].url().toString());
        }
    }
    m_bar->show();
}
