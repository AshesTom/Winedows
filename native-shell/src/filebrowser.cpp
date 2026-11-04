// SPDX-License-Identifier: LGPL-2.1-or-later
#include "filebrowser.h"

#include "applets.h"
#include "controlpanel.h"
#include "fileicons.h"
#include "fileops.h"
#include "pictures.h"
#include "programs.h"
#include "searchcompanion.h"
#include "sounds.h"
#include "taskpane.h"
#include "theme.h"
#include "xpfiles.h"
#include "xpprogram.h"
#include "shellmenus.h"
#include "fileproperties.h"

#include <array>
#include <cmath>

#include <QRegularExpression>
#include <QAbstractProxyModel>
#include <QTreeWidget>
#ifdef XP_WEB_PAGES
#include <QWebEnginePage>
#include <QWebEngineView>
#endif
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include "xpdialog.h"
#include <QDialogButtonBox>
#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIdentityProxyModel>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStyleOptionMenuItem>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace places {
const QString Computer = QStringLiteral("::computer"), RecycleBin = QStringLiteral("::recycle"),
              ControlPanel = QStringLiteral("::controlpanel"), Search = QStringLiteral("::search");

static QString shellText(int id, const char *fallback)
{
    return xpfiles::shellString(id, fallback);
}

bool isWeb(const QString &location)
{
    return location.startsWith(QLatin1String("http://")) || location.startsWith(QLatin1String("https://"));
}

QString title(const QString &location)
{
    if (location == Computer)
        return shellText(9216, "My Computer");
    if (location == RecycleBin)
        return shellText(8964, "Recycle Bin");
    if (location == ControlPanel)
        return shellText(4161, "Control Panel");
    if (location.startsWith(ControlPanel + QLatin1Char('/')))
        return controlpanel::categories().value(location.section(QLatin1Char('/'), 1).toInt()).name;
    if (location == Search)
        return QObject::tr("Search Results");
    const QFileInfo info(location);
    if (info.isRoot())
        return info.absoluteFilePath();
    if (info.absoluteFilePath() == QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        return shellText(9227, "My Documents");
    return info.fileName();
}

QIcon icon(const QString &location)
{
    if (isWeb(location))
        return fileicons::shell(fileicons::Search);
    if (location == Computer)
        return fileicons::shell(fileicons::Computer);
    if (location == RecycleBin)
        return fileicons::shell(QDir(fileops::trashFiles()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ? fileicons::RecycleEmpty : fileicons::RecycleFull);
    if (location == ControlPanel)
        return fileicons::shell(fileicons::ControlPanel);
    if (location.startsWith(ControlPanel + QLatin1Char('/')))
        return controlpanel::categories().value(location.section(QLatin1Char('/'), 1).toInt()).icon;
    if (location == Search)
        return fileicons::shell(fileicons::Search);
    return fileicons::forFile(QFileInfo(location));
}
} // namespace places

// "59.9 KB": XP's units, not "KiB"
static QString xpSize(qint64 bytes)
{
    if (bytes < 1024)
        return QObject::tr("%1 bytes").arg(bytes);
    // Three digits, the rest dropped: "59.9 KB"
    double value = bytes;
    int unit = -1;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    const int decimals = value < 10 ? 2 : value < 100 ? 1 : 0;
    const double scale = std::pow(10, decimals);
    return QStringLiteral("%1 %2").arg(QLocale().toString(std::floor(value * scale) / scale, 'f', decimals), QLatin1String(std::array{"KB", "MB", "GB", "TB"}[unit]));
}

// What XP keeps its users out of until they ask: its system drive and folders, here Linux's.
static bool isSystemFolder(const QString &path)
{
    return path == QLatin1String("/") || path == QLatin1String("/usr") || path == QLatin1String("/etc") || path == QLatin1String("/boot") || path == QLatin1String("/opt");
}

namespace {

// shell32.dll's column tables give each column's width in characters, which the view makes pixels
// with the list's font: 6 a character with Tahoma 8, measured on XP's (Name 180, Size 96, Type 120),
// the width of its "0". A file folder's: Name 30, Size 16, Type 20, Date Modified 20; My Computer's:
// Name 20, Type 25, Total Size 16, Free Space 16; the Control Panel's: Name 20, Comments 20; search
// results': Name 30, In Folder 30, then a file folder's.
void setColumnChars(QTreeView *view, std::initializer_list<int> chars)
{
    const int perChar = view->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    int column = 0;
    for (int count : chars)
        view->setColumnWidth(column++, count * perChar);
}

// The search results' columns: shell32.dll's words
void clearResults(QStandardItemModel *model)
{
    model->clear();
    model->setHorizontalHeaderLabels({xpfiles::shellString(8976, "Name"), xpfiles::shellString(8977, "In Folder"), xpfiles::shellString(8978, "Size"),
                                      xpfiles::shellString(8979, "Type"), xpfiles::shellString(8980, "Date Modified")});
}

// The results over the magnifier of the shell style, and what to do while there are none
class SearchResultsView : public QTreeView
{
public:
    explicit SearchResultsView(QWidget *parent) : QTreeView(parent)
    {
        const auto load = [this] {
            const LookChoice look = ShellTheme::instance()->choice();
            const QString file = ShellTheme::shellStylePath(look);
            const auto resources = xpfiles::resources(file);
            m_watermark = resources ? xpfiles::bitmap(*resources, BITMAP_SEARCH_WATERMARK) : QImage();
        };
        load();
        connect(ShellTheme::instance(), &ShellTheme::changed, this, load);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter painter(viewport());
            painter.fillRect(event->rect(), palette().base());
            if (!m_watermark.isNull())
                painter.drawImage(viewport()->width() - m_watermark.width(), viewport()->height() - m_watermark.height(), m_watermark);
            if (!model()->rowCount()) {
                painter.setPen(palette().text().color());
                painter.drawText(viewport()->rect().adjusted(4, 4, -4, -4), Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft,
                                 xpfiles::shellString(29470, "To start your search, follow the instructions in the left pane."));
            }
        }
        QTreeView::paintEvent(event);
    }
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem transparent = option;
        transparent.features &= ~QStyleOptionViewItem::Alternate;
        QTreeView::drawRow(painter, transparent, index);
    }

private:
    static constexpr int BITMAP_SEARCH_WATERMARK = 18;
    QImage m_watermark;
};

// shell32.dll's strip of the toolbar's 24-pixel pictures (214, hot 215) and where they are in it
const int STRIP_NORMAL = 214, STRIP_HOT = 215;
enum { BACK = 0, FORWARD = 1, DELETE_ICON = 10, UNDO = 8, SEARCH = 17, UP = 28, VIEWS = 32, FOLDERS = 43, MOVE_TO = 44, COPY_TO = 45 };
// shell32.dll's flag: the first 22-pixel frame of its animation, 9 pixels into its white tile of 39 x 22
const int BITMAP_FLAG = 240, FLAG_SIZE = 22, FLAG_X = 9, TILE_WIDTH = 39;
// shell32.dll's 20-pixel Go arrow of the address band, and its hot picture
const int BITMAP_GO = 230, BITMAP_GO_HOT = 231;
const int STRING_FILE_TASKS = 31233, STRING_NEW_FOLDER = 31236, STRING_RENAME = 31242, STRING_MOVE = 31244, STRING_COPY = 31246, STRING_DELETE = 31252;
const int STRING_OTHER_PLACES = 31272, STRING_DETAILS = 31274, STRING_SYSTEM_TASKS = 31232, STRING_SYSTEM_INFO = 31294, STRING_ADD_REMOVE = 31327, STRING_CHANGE = 31312, STRING_EMPTY = 31331;
const int STRING_PICTURE_TASKS = 31283, STRING_SLIDE_SHOW = 31287, STRING_BACKGROUND = 31289;
const int STRING_LABELLED = 31363, STRING_ITEMS_SELECTED = 31364, STRING_TOTAL_SIZE = 31365, STRING_SIZE = 8978, STRING_DATE_MODIFIED = 8980, STRING_DIMENSIONS = 9005;
// The Details group's preview, at most so wide and tall: its body's width
const int PREVIEW_SIDE = 160;
// The sorted column's shade in Details, on the window's white
const QColor SORTED_COLUMN(247, 247, 247);
const int STRING_HARD_DISKS = 9426, STRING_SWITCH_CLASSIC = 32150, STRING_SWITCH_CATEGORY = 32152, STRING_SEE_ALSO = 20868;

// Item data of the virtual lists, past the roles of QFileSystemModel
enum { ActionRole = Qt::UserRole + 100, HeaderRole, DetailRole, PathRole };

class VirtualFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void showGroups(bool groups) { if (m_groups != groups) { m_groups = groups; invalidate(); } }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    { return m_groups || !sourceModel()->index(row, 0, parent).data(HeaderRole).toBool(); }
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (left.column() >= 2 && left.data(Qt::UserRole).isValid() && right.data(Qt::UserRole).isValid())
            return left.data(Qt::UserRole).toULongLong() < right.data(Qt::UserRole).toULongLong();
        return QSortFilterProxyModel::lessThan(left, right);
    }
private:
    bool m_groups = true;
};

// The rebar's art is one picture for the menu and the toolbars together, down to the folder itself.
void drawBand(QPainter &painter, QWidget *row, w4w::Renderer *renderer)
{
    auto *window = qobject_cast<QMainWindow *>(row->window());
    const int bottom = window && window->centralWidget() ? window->centralWidget()->y() : row->height();
    const int top = row->mapTo(row->window(), QPoint(0, 0)).y();
    renderer->draw(&painter, QRect(0, -top, row->width(), bottom), w4w::Part{QStringLiteral("rebar"), {}, {}, 0, {}});
}

// XP's folder menu bar is no user32 menu bar but shell32's menu band, a comctl32 toolbar of text-only
// buttons with the default padding: each its words and 16 pixels (the words 8 in) and 19 tall, 2 under
// the band's top, the first 2 in from the window's client edge (measured on XP's explorer: File 6 to 37
// from the frame's edge and 31 to 49 from its top, Edit's words at 46, View's at 80).
class MenuBandStyle : public QProxyStyle
{
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override
    {
        if (type == CT_MenuBarItem)
            return QSize(size.width() + 16, 19);
        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_MenuBarHMargin:
            return 2;
        case PM_MenuBarVMargin:
            return 1;   // (the bar's own row starts a pixel under the band's top)
        case PM_MenuBarItemSpacing:
        case PM_MenuBarPanelWidth:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// The menu bar as XP's: its row, the flag's white tile at its right end, and the band's two lines under
// it (the first in the style's Rebar.Band EdgeShadowColor, as along the tile's left side)
class BrandMenuBar : public QMenuBar
{
public:
    explicit BrandMenuBar(QWidget *parent)
        : QMenuBar(parent)
    {
        static MenuBandStyle *style = new MenuBandStyle;
        setStyle(style);
        if (const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll"))))
            m_flag = xpfiles::bitmap(*shell32, BITMAP_FLAG).copy(0, 0, FLAG_SIZE, FLAG_SIZE);
    }
    QSize sizeHint() const override
    {
        return QSize(QMenuBar::sizeHint().width(), FLAG_SIZE + 2);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // Windows Classic's menu bar is Qt's own; it opens its own painter, before this one is.
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        if (!renderer)
            QMenuBar::paintEvent(event);
        QPainter painter(this);
        const QColor shadow = edgeColour();
        const int row = height() - 2;
        if (renderer) {
            // The band's art under the menu's names, not the style's flat colour; the item
            // under the pointer as the style draws it.
            drawBand(painter, this, renderer);
            // XP's menu band underlines its letters only once Alt is pressed ("Hide underlined
            // letters for keyboard navigation" is its default): Qt gives the bar the focus then.
            const int cues = style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this) || hasFocus() ? 0 : Qt::TextHideMnemonic;
            for (QAction *action : actions()) {
                QStyleOptionMenuItem option;
                initStyleOption(&option, action);
                option.rect = actionGeometry(action);
                if (option.state & (QStyle::State_Selected | QStyle::State_Sunken))
                    style()->drawControl(QStyle::CE_MenuBarItem, &option, &painter, this);
                else
                    style()->drawItemText(&painter, option.rect, Qt::AlignCenter | Qt::TextShowMnemonic | cues | Qt::TextSingleLine, option.palette,
                                          option.state & QStyle::State_Enabled, option.text, QPalette::ButtonText);
            }
        }
        painter.fillRect(QRect(0, row, width(), 1), shadow);
        painter.fillRect(QRect(0, row + 1, width(), 1), Qt::white);
        const QRect tile(width() - TILE_WIDTH, 0, TILE_WIDTH, row);
        painter.fillRect(QRect(tile.x() - 1, 0, 1, row), shadow);
        painter.fillRect(tile, Qt::white);
        painter.drawImage(tile.x() + FLAG_X, (row - m_flag.height()) / 2, m_flag);
    }
    void focusInEvent(QFocusEvent *event) override { QMenuBar::focusInEvent(event); update(); }
    void focusOutEvent(QFocusEvent *event) override { QMenuBar::focusOutEvent(event); update(); }

private:
    static QColor edgeColour()
    {
        ShellTheme *theme = ShellTheme::instance();
        if (w4w::Renderer *renderer = theme->renderer())
            if (const auto colour = renderer->colour(w4w::Part{QStringLiteral("rebar"), QStringLiteral("band"), {}, 0, {}}, QStringLiteral("edgeshadowcolor")))
                return *colour;
        return theme->system(QStringLiteral("btnshadow"), Qt::gray);
    }

    QImage m_flag;
};

// The rebar's bands place their buttons themselves, edge to edge as comctl32's toolbar does.
class BandStyle : public QProxyStyle
{
public:
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_ToolBarFrameWidth:
        case PM_ToolBarItemMargin:
        case PM_ToolBarItemSpacing:
        case PM_ToolBarHandleExtent:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// A toolbar on the visual style's rebar art, as XP's folder windows have it. A band above another one
// ends with the same two lines as the menu's (the rebar's band borders).
class RebarToolBar : public QToolBar
{
public:
    RebarToolBar(const QString &title, QWidget *parent, bool border)
        : QToolBar(title, parent), m_border(border)
    {
        static BandStyle *style = new BandStyle;
        setStyle(style);
        setContentsMargins(0, 0, 0, border ? 2 : 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        // (Qt's own toolbar under Windows Classic opens its own painter first.)
        if (!renderer)
            QToolBar::paintEvent(event);
        QPainter painter(this);
        if (renderer)
            drawBand(painter, this, renderer);
        if (m_border) {
            ShellTheme *theme = ShellTheme::instance();
            const QColor shadow = renderer ? renderer->colour(w4w::Part{QStringLiteral("rebar"), QStringLiteral("band"), {}, 0, {}}, QStringLiteral("edgeshadowcolor"))
                                                 .value_or(theme->system(QStringLiteral("btnshadow"), Qt::gray))
                                           : theme->system(QStringLiteral("btnshadow"), Qt::gray);
            painter.fillRect(QRect(0, height() - 2, width(), 1), shadow);
            painter.fillRect(QRect(0, height() - 1, width(), 1), theme->system(QStringLiteral("btnhighlight"), Qt::white));
        }
    }

private:
    bool m_border;
};

// A button of the folder window's toolbars, laid out as on XP (comctl32's list toolbar, as browseui fills
// it), measured there with Tahoma 8: the picture centred in the height; with words, the picture 3 from the
// left, the words 4 after it and 8 after them; without, 4 on each side. Back and Forward have 13 more for
// their arrow, and Back's picture and words are 4 further in (7 after the words), Forward's picture 8 in
// and 6 after. Views' arrow, 5 x 3, is 2 after its picture, 4 from the right.
class BandButton : public QToolButton
{
public:
    enum Kind { Plain, Split, WholeDropDown };
    BandButton(QWidget *parent, int height, int iconSide, Kind kind = Plain)
        : QToolButton(parent), m_height(height), m_kind(kind)
    {
        setAutoRaise(true);
        setIconSize(QSize(iconSide, iconSide));
        if (kind == Split)
            setPopupMode(QToolButton::MenuButtonPopup);
        else if (kind == WholeDropDown)
            setPopupMode(QToolButton::InstantPopup);
    }
    QSize sizeHint() const override
    {
        const int icon = iconSize().width(), words = textWidth();
        int width;
        if (m_kind == Split)
            width = (words ? 7 + icon + 4 + words + 7 : 8 + icon + 6) + ARROW;
        else if (m_kind == WholeDropDown)
            width = 4 + icon + 2 + 5 + 4;
        else
            width = words ? 3 + icon + 4 + words + 8 : 4 + icon + 4;
        return QSize(width, m_height);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // The arrow's 13 pixels open the history, as XP's split button.
        if (m_kind == Split && event->button() == Qt::LeftButton && event->position().x() >= width() - ARROW && isEnabled()) {
            m_arrowDown = true;
            update();
            showMenu();
            m_arrowDown = false;
            update();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        const bool enabled = isEnabled(), hot = enabled && underMouse();
        const auto state = [&](bool down) {
            if (!enabled)
                return 4;
            if (down)
                return 3;
            if (isChecked())
                return hot ? 6 : 5;
            return hot ? 2 : 1;
        };
        const QRect main = m_kind == Split ? QRect(0, 0, width() - ARROW, height()) : rect();
        const int mainState = state(isDown() && !m_arrowDown);
        const auto frame = [&](const QRect &r, const QString &part, int id) {
            if (renderer) {
                renderer->draw(&painter, r, w4w::Part{QStringLiteral("toolbar"), part, {}, id, {}});
            } else if (id == 2 || id == 3 || id == 5 || id == 6) {
                QStyleOption option;
                option.initFrom(this);
                option.rect = r;
                option.state |= (id == 2 ? QStyle::State_Raised : QStyle::State_Sunken);
                style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);
            }
        };
        // At rest a button has no frame; Back's and Forward's arrow, a picture of the part, always shows.
        if (mainState != 1 && mainState != 4)
            frame(main, m_kind == Split ? QStringLiteral("splitbutton") : QStringLiteral("button"), mainState);
        if (m_kind == Split) {
            const QRect part(main.right() + 1, 0, ARROW, height());
            frame(part, QStringLiteral("splitbuttondropdown"), state(m_arrowDown));
            // Windows Classic has no picture of the arrow: comctl32's own, 5 x 3
            if (!renderer) {
                const QColor ink = palette().color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
                for (int row = 0; row < 3; ++row)
                    painter.fillRect(QRect(part.x() + 4 + row, (height() - 3) / 2 + row, 5 - 2 * row, 1), ink);
            }
        }

        const int icon = iconSize().width(), words = textWidth();
        int x = words ? (m_kind == Split ? 7 : 3) : (m_kind == Split ? 8 : 4);
        if (m_kind == Plain && !words)
            x = (width() - icon) / 2;
        const QIcon::Mode mode = !enabled ? QIcon::Disabled : hot ? QIcon::Active : QIcon::Normal;
        painter.drawPixmap(x, (height() - icon) / 2, this->icon().pixmap(iconSize(), devicePixelRatio(), mode));
        x += icon;
        if (words) {
            style()->drawItemText(&painter, QRect(x + 4, 0, words, height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextHideMnemonic,
                                  palette(), enabled, text(), QPalette::ButtonText);
        }
        if (m_kind == WholeDropDown) {
            const QColor ink = palette().color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
            const int ax = x + 2, ay = (height() - 3) / 2;
            for (int row = 0; row < 3; ++row)
                painter.fillRect(QRect(ax + row, ay + row, 5 - 2 * row, 1), ink);
        }
    }

private:
    static constexpr int ARROW = 13;
    int textWidth() const
    {
        return toolButtonStyle() == Qt::ToolButtonIconOnly || text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text());
    }

    int m_height;
    Kind m_kind;
    bool m_arrowDown = false;
};

// A separator between the toolbar's groups: 6 wide, the style's line 2 in, as on XP
class BandSeparator : public QWidget
{
public:
    BandSeparator(QWidget *parent, int height)
        : QWidget(parent)
    {
        setFixedSize(6, height);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        option.state |= QStyle::State_Horizontal;
        option.rect = rect().translated(-1, 0);   // its line 2 in, as on XP
        style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &option, &painter, this);
    }
};

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

QIcon stripIcon(int index)
{
    static const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    QIcon icon;
    if (!shell32)
        return icon;
    static const QImage normal = xpfiles::bitmap(*shell32, STRIP_NORMAL), hot = xpfiles::bitmap(*shell32, STRIP_HOT);
    const int side = normal.height();
    icon.addPixmap(QPixmap::fromImage(normal.copy(index * side, 0, side, side)), QIcon::Normal);
    icon.addPixmap(QPixmap::fromImage(hot.copy(index * side, 0, side, side)), QIcon::Active);
    return icon;
}

// The file model with XP's icons, asked here (the GUI thread), not in the model's own thread
class FolderModelImpl : public QIdentityProxyModel
{
public:
    using QIdentityProxyModel::QIdentityProxyModel;
    QFileSystemModel *files() const { return static_cast<QFileSystemModel *>(sourceModel()); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DecorationRole && index.column() == 0)
            return fileicons::forFile(files()->fileInfo(mapToSource(index)));
        // Details, as XP words it: sizes in KB, no size for a folder, "File Folder"
        if (role == Qt::DisplayRole && (index.column() == 1 || index.column() == 2)) {
            const QFileInfo info = files()->fileInfo(mapToSource(index));
            if (index.column() == 2)
                return fileicons::typeName(info);
            return info.isDir() ? QString() : fileicons::sizeText(info.size());
        }
        return QIdentityProxyModel::data(index, role);
    }
    // Size's header to the right: shell32's LVCFMT_RIGHT
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal && section == 1)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return QIdentityProxyModel::headerData(section, orientation, role);
    }
    QString path(const QModelIndex &index) const { return files()->filePath(mapToSource(index)); }
};

// XP list/report rows: max(text height, small icon height) + SM_CYBORDER.
int compactRowHeight(const QFontMetrics &metrics)
{
    return qMax(metrics.height(), ShellTheme::instance()->scaled(16)) + ShellTheme::instance()->scaled(1);
}

void paintCompactItem(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, QAbstractItemView *view)
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool active = (option.state & QStyle::State_Active) && view->hasFocus();
    const int side = ShellTheme::instance()->scaled(16);
    const int inset = qobject_cast<QTreeView *>(view) ? ShellTheme::instance()->scaled(4) : 0;
    const QRect iconRect(option.rect.x() + inset, option.rect.y() + (option.rect.height() - side) / 2, side, side);
    const QRect available(iconRect.right() + 1 + 2, option.rect.y(), qMax(0, option.rect.right() - iconRect.right() - 3), option.rect.height());
    const QString text = option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, available.width());
    const QRect label(available.x() - 1, available.y(), qMin(available.width() + 2, option.fontMetrics.horizontalAdvance(text) + 2), available.height());
    const QColor background = selected ? option.palette.color(active ? QPalette::Highlight : QPalette::Button) : option.palette.color(QPalette::Base);
    painter->save();
    painter->setClipRect(option.rect);
    painter->setFont(option.font);
    if (selected) painter->fillRect(label, background);
    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    icon.paint(painter, iconRect, Qt::AlignCenter, selected && active ? QIcon::Selected : QIcon::Normal);
    painter->setPen(option.palette.color(selected && active ? QPalette::HighlightedText : QPalette::Text));
    painter->drawText(available, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    if (option.state & QStyle::State_HasFocus) {
        QStyleOptionFocusRect focus;
        focus.QStyleOption::operator=(option);
        focus.rect = label; focus.backgroundColor = background;
        view->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, view);
    }
    painter->restore();
}

class ExplorerDetailsView : public QTreeView
{
public:
    using QTreeView::QTreeView;
    QModelIndex indexAt(const QPoint &point) const override
    {
        const QModelIndex index = QTreeView::indexAt(point);
        if (!index.isValid() || index.column() != 0) return {};
        const QRect rect = visualRect(index);
        const int end = rect.x() + ShellTheme::instance()->scaled(20) + 2 + fontMetrics().horizontalAdvance(index.data().toString()) + 2;
        return point.x() < qMin(rect.right() + 1, end) ? index : QModelIndex();
    }
};

class DetailsDelegate : public QStyledItemDelegate
{
public:
    static constexpr int GROUP_HEADER = 31, GROUP_GAP = 12, GROUP_LINE = 300;
    explicit DetailsDelegate(QTreeView *view) : QStyledItemDelegate(view), m_view(view) {}
    void setGroups(bool groups, int column)
    {
        m_groups = groups;
        m_groupColumn = column;
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(compactRowHeight(option.fontMetrics) + (m_groups && startsGroup(index) ? ShellTheme::instance()->scaled(GROUP_HEADER + (index.row() ? GROUP_GAP : 0)) : 0));
        return size;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(option);
        if (m_groups && startsGroup(index)) {
            const int header = ShellTheme::instance()->scaled(GROUP_HEADER), gap = index.row() ? ShellTheme::instance()->scaled(GROUP_GAP) : 0;
            if (index.column() == 0) {
                // XP's group header: bold words 14 in, and 17 under its top a line of 300 pixels fading
                // from the caption's gradient colour to the window's.
                painter->save();
                QFont bold = option.font;
                bold.setBold(true);
                painter->setFont(bold);
                painter->setPen(option.palette.color(QPalette::Text));
                painter->drawText(QRect(option.rect.x() + ShellTheme::instance()->scaled(14), option.rect.y() + ShellTheme::instance()->scaled(2),
                                        option.rect.width() - ShellTheme::instance()->scaled(14), QFontMetrics(bold).height()),
                                  Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, groupKey(index));
                const QRect rule(option.rect.x(), option.rect.y() + ShellTheme::instance()->scaled(17),
                                 qMin(ShellTheme::instance()->scaled(GROUP_LINE), option.rect.width()), 1);
                QLinearGradient line(rule.topLeft(), rule.topRight());
                line.setColorAt(0, ShellTheme::instance()->system(QStringLiteral("gradientactivecaption"), QColor(61, 149, 255)));
                line.setColorAt(1, option.palette.color(QPalette::Base));
                painter->fillRect(rule, line);
                painter->restore();
            }
            item.rect = option.rect.adjusted(0, header + gap, 0, 0);
        }
        if (index.column() == 0) {
            paintCompactItem(painter, item, index, m_view);
            return;
        }
        item.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
        QStyledItemDelegate::paint(painter, item, index);
    }
private:
    bool startsGroup(const QModelIndex &index) const
    {
        return index.row() == 0 || groupKey(index) != groupKey(index.sibling(index.row() - 1, 0));
    }
    QString groupKey(const QModelIndex &index) const
    {
        if (!index.isValid())
            return QString();
        const auto *model = static_cast<const FolderModelImpl *>(m_view->model());
        if (!model)
            return QString();
        const QFileInfo info = model->files()->fileInfo(model->mapToSource(index));
        if (m_groupColumn == 1) {
            const qint64 size = info.size();
            if (size == 0) return QObject::tr("0 KB");
            if (size < 10 * 1024) return QObject::tr("Tiny");
            if (size < 100 * 1024) return QObject::tr("Small");
            if (size < 1024 * 1024) return QObject::tr("Medium");
            if (size < 16 * 1024 * 1024) return QObject::tr("Large");
            return QObject::tr("Huge");
        }
        if (m_groupColumn == 2)
            return index.sibling(index.row(), 2).data().toString();
        if (m_groupColumn == 3) {
            const QDate date = info.lastModified().date(), today = QDate::currentDate();
            if (date == today) return QObject::tr("Today");
            if (date == today.addDays(-1)) return QObject::tr("Yesterday");
            if (date >= today.addDays(-7)) return QObject::tr("Earlier this week");
            if (date >= today.addDays(-30)) return QObject::tr("Earlier this month");
            return QObject::tr("A long time ago");
        }
        const QString name = info.fileName();
        if (name.isEmpty()) return QObject::tr("Other");
        const QChar first = name.at(0).toUpper();
        return first.isDigit() ? QStringLiteral("#") : QString(first);
    }
    QTreeView *m_view;
    bool m_groups = false;
    int m_groupColumn = 0;
};

// How a view draws an item: XP's Tiles and Thumbnails; My Computer's group headers
class ItemDelegate : public QStyledItemDelegate
{
    static constexpr int ICONS_TOP = 8, ICONS_SIDE = 32, ICONS_GAP = 4, ICONS_TEXT_WIDTH = 71, ICONS_LINES = 2;
    // XP's Tiles and group headers, measured on its My Computer
    static constexpr int TILES_WIDTH = 218, TILES_HEIGHT = 56, GROUP_HEADER = 31, GROUP_GAP = 12, GROUP_LINE = 300;

    // XP's Icons: in a cell of 75 pixels the icon, 8 from the top, then the name, on two lines
    // unless it is selected. Selected, the icon is tinted and the name stands on the highlight,
    // only as wide as its words; grey when the window does not have the keyboard.
    void paintIcon(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        const bool selected = option.state & QStyle::State_Selected;
        const bool active = (option.state & QStyle::State_Active) && m_view->hasFocus();
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect place(option.rect.x() + (option.rect.width() - ICONS_SIDE) / 2, option.rect.y() + ICONS_TOP, ICONS_SIDE, ICONS_SIDE);
        icon.paint(painter, place, Qt::AlignCenter, selected && active ? QIcon::Selected : QIcon::Normal);
        const QString name = index.data().toString();
        const QFontMetrics metrics(option.font);
        const int flags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere;
        const int x = option.rect.x() + (option.rect.width() - ICONS_TEXT_WIDTH) / 2, y = place.bottom() + 1 + ICONS_GAP;
        QRect words = metrics.boundingRect(QRect(x, y, ICONS_TEXT_WIDTH, 1000), flags, name);
        if (!selected)
            words.setHeight(qMin(words.height(), ICONS_LINES * metrics.lineSpacing()));
        painter->save();
        painter->setFont(option.font);
        if (selected)
            painter->fillRect(words.adjusted(-2, 0, 2, 0), active ? option.palette.color(QPalette::Active, QPalette::Highlight) : option.palette.color(QPalette::Inactive, QPalette::Button));
        painter->setPen(selected && active ? option.palette.color(QPalette::Active, QPalette::HighlightedText) : option.palette.color(QPalette::Text));
        painter->setClipRect(words.adjusted(-2, 0, 2, 0));
        painter->drawText(QRect(x, y, ICONS_TEXT_WIDTH, 1000), flags, name);
        painter->restore();
    }

public:
    enum Mode { Plain, IconsMode, TilesMode, ThumbnailMode };
    ItemDelegate(QListView *view, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_view(view)
    {
    }
    void setMode(Mode mode) { m_mode = mode; }
    bool hit(const QRect &rect, const QPoint &point, const QModelIndex &index) const
    {
        if (index.data(HeaderRole).toBool()) return true;
        const QFontMetrics metrics(m_view->font());
        const QString name = index.data().toString();
        if (m_mode == Plain)
            return point.x() < rect.x() + ShellTheme::instance()->scaled(16) + 2 + metrics.horizontalAdvance(name) + 2;
        if (m_mode == IconsMode) {
            const QRect icon(rect.x() + (rect.width() - ICONS_SIDE) / 2, rect.y() + ICONS_TOP, ICONS_SIDE, ICONS_SIDE);
            QRect text = metrics.boundingRect(QRect(rect.x() + (rect.width() - ICONS_TEXT_WIDTH) / 2, icon.bottom() + 1 + ICONS_GAP, ICONS_TEXT_WIDTH, 1000),
                                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere, name);
            if (!m_view->selectionModel()->isSelected(index)) text.setHeight(qMin(text.height(), ICONS_LINES * metrics.lineSpacing()));
            return icon.contains(point) || text.adjusted(-2, 0, 2, 0).contains(point);
        }
        if (m_mode == ThumbnailMode) {
            const QRect box(rect.x() + (rect.width() - 100) / 2, rect.y() + 4, 100, 100);
            const int width = metrics.horizontalAdvance(metrics.elidedText(name, Qt::ElideRight, rect.width() - 4)) + 4;
            const QRect text(rect.x() + (rect.width() - width) / 2, box.bottom() + 5, width, 16);
            return box.contains(point) || text.contains(point);
        }
        return true;
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        // A group's header: 31 pixels down to its first tile, and 12 more above it after another group
        if (index.data(HeaderRole).toBool())
            return QSize(qMax(100, m_view->viewport()->width() - 20), GROUP_HEADER + (index.row() > 0 ? GROUP_GAP : 0));
        if (m_mode == Plain)
            return QSize(100, compactRowHeight(option.fontMetrics));
        if (m_mode == IconsMode)
            return QSize(75, 75);
        if (m_mode == TilesMode)
            return QSize(TILES_WIDTH, TILES_HEIGHT);
        if (m_mode == ThumbnailMode)
            return QSize(120, 134);
        return QStyledItemDelegate::sizeHint(option, index);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(HeaderRole).toBool()) {
            // XP's group header: its bold words 14 in, and 17 under its top a line of 300 pixels fading
            // from the caption's gradient colour to the window's
            painter->save();
            const QRect header = option.rect.adjusted(0, option.rect.height() - GROUP_HEADER, 0, 0);
            QFont bold = option.font;
            bold.setBold(true);
            painter->setFont(bold);
            painter->drawText(QRect(header.x() + 14, header.y() + 2, header.width() - 14, QFontMetrics(bold).height()), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine,
                              index.data().toString());
            const QRect rule(header.x(), header.y() + 17, qMin(GROUP_LINE, header.width()), 1);
            QLinearGradient line(rule.topLeft(), rule.topRight());
            line.setColorAt(0, ShellTheme::instance()->system(QStringLiteral("gradientactivecaption"), QColor(61, 149, 255)));
            line.setColorAt(1, option.palette.color(QPalette::Base));
            painter->fillRect(rule, line);
            painter->restore();
            return;
        }
        if (m_mode == Plain) {
            paintCompactItem(painter, option, index, m_view);
            return;
        }
        if (m_mode == IconsMode) {
            paintIcon(painter, option, index);
            return;
        }
        const bool selected = option.state & QStyle::State_Selected;
        const bool active = (option.state & QStyle::State_Active) && m_view->hasFocus();
        const QPalette &palette = option.palette;
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QString name = index.data().toString();
        painter->save();
        painter->setFont(option.font);
        if (m_mode == TilesMode) {
            // XP's tile: its 48-pixel icon 16 in and 4 down, the words 69 in
            const QRect iconRect(option.rect.x() + 16, option.rect.y() + 4, 48, 48);
            icon.paint(painter, iconRect, Qt::AlignCenter, selected && active ? QIcon::Selected : QIcon::Normal);
            const QRect text = option.rect.adjusted(69, 5, -4, -5);
            painter->setPen(selected && active ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text));
            QStringList lines{name};
            const QString detail = index.data(DetailRole).toString();
            if (!detail.isEmpty()) {
                lines += detail.split(QLatin1Char('\n'));
            } else if (const auto *model = dynamic_cast<const FolderModelImpl *>(index.model())) {
                const QFileInfo info = model->files()->fileInfo(model->mapToSource(index));
                lines << fileicons::typeName(info);
                if (!info.isDir())
                    lines << fileicons::sizeText(info.size());
            }
            const int lineHeight = option.fontMetrics.height();
            int labelWidth = 0;
            for (const QString &line : std::as_const(lines))
                labelWidth = qMax(labelWidth, option.fontMetrics.horizontalAdvance(option.fontMetrics.elidedText(line, Qt::ElideRight, text.width())));
            int y = text.y() + qMax(0, (text.height() - int(lines.size()) * lineHeight) / 2);
            const QRect selectedLabel(text.x() - 2, y, qMin(text.width() + 2, labelWidth + 4), int(lines.size()) * lineHeight);
            if (selected)
                painter->fillRect(selectedLabel, palette.color(active ? QPalette::Highlight : QPalette::Button));
            for (int i = 0; i < lines.size(); ++i) {
                if (i == 1 && !(selected && active))
                    painter->setPen(QColor(128, 128, 128));
                painter->drawText(QRect(text.x(), y, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  option.fontMetrics.elidedText(lines[i], Qt::ElideRight, text.width()));
                y += lineHeight;
            }
            if (option.state & QStyle::State_HasFocus) {
                QStyleOptionFocusRect focus; focus.QStyleOption::operator=(option);
                focus.rect = selectedLabel; focus.backgroundColor = palette.color(selected && active ? QPalette::Highlight : QPalette::Base);
                m_view->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, m_view);
            }
        } else {
            paintThumbnail(painter, option, index, icon, name);
        }
        painter->restore();
    }

private:
    static constexpr int THUMB_BOX = 100, THUMB_SIDE = 96, THUMB_TOP = 4, THUMB_GAP = 4, THUMB_TEXT = 16;

    // XP's Thumbnails: the picture within a white square of 96 pixels and its frame, the type's
    // icon for other files, a folder's first pictures; the name on a line under it, highlighted
    // only as wide as its words. Selected, the frame is the highlight, two pixels wide.
    void paintThumbnail(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, const QIcon &icon, const QString &name) const
    {
        const bool selected = option.state & QStyle::State_Selected, active = (option.state & QStyle::State_Active) && m_view->hasFocus();
        const QPalette &palette = option.palette;
        const QColor highlight = active ? palette.color(QPalette::Active, QPalette::Highlight) : palette.color(QPalette::Inactive, QPalette::Button);
        const QRect box(option.rect.x() + (option.rect.width() - THUMB_BOX) / 2, option.rect.y() + THUMB_TOP, THUMB_BOX, THUMB_BOX);
        painter->fillRect(box, Qt::white);
        const auto *model = dynamic_cast<const FolderModelImpl *>(index.model());
        const QString path = model ? model->path(index) : QString();
        const bool folder = model && QFileInfo(path).isDir();
        const QImage thumbnail = model ? pictures::Thumbnails::instance()->get(path, folder ? THUMB_SIDE - 12 : THUMB_SIDE) : QImage();
        if (!thumbnail.isNull() && folder) {
            // Its pictures two by two, the folder's icon over their lower left corner
            const QRect inner = box.adjusted(8, 8, -8, -8);
            painter->drawImage(inner.x() + (inner.width() - thumbnail.width()) / 2, inner.y() + (inner.height() - thumbnail.height()) / 2, thumbnail);
            icon.paint(painter, QRect(box.x() + 3, box.bottom() - 34, 32, 32), Qt::AlignCenter);
        } else if (!thumbnail.isNull()) {
            painter->drawImage(box.x() + (box.width() - thumbnail.width()) / 2, box.y() + (box.height() - thumbnail.height()) / 2, thumbnail);
        } else {
            icon.paint(painter, box.adjusted(26, 26, -26, -26), Qt::AlignCenter);
        }
        if (selected) {
            painter->fillRect(QRect(box.x(), box.y(), box.width(), 2), highlight);
            painter->fillRect(QRect(box.x(), box.bottom() - 1, box.width(), 2), highlight);
            painter->fillRect(QRect(box.x(), box.y(), 2, box.height()), highlight);
            painter->fillRect(QRect(box.right() - 1, box.y(), 2, box.height()), highlight);
        } else {
            painter->setPen(QColor(201, 199, 186));
            painter->drawRect(box.adjusted(0, 0, -1, -1));
        }
        const QFontMetrics &metrics = option.fontMetrics;
        const QString shown = metrics.elidedText(name, Qt::ElideRight, option.rect.width() - 4);
        const int width = metrics.horizontalAdvance(shown) + 4;
        const QRect text(option.rect.x() + (option.rect.width() - width) / 2, box.bottom() + 1 + THUMB_GAP, width, THUMB_TEXT);
        if (selected)
            painter->fillRect(text, highlight);
        painter->setPen(selected && active ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text));
        painter->drawText(text, Qt::AlignCenter | Qt::TextSingleLine, shown);
    }

    QListView *m_view;
    Mode m_mode = Plain;
};

class ExplorerListView : public QListView
{
public:
    using QListView::QListView;
    QModelIndex indexAt(const QPoint &point) const override
    {
        const QModelIndex index = QListView::indexAt(point);
        if (!index.isValid()) return {};
        return static_cast<ItemDelegate *>(itemDelegate())->hit(visualRect(index), point, index) ? index : QModelIndex();
    }
};

ItemDelegate *delegateOf(QListView *view)
{
    return static_cast<ItemDelegate *>(view->itemDelegate());
}

// The sizes of a selection, added up little by little
class SizeCounter : public QObject
{
public:
    SizeCounter(const QStringList &paths, QLabel *size, QLabel *contains)
        : QObject(size)
        , m_pending(paths)
        , m_size(size)
        , m_contains(contains)
    {
        connect(&m_timer, &QTimer::timeout, this, &SizeCounter::step);
        m_timer.start(0);
    }

private:
    void step()
    {
        for (int i = 0; i < 200 && !m_pending.isEmpty(); ++i) {
            const QFileInfo info(m_pending.takeLast());
            if (info.isDir() && !info.isSymLink()) {
                ++m_folders;
                const QFileInfoList children = QDir(info.absoluteFilePath()).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
                for (const QFileInfo &child : children)
                    m_pending.append(child.absoluteFilePath());
            } else {
                ++m_filesCount;
                m_bytes += info.size();
            }
        }
        m_size->setText(QObject::tr("%1 (%2 bytes)").arg(xpSize(m_bytes), QLocale().toString(m_bytes)));
        if (m_contains)
            m_contains->setText(QObject::tr("%1 Files, %2 Folders").arg(m_filesCount).arg(qMax(0, m_folders - 1)));
        if (m_pending.isEmpty())
            m_timer.stop();
    }

    QTimer m_timer;
    QStringList m_pending;
    QLabel *m_size, *m_contains;
    qint64 m_bytes = 0;
    int m_filesCount = 0, m_folders = 0;
};

// XP's drive pie: the used part and the free part
class DrivePie : public QWidget
{
public:
    DrivePie(double used, QWidget *parent)
        : QWidget(parent)
        , m_used(used)
    {
        setFixedSize(150, 90);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect top(10, 8, 130, 62);
        painter.setPen(Qt::black);
        for (int i = 16; i > 0; --i) {
            painter.setBrush(QColor(0, 0, 160));
            painter.drawPie(top.translated(0, i), 0, 360 * 16);
        }
        painter.setBrush(QColor(255, 0, 255));
        painter.drawEllipse(top);
        painter.setBrush(QColor(0, 0, 255));
        painter.drawPie(top, 0, int(-m_used * 360 * 16));
    }

private:
    double m_used;
};

void showItemProperties(const QStringList &paths, QWidget *parent)
{
    if (paths.isEmpty())
        return;
    // Files and folders have shell32's sheet; a drive keeps this page
    if (!(paths.size() == 1 && QFileInfo(paths.first()).isRoot())) {
        fileproperties::show(paths, parent);
        return;
    }
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QFileInfo first(paths.first());
    const bool one = paths.size() == 1;
    dialog->setWindowTitle(QObject::tr("%1 Properties").arg(one ? (first.isRoot() ? first.absoluteFilePath() : first.fileName()) : QObject::tr("%1 Items").arg(paths.size())));
    auto *outer = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget(dialog);
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);
    int row = 0;
    const auto line = [&] {
        auto *rule = new QFrame(page);
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Sunken);
        grid->addWidget(rule, row++, 0, 1, 2);
    };
    const auto field = [&](const QString &label, const QString &value) {
        grid->addWidget(new QLabel(label, page), row, 0);
        auto *text = new QLabel(value, page);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setWordWrap(true);
        grid->addWidget(text, row++, 1);
        return text;
    };
    auto *picture = new QLabel(page);
    picture->setPixmap(fileicons::forFile(first).pixmap(32, 32));
    grid->addWidget(picture, row, 0);
    auto *nameEdit = new QLineEdit(one ? first.fileName() : QObject::tr("%1 Files and Folders").arg(paths.size()), page);
    nameEdit->setReadOnly(!one || first.isRoot());
    grid->addWidget(nameEdit, row++, 1);
    line();
    if (one && first.isRoot()) {
        const QStorageInfo drive(first.absoluteFilePath());
        field(QObject::tr("Type:"), QObject::tr("Local Disk"));
        field(QObject::tr("File system:"), QString::fromLatin1(drive.fileSystemType()));
        line();
        const qint64 total = drive.bytesTotal(), free = drive.bytesAvailable();
        field(QObject::tr("Used space:"), QObject::tr("%1 (%2 bytes)").arg(xpSize(total - free), QLocale().toString(total - free)));
        field(QObject::tr("Free space:"), QObject::tr("%1 (%2 bytes)").arg(xpSize(free), QLocale().toString(free)));
        line();
        field(QObject::tr("Capacity:"), QObject::tr("%1 (%2 bytes)").arg(xpSize(total), QLocale().toString(total)));
        grid->addWidget(new DrivePie(total > 0 ? double(total - free) / total : 0, page), row++, 0, 1, 2, Qt::AlignCenter);
    } else {
        field(QObject::tr("Type of file:"), one ? fileicons::typeName(first) : QObject::tr("Multiple Types"));
        field(QObject::tr("Location:"), first.absolutePath());
        QLabel *size = field(QObject::tr("Size:"), QString());
        QLabel *contains = first.isDir() || !one ? field(QObject::tr("Contains:"), QString()) : nullptr;
        new SizeCounter(paths, size, contains);
        if (one) {
            line();
            const auto date = [](const QDateTime &time) { return QLocale().toString(time, QLocale::LongFormat); };
            field(QObject::tr("Created:"), date(first.birthTime().isValid() ? first.birthTime() : first.metadataChangeTime()));
            field(QObject::tr("Modified:"), date(first.lastModified()));
            field(QObject::tr("Accessed:"), date(first.lastRead()));
        }
    }
    grid->setRowStretch(row, 1);
    grid->setColumnStretch(1, 1);
    tabs->addTab(page, QObject::tr("General"));
    outer->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, nameEdit, first, one] {
        if (one && nameEdit->text() != first.fileName() && !nameEdit->text().contains(QLatin1Char('/')))
            QFile::rename(first.absoluteFilePath(), first.absoluteDir().filePath(nameEdit->text()));
        dialog->accept();
    });
    dialog->resize(370, 430);
    dialog->show();
}

} // namespace

// Filmstrip's large picture, over the strip: the selected picture as large as the place lets it,
// never larger than it is; the type's icon for another file. A double click opens it.
class FilmPreview : public QWidget
{
public:
    FilmPreview(std::function<void()> open, QWidget *parent)
        : QWidget(parent)
        , m_open(std::move(open))
    {
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Base);
        setMinimumHeight(60);
    }
    void setPath(const QString &path)
    {
        if (path == m_path && !m_stale)
            return;
        m_path = path;
        m_stale = false;
        reload();
    }
    // The file changed (turned): read it again.
    void refresh()
    {
        reload();
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        reload();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (!m_image.isNull()) {
            const QSizeF shown = m_image.deviceIndependentSize();
            painter.drawImage(QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown), m_image);
        } else if (!m_path.isEmpty()) {
            const QRect place((width() - 48) / 2, (height() - 48) / 2, 48, 48);
            fileicons::forFile(QFileInfo(m_path)).paint(&painter, place);
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        if (!m_path.isEmpty())
            m_open();
    }

private:
    void reload()
    {
        const qreal ratio = devicePixelRatioF();
        m_image = m_path.isEmpty() || !isVisible() ? QImage() : pictures::load(m_path, (size() - QSize(16, 16)).expandedTo(QSize(1, 1)) * ratio);
        m_image.setDevicePixelRatio(ratio);
        m_stale = !isVisible();
        update();
    }
    void showEvent(QShowEvent *) override
    {
        if (m_stale)
            reload();
    }

    std::function<void()> m_open;
    QString m_path;
    QImage m_image;
    bool m_stale = false;
};

class FolderModel : public FolderModelImpl
{
public:
    using FolderModelImpl::FolderModelImpl;
};

// (ControlPanelCategories is complete here through controlpanel.h.)

void FileBrowser::open(const QString &location)
{
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<FileBrowser *>(widget); window && window->location() == location) {
            window->show();
            window->raise();
            window->activateWindow();
            return;
        }
    auto *window = new FileBrowser(location);
    window->show();
}

void FileBrowser::settingsChanged()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<FileBrowser *>(widget))
            window->reloadSettings();
}

FileBrowser::FileBrowser(const QString &location)
{
    setAttribute(Qt::WA_DeleteOnClose);
    // XP's browseui opens a folder it has no saved place for at 800 x 600 (its bag's WinPos on a
    // 1024 x 768 screen: 44,58 to 844,658), frame included; less on a smaller screen.
    {
        const QRect work = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1024, 738);
        const FrameStyle &frame = ShellTheme::instance()->frame();
        const int width = qMin(ShellTheme::instance()->scaled(800), work.width()) - 2 * frame.border;
        const int height = qMin(ShellTheme::instance()->scaled(600), work.height()) - frame.top - frame.border;
        resize(width, height);
    }
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_view = View(qBound(0, settings.value(QStringLiteral("view"), int(Tiles)).toInt(), int(Filmstrip)));
    m_groups = settings.value(QStringLiteral("folder/groups"), false).toBool();
    m_autoArrange = settings.value(QStringLiteral("folder/autoArrange"), true).toBool();
    m_foldersShown = settings.value(QStringLiteral("folders"), false).toBool();

    m_files = new QFileSystemModel(this);
    m_files->setIconProvider(nullptr);
    m_files->setReadOnly(false);
    m_files->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
    m_files->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model = new FolderModel(this);
    m_model->setSourceModel(m_files);
    m_virtual = new QStandardItemModel(this);
    m_virtual->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Total Size"), tr("Free Space")});
    m_searchTimer = new QTimer(this);
    connect(m_searchTimer, &QTimer::timeout, this, &FileBrowser::continueSearch);

    buildPanes();
    buildMenus();
    buildToolbars();
    reloadSettings();
    setView(m_view);
    setFoldersShown(m_foldersShown);
    navigate(location, false);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &FileBrowser::updateActions);
    connect(ShellTheme::instance(), &ShellTheme::changed, this, [this] { updatePane(); });
}

void FileBrowser::closeEvent(QCloseEvent *event)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("view"), int(m_view));
    settings.setValue(QStringLiteral("folders"), m_foldersShown);
    settings.setValue(QStringLiteral("status"), m_statusAction->isChecked());
    QMainWindow::closeEvent(event);
}

void FileBrowser::reloadSettings()
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    QDir::Filters filter = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (settings.value(QStringLiteral("showHidden"), false).toBool())
        filter |= QDir::Hidden;
    m_files->setFilter(filter);
    m_singleClick = settings.value(QStringLiteral("singleClick"), false).toBool();
    m_noTasks = settings.value(QStringLiteral("classicFolders"), false).toBool();
    if (m_left)
        setFoldersShown(m_foldersShown);
}

void FileBrowser::buildPanes()
{
    m_splitter = new QSplitter(this);
    m_splitter->setChildrenCollapsible(false);
    m_left = new QStackedWidget(m_splitter);

    // The Common Tasks pane
    m_paneArea = new QScrollArea(m_left);
    m_paneArea->setFrameShape(QFrame::NoFrame);
    m_paneArea->setWidgetResizable(true);
    m_paneArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pane = new TaskPane;
    m_paneArea->setWidget(m_pane);
    m_left->addWidget(m_paneArea);

    // A bar's small title, with what closes it
    const auto barPage = [this](const QString &text, std::function<void()> close) {
        auto *page = new QWidget(m_left);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *title = new QWidget(page);
        auto *titleLayout = new QHBoxLayout(title);
        titleLayout->setContentsMargins(6, 2, 2, 2);
        titleLayout->addWidget(new QLabel(text, title), 1);
        auto *closeButton = new QToolButton(title);
        closeButton->setAutoRaise(true);
        closeButton->setText(QStringLiteral("×"));
        connect(closeButton, &QToolButton::clicked, this, close);
        titleLayout->addWidget(closeButton);
        layout->addWidget(title);
        return page;
    };

    // The Folders tree: the Desktop, and under it what XP has there
    auto *foldersPage = barPage(tr("Folders"), [this] { setFoldersShown(false); });
    m_tree = new QTreeWidget(foldersPage);
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setRootIsDecorated(true);
    const auto addPlace = [this](QTreeWidgetItem *parent, const QString &text, const QIcon &icon, const QString &location) {
        auto *item = new QTreeWidgetItem(QStringList(text));
        item->setIcon(0, icon);
        item->setData(0, Qt::UserRole, location);
        if (parent)
            parent->addChild(item);
        else
            m_tree->addTopLevelItem(item);
        // A folder shows it can open until it is read.
        if (!location.startsWith(QLatin1String("::")))
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        return item;
    };
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    auto *desktopItem = addPlace(nullptr, xpfiles::shellString(9208, "Desktop"), fileicons::shell(35), desktop);
    addPlace(desktopItem, places::title(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)), fileicons::shell(fileicons::Documents), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    auto *computer = addPlace(desktopItem, places::title(places::Computer), places::icon(places::Computer), places::Computer);
    addPlace(computer, QStringLiteral("/"), fileicons::shell(fileicons::Drive), QStringLiteral("/"));
    addPlace(computer, places::title(places::ControlPanel), places::icon(places::ControlPanel), places::ControlPanel);
    addPlace(desktopItem, places::title(places::RecycleBin), places::icon(places::RecycleBin), places::RecycleBin);
    desktopItem->setExpanded(true);
    computer->setExpanded(true);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &FileBrowser::fillTreeItem);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { navigate(item->data(0, Qt::UserRole).toString()); });
    foldersPage->layout()->addWidget(m_tree);
    m_left->addWidget(foldersPage);

    // The Search Companion
    auto *searchPage = barPage(xpfiles::resources(xpfiles::find(QStringLiteral("srchui.dll"))) ? xpfiles::string(*xpfiles::resources(xpfiles::find(QStringLiteral("srchui.dll"))), 5013) : tr("Search Companion"), [this] { showSearchBar(false); });
    m_searchCompanion = new SearchCompanion(searchPage);
    connect(m_searchCompanion, &SearchCompanion::searchFiles, this, &FileBrowser::startSearch);
    connect(m_searchCompanion, &SearchCompanion::stopSearch, this, &FileBrowser::finishSearch);
    connect(m_searchCompanion, &SearchCompanion::searchWeb, this, &FileBrowser::searchTheWeb);
#ifdef XP_WEB_PAGES
    connect(m_searchCompanion, &SearchCompanion::highlightWeb, this, [this](const QString &text) {
        if (m_web)
            m_web->findText(text);
    });
#endif
    static_cast<QVBoxLayout *>(searchPage->layout())->addWidget(m_searchCompanion, 1);
    m_left->addWidget(searchPage);

    // The folder itself
    m_pages = new QStackedWidget(m_splitter);
    const auto listView = [this](QAbstractItemModel *model) {
        auto *view = new ExplorerListView(m_pages);
        view->setModel(model);
        view->setItemDelegate(new ItemDelegate(view, view));
        view->setResizeMode(QListView::Adjust);
        view->setUniformItemSizes(false);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setSelectionRectVisible(true);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->setFrameShape(QFrame::NoFrame);
        view->setWordWrap(true);
        view->setEditTriggers(QAbstractItemView::EditKeyPressed);
        view->viewport()->installEventFilter(this);
        view->installEventFilter(this);
        connect(view, &QAbstractItemView::customContextMenuRequested, this, [this, view](QPoint at) {
            showViewContextMenu(view, at);
        });
        connect(view, &QAbstractItemView::activated, this, &FileBrowser::activate);
        // Thumbnails come from their thread one by one.
        connect(pictures::Thumbnails::instance(), &pictures::Thumbnails::ready, view->viewport(), qOverload<>(&QWidget::update));
        return view;
    };
    m_list = listView(m_model);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_pages->addWidget(m_list);
    m_details = new ExplorerDetailsView(m_pages);
    m_details->setModel(m_model);
    m_details->setSelectionModel(m_list->selectionModel());
    m_detailsDelegate = new DetailsDelegate(m_details);
    m_details->setItemDelegate(m_detailsDelegate);
    m_details->setIconSize(QSize(16, 16));
    m_details->setUniformRowHeights(true);
    m_details->setRootIsDecorated(false);
    m_details->setItemsExpandable(false);
    m_details->setSortingEnabled(true);
    m_details->sortByColumn(0, Qt::AscendingOrder);
    m_details->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_details->setContextMenuPolicy(Qt::CustomContextMenu);
    m_details->setFrameShape(QFrame::NoFrame);
    m_details->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_details->setDragEnabled(true);
    m_details->setAcceptDrops(true);
    m_details->setDragDropMode(QAbstractItemView::DragDrop);
    m_details->viewport()->installEventFilter(this);
    m_details->installEventFilter(this);
    m_details->header()->setSectionsMovable(false);
    // 20 with Tahoma 8, as XP's Details header (its art is 18)
    m_details->header()->setFixedHeight(m_details->fontMetrics().height() + 7);
    setColumnChars(m_details, {30, 16, 20, 20});
    {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        const QStringList hidden = settings.value(QStringLiteral("folder/hiddenColumns")).toStringList();
        for (int column = 1; column < 4; ++column)
            m_details->setColumnHidden(column, hidden.contains(QString::number(column)));
    }
    connect(m_details, &QAbstractItemView::customContextMenuRequested, this, [this](QPoint at) {
        showViewContextMenu(m_details, at);
    });
    connect(m_details, &QAbstractItemView::activated, this, &FileBrowser::activate);
    connect(m_details->header(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder) {
        m_groupColumn = column;
        if (m_groups)
            applyGroups();
    });
    m_pages->addWidget(m_details);
    // Filmstrip: the large picture, the viewer's four buttons under it, and the folder's
    // thumbnails on one row, the list itself moved here while this view is shown
    m_film = new QWidget(m_pages);
    m_film->setAutoFillBackground(true);
    m_film->setBackgroundRole(QPalette::Base);
    auto *filmLayout = new QVBoxLayout(m_film);
    filmLayout->setContentsMargins(0, 0, 0, 0);
    filmLayout->setSpacing(0);
    m_filmPreview = new FilmPreview([this] {
        const QStringList paths = selectedPaths();
        if (!paths.isEmpty())
            openPath(paths.first());
    }, m_film);
    filmLayout->addWidget(m_filmPreview, 1);
    auto *filmButtons = new QHBoxLayout;
    filmButtons->setContentsMargins(0, 4, 0, 4);
    filmButtons->setSpacing(2);
    filmButtons->addStretch();
    const struct { int icon, tip; const char *fallback; QKeySequence key; std::function<void()> action; } filmTools[] = {
        {pictures::PreviousImage, 417, "Previous Image (Left Arrow)", {}, [this] { stepFilmstrip(-1); }},
        {pictures::NextImage, 416, "Next Image (Right Arrow)", {}, [this] { stepFilmstrip(1); }},
        {-1, 0, nullptr, {}, {}},
        {pictures::RotateClockwise, 500, "Rotate Clockwise (Ctrl+K)", QKeySequence(Qt::CTRL | Qt::Key_K), [this] { rotateSelected(true); }},
        {pictures::RotateCounterclockwise, 501, "Rotate Counterclockwise (Ctrl+L)", QKeySequence(Qt::CTRL | Qt::Key_L), [this] { rotateSelected(false); }},
    };
    for (const auto &tool : filmTools) {
        if (tool.icon < 0) {
            filmButtons->addSpacing(12);
            continue;
        }
        auto *button = new QToolButton(m_film);
        button->setAutoRaise(true);
        button->setIcon(pictures::viewerIcon(tool.icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(pictures::viewerString(tool.tip, tool.fallback));
        button->setShortcut(tool.key);
        connect(button, &QToolButton::clicked, this, tool.action);
        filmButtons->addWidget(button);
    }
    filmButtons->addStretch();
    filmLayout->addLayout(filmButtons);
    m_pages->addWidget(m_film);
    // Filmstrip starts on the folder's first picture once the folder is read, as XP's does.
    connect(m_files, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
        if (path == currentFolder())
            selectFirstPicture();
    });
    m_virtualFilter = new VirtualFilter(this);
    m_virtualFilter->setSourceModel(m_virtual);
    m_virtualList = listView(m_virtualFilter);
    m_virtualList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_virtualList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pages->addWidget(m_virtualList);
    m_virtualDetails = new ExplorerDetailsView(m_pages);
    m_virtualDetails->setModel(m_virtualFilter);
    m_virtualDetails->setSelectionModel(m_virtualList->selectionModel());
    m_virtualDetails->setItemDelegate(new DetailsDelegate(m_virtualDetails));
    m_virtualDetails->setRootIsDecorated(false);
    m_virtualDetails->setItemsExpandable(false);
    m_virtualDetails->setUniformRowHeights(true);
    m_virtualDetails->setIconSize(QSize(16, 16));
    m_virtualDetails->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_virtualDetails->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_virtualDetails->setFrameShape(QFrame::NoFrame);
    m_virtualDetails->viewport()->installEventFilter(this);
    m_virtualDetails->installEventFilter(this);
    m_virtualDetails->setContextMenuPolicy(Qt::CustomContextMenu);
    m_virtualDetails->header()->setFixedHeight(m_virtualDetails->fontMetrics().height() + 7);
    setColumnChars(m_virtualDetails, {20, 20});
    m_virtualDetails->setSortingEnabled(true);
    m_virtualDetails->sortByColumn(0, Qt::AscendingOrder);
    connect(m_virtualDetails, &QAbstractItemView::activated, this, &FileBrowser::activate);
    connect(m_virtualDetails, &QAbstractItemView::customContextMenuRequested, this, [this](QPoint at) { showViewContextMenu(m_virtualDetails, at); });
    applyGroups();
    m_pages->addWidget(m_virtualDetails);
    m_categoryPage = new ControlPanelCategories([this](int category) { navigate(places::ControlPanel + QLatin1Char('/') + QString::number(category)); }, this);
    m_categoryArea = new QScrollArea(m_pages);
    m_categoryArea->setFrameShape(QFrame::NoFrame);
    m_categoryArea->setWidgetResizable(true);
    m_categoryArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryArea->setWidget(m_categoryPage);
    m_pages->addWidget(m_categoryArea);
    m_warning = new SystemFolderWarning([this] { setSystemFolderShown(true); }, m_pages);
    m_pages->addWidget(m_warning);
    m_resultsModel = new QStandardItemModel(this);
    m_results = new SearchResultsView(m_pages);
    m_results->setModel(m_resultsModel);
    m_results->setRootIsDecorated(false);
    m_results->setItemsExpandable(false);
    m_results->setSortingEnabled(true);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_results, &QAbstractItemView::customContextMenuRequested, this, [this](QPoint at) {
        showViewContextMenu(m_results, at);
    });
    connect(m_results, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        openPath(m_resultsModel->index(index.row(), 0).data(Qt::UserRole).toString());
    });
    m_pages->addWidget(m_results);
    connect(m_results->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateActions();
        updateStatus();
    });
    clearResults(m_resultsModel);
    setColumnChars(m_results, {30, 30, 16, 20, 20});

    for (QItemSelectionModel *selection : {m_list->selectionModel(), m_virtualList->selectionModel()})
        connect(selection, &QItemSelectionModel::selectionChanged, this, [this] {
            if (shownView() == Filmstrip) {
                const QStringList paths = selectedPaths();
                m_filmPreview->setPath(paths.size() == 1 ? paths.first() : QString());
            }
            updatePane();
            updateActions();
            updateStatus();
        });

    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_pages);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    // UIFILE 3's 210 includes the white line down its left edge.
    m_splitter->setSizes({ShellTheme::instance()->scaled(210) - (ShellTheme::instance()->renderer() ? 1 : 0), width()});
    setCentralWidget(m_splitter);
    // XP's folder windows have a white line down their left edge and along their bottom, the
    // pane and the folder inside them.
    const auto edgeLine = [this] {
        auto *line = new QWidget(this);
        line->setAttribute(Qt::WA_TransparentForMouseEvents);
        line->setAutoFillBackground(true);
        QPalette colour = line->palette();
        colour.setColor(QPalette::Window, Qt::white);
        line->setPalette(colour);
        return line;
    };
    m_bottomLine = edgeLine();
    m_leftLine = edgeLine();
    m_splitter->installEventFilter(this);
    connect(ShellTheme::instance(), &ShellTheme::changed, this, &FileBrowser::placeEdgeLines);
    placeEdgeLines();
    connect(m_files, &QFileSystemModel::directoryLoaded, this, [this] {
        if (m_view == List) configureList(m_list, List);
        updateStatus();
    });
    connect(m_files, &QFileSystemModel::fileRenamed, this, [this](const QString &path, const QString &oldName, const QString &newName) {
        if (m_undoing)
            return;
        const QString before = QDir(path).filePath(oldName), after = QDir(path).filePath(newName);
        m_undo = [before, after] { QFile::rename(after, before); };
        m_undoAction->setText(tr("&Undo Rename"));
        m_undoAction->setEnabled(true);
    });
}

void FileBrowser::buildMenus()
{
    setMenuBar(new BrandMenuBar(this));
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QMenu *newMenu = file->addMenu(tr("&New"));
    m_newFolderAction = newMenu->addAction(tr("&Folder"), this, &FileBrowser::newFolder);
    newMenu->addAction(tr("&Shortcut"), this, &FileBrowser::newShortcut);
    newMenu->addSeparator();
    m_newTextAction = newMenu->addAction(tr("&Text Document"), this, &FileBrowser::newTextDocument);
    newMenu->addAction(tr("&Bitmap Image"), this, &FileBrowser::newBitmapImage);
    newMenu->addAction(tr("&WordPad Document"), this, &FileBrowser::newWordPadDocument);
    newMenu->addAction(tr("&Wave Sound"), this, &FileBrowser::newWaveSound);
    newMenu->addSeparator();
    newMenu->addAction(tr("&Briefcase"), this, &FileBrowser::newBriefcase);
    newMenu->addAction(tr("Compressed (&zipped) Folder"), this, &FileBrowser::newCompressedFolder);
    file->addSeparator();
    file->addAction(tr("Create &Shortcut"), this, [this] {
        for (const QString &path : selectedPaths()) {
            const QFileInfo info(path);
            fileops::createShortcut(path, info.absolutePath() + QLatin1Char('/') + tr("Shortcut to %1.lnk").arg(info.fileName()));
        }
    });
    m_deleteAction = file->addAction(tr("&Delete"), this, [this] { deleteSelected(false); });
    m_renameAction = file->addAction(tr("Rena&me"), this, &FileBrowser::renameSelected);
    m_propertiesAction = file->addAction(tr("P&roperties"), this, &FileBrowser::showProperties);
    file->addSeparator();
    file->addAction(tr("&Close"), this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = edit->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] {
        const std::function<void()> undo = m_undo;
        m_undo = {};
        m_undoAction->setText(tr("&Undo"));
        m_undoAction->setEnabled(false);
        m_undoing = true;
        if (undo)
            undo();
        m_undoing = false;
    });
    m_undoAction->setEnabled(false);
    edit->addSeparator();
    m_cutAction = edit->addAction(tr("Cu&t"), QKeySequence::Cut, this, [this] { fileops::copyToClipboard(selectedPaths(), true); });
    m_copyAction = edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] { fileops::copyToClipboard(selectedPaths(), false); });
    m_pasteAction = edit->addAction(tr("&Paste"), QKeySequence::Paste, this, [this] { fileops::pasteInto(currentFolder(), this); });
    edit->addSeparator();
    m_copyToAction = edit->addAction(tr("Cop&y To Folder..."), this, [this] { moveOrCopyTo(false); });
    m_moveAction = edit->addAction(tr("Mo&ve To Folder..."), this, [this] { moveOrCopyTo(true); });
    edit->addSeparator();
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] { currentView()->selectAll(); });
    edit->addAction(tr("&Invert Selection"), this, [this] {
        QAbstractItemView *view = currentView();
        QItemSelection all;
        const QModelIndex root = view->rootIndex();
        const int rows = view->model()->rowCount(root), columns = view->model()->columnCount(root);
        if (rows && columns)
            all.select(view->model()->index(0, 0, root), view->model()->index(rows - 1, columns - 1, root));
        view->selectionModel()->select(all, QItemSelectionModel::Toggle);
    });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_toolbarsMenu = view->addMenu(tr("&Toolbars"));
    m_statusAction = view->addAction(tr("Status &Bar"));
    m_statusAction->setCheckable(true);
    connect(m_statusAction, &QAction::toggled, statusBar(), &QWidget::setVisible);
    m_foldersAction = new QAction(stripIcon(FOLDERS), tr("Folders"), this);
    m_foldersAction->setCheckable(true);
    connect(m_foldersAction, &QAction::triggered, this, &FileBrowser::setFoldersShown);
    m_searchAction = new QAction(stripIcon(SEARCH), tr("Search"), this);
    m_searchAction->setCheckable(true);
    m_searchAction->setShortcut(Qt::CTRL | Qt::Key_E);
    connect(m_searchAction, &QAction::triggered, this, &FileBrowser::showSearchBar);
    QMenu *bar = view->addMenu(tr("E&xplorer Bar"));
    bar->addAction(m_searchAction);
    bar->addAction(m_foldersAction);
    view->addSeparator();
    m_viewGroup = new QActionGroup(this);
    m_viewsMenu = new QMenu(this);
    const struct { const char *name; View mode; } modes[] = {
        {QT_TR_NOOP("Filmstri&p"), Filmstrip}, {QT_TR_NOOP("T&humbnails"), Thumbnails}, {QT_TR_NOOP("Tile&s"), Tiles}, {QT_TR_NOOP("Ico&ns"), Icons}, {QT_TR_NOOP("&List"), List}, {QT_TR_NOOP("&Details"), Details}};
    for (const auto &mode : modes) {
        QAction *action = m_viewGroup->addAction(tr(mode.name));
        action->setCheckable(true);
        action->setData(int(mode.mode));
        view->addAction(action);
        m_viewsMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, target = mode.mode] { setView(target); });
        if (mode.mode == Filmstrip)
            m_filmstripAction = action;
    }
    // Only picture folders have it.
    m_filmstripAction->setVisible(false);
    QAction *detailsAction = view->addAction(tr("Choose &Details..."), this, [this] {
        if (!shellmenus::shell32())
            return;
        QDialog dialog(this);
        const xpdialog::Page page = xpdialog::build(*shellmenus::shell32(), 1096, &dialog);
        dialog.setWindowTitle(page.caption);
        auto *list = new QListWidget(&dialog);
        if (QWidget *place = page.controls.value(1001)) {
            list->setGeometry(place->geometry());
            place->hide();
        }
        const QList<QPair<QString, int>> columns = {{tr("Name"), 0}, {tr("Size"), 1}, {tr("Type"), 2}, {tr("Date Modified"), 3}};
        for (const auto &[name, column] : columns) {
            auto *item = new QListWidgetItem(name, list);
            item->setData(Qt::UserRole, column);
            if (column == 0) {
                item->setCheckState(Qt::Checked);
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            } else {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(m_details->isColumnHidden(column) ? Qt::Unchecked : Qt::Checked);
            }
        }
        list->setCurrentRow(0);
        auto *widthEdit = qobject_cast<QLineEdit *>(page.controls.value(1005));
        if (widthEdit)
            widthEdit->setText(QString::number(m_details->columnWidth(list->currentRow())));
        QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&, list, widthEdit] {
            if (widthEdit && list->currentItem())
                widthEdit->setText(QString::number(m_details->columnWidth(list->currentItem()->data(Qt::UserRole).toInt())));
        });
        if (auto *show = qobject_cast<QPushButton *>(page.controls.value(1006)))
            QObject::connect(show, &QPushButton::clicked, &dialog, [list] { if (list->currentItem()) list->currentItem()->setCheckState(Qt::Checked); });
        if (auto *hide = qobject_cast<QPushButton *>(page.controls.value(1007)))
            QObject::connect(hide, &QPushButton::clicked, &dialog, [list] {
                if (list->currentItem() && list->currentItem()->data(Qt::UserRole).toInt() != 0)
                    list->currentItem()->setCheckState(Qt::Unchecked);
            });
        for (int id : {1003, 1004})
            if (QWidget *button = page.controls.value(id))
                button->setEnabled(false);
        if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1)))
            QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
                QStringList hidden, widths;
                for (int i = 0; i < list->count(); ++i) {
                    const QListWidgetItem *item = list->item(i);
                    const int column = item->data(Qt::UserRole).toInt();
                    if (column != 0 && item->checkState() != Qt::Checked)
                        hidden.append(QString::number(column));
                    widths.append(QString::number(m_details->columnWidth(column)));
                    if (column == list->currentRow() && widthEdit)
                        m_details->setColumnWidth(column, widthEdit->text().toInt());
                }
                QSettings write(settingsPath(), QSettings::IniFormat);
                write.setValue(QStringLiteral("folder/hiddenColumns"), hidden);
                write.setValue(QStringLiteral("folder/columnWidths"), widths);
                dialog.accept();
            });
        if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
            QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        const QStringList hidden = settings.value(QStringLiteral("folder/hiddenColumns")).toStringList();
        for (int column = 1; column < 4; ++column)
            m_details->setColumnHidden(column, hidden.contains(QString::number(column)));
    });
    detailsAction->setEnabled(true);
    QAction *groupsAction = view->addAction(tr("Show in &Groups"));
    groupsAction->setCheckable(true);
    groupsAction->setChecked(m_groups);
    connect(groupsAction, &QAction::triggered, this, [this, groupsAction](bool on) {
        m_groups = on;
        QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("folder/groups"), on);
        applyGroups();
        groupsAction->setChecked(on);
    });
    view->addSeparator();
    QMenu *arrange = view->addMenu(tr("Arrange Icons &by"));
    const struct { int string; const char *fallback; int column; } orders[] = {{8976, "Name", 0}, {8978, "Size", 1}, {8979, "Type", 2}, {8980, "Date Modified", 3}};
    for (const auto &order : orders)
        arrange->addAction(xpfiles::shellString(order.string, order.fallback), this, [this, column = order.column] { m_details->sortByColumn(column, Qt::AscendingOrder); });
    view->addSeparator();
    QMenu *go = view->addMenu(tr("&Go To"));
    go->addAction(tr("&Back"), this, [this] { if (m_backButton->isEnabled()) m_backButton->click(); });
    go->addAction(tr("&Forward"), this, [this] { if (m_forwardButton->isEnabled()) m_forwardButton->click(); });
    go->addAction(tr("&Up One Level"), this, [this] { m_upAction->trigger(); });
    view->addAction(tr("&Refresh"), QKeySequence(Qt::Key_F5), this, [this] { navigate(m_location, false); });
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_statusAction->setChecked(settings.value(QStringLiteral("status"), false).toBool());
    statusBar()->setVisible(m_statusAction->isChecked());

    menuBar()->addMenu(buildFavoritesMenu());

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    // Linux mounts its network drives elsewhere: XP's entries, greyed as XP greys them when it has none.
    tools->addAction(tr("Map &Network Drive..."))->setEnabled(false);
    tools->addAction(tr("&Disconnect Network Drive..."))->setEnabled(false);
    tools->addAction(tr("&Synchronize..."))->setEnabled(false);
    tools->addSeparator();
    tools->addAction(tr("Folder &Options..."), this, [] { controlpanel::open(QStringLiteral("folders")); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    const auto helpProgram = [] {
        for (const char *name : {"khelpcenter", "yelp"})
            if (const QString found = QStandardPaths::findExecutable(QLatin1String(name)); !found.isEmpty())
                return found;
        return QString();
    }();
    QAction *helpAction = help->addAction(tr("Help and Support &Center"), this, [helpProgram] { QProcess::startDetached(helpProgram, {}); });
    helpAction->setEnabled(!helpProgram.isEmpty());
    help->addSeparator();
    help->addAction(tr("&About Windows"), this, [] { applets::open(QStringLiteral("winver")); });
}

QMenu *FileBrowser::buildFavoritesMenu()
{
    // XP's Favorites folder: links to places, in a folder of the user's.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Favorites");
    auto *menu = new QMenu(xpfiles::shellString(9106, "Favorites").prepend(QLatin1Char('&')).replace(QStringLiteral("&Favorites"), tr("F&avorites")), this);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, folder] {
        menu->clear();
        menu->addAction(tr("&Add to Favorites..."), this, [this, folder] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Add Favorite"), tr("&Name:"), QLineEdit::Normal, places::title(m_location), &ok);
            if (!ok || name.isEmpty())
                return;
            QDir().mkpath(folder);
            if (m_location.startsWith(QLatin1String("::")) || places::isWeb(m_location)) {
                QFile link(folder + QLatin1Char('/') + name + QStringLiteral(".place"));
                if (link.open(QIODevice::WriteOnly))
                    link.write(m_location.toUtf8());
            } else {
                fileops::createShortcut(m_location, folder + QLatin1Char('/') + name + QStringLiteral(".lnk"));
            }
        });
        menu->addAction(tr("&Organize Favorites..."), this, [folder] {
            QDir().mkpath(folder);
            FileBrowser::open(folder);
        });
        const QFileInfoList entries = QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
        if (!entries.isEmpty())
            menu->addSeparator();
        for (const QFileInfo &entry : entries) {
            QString target = entry.symLinkTarget();
            if (entry.suffix() == QLatin1String("place")) {
                QFile link(entry.filePath());
                if (link.open(QIODevice::ReadOnly))
                    target = QString::fromUtf8(link.readAll());
            }
            if (!target.isEmpty())
                menu->addAction(places::icon(target), entry.completeBaseName().isEmpty() ? entry.fileName() : entry.suffix() == QLatin1String("place") ? entry.completeBaseName() : entry.fileName(), this, [this, target] { navigate(target); });
        }
    });
    // A menu's empty until it opens, and the bar only opens it if it has something.
    menu->addAction(tr("&Add to Favorites..."));
    return menu;
}

void FileBrowser::buildToolbars()
{
    // XP's buttons are the band's 38 pixels tall, the first 2 from its left
    const int BUTTON_HEIGHT = 38;
    auto *toolbar = new RebarToolBar(tr("Standard Buttons"), this, true);
    addToolBar(toolbar);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    auto *lead = new QWidget(toolbar);
    lead->setFixedSize(2, BUTTON_HEIGHT);
    toolbar->addWidget(lead);
    const auto historyButton = [&](int icon, const QString &text, bool back) {
        auto *button = new BandButton(toolbar, BUTTON_HEIGHT, 24, BandButton::Split);
        button->setIcon(stripIcon(icon));
        button->setText(text);
        button->setToolTip(text);
        button->setToolButtonStyle(back ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        addHistoryMenu(button, back);
        connect(button, &QToolButton::clicked, this, [this, back] {
            QStringList &from = back ? m_back : m_forward;
            QStringList &to = back ? m_forward : m_back;
            if (from.isEmpty())
                return;
            to.append(m_location);
            navigate(from.takeLast(), false);
        });
        toolbar->addWidget(button);
        return button;
    };
    m_backButton = historyButton(BACK, tr("Back"), true);
    m_forwardButton = historyButton(FORWARD, tr("Forward"), false);
    const auto actionButton = [&](QAction *action, bool words) {
        auto *button = new BandButton(toolbar, BUTTON_HEIGHT, 24);
        button->setToolButtonStyle(words ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        button->setDefaultAction(action);
        toolbar->addWidget(button);
    };
    m_upAction = new QAction(stripIcon(UP), tr("Up"), this);
    connect(m_upAction, &QAction::triggered, this, [this] {
        if (m_location.startsWith(places::ControlPanel + QLatin1Char('/'))) {
            navigate(places::ControlPanel);
            return;
        }
        // XP's tree: the Desktop holds My Computer, My Documents and the Recycle Bin.
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (m_location == places::Computer || m_location == places::RecycleBin || m_location == QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)) {
            navigate(desktop);
            return;
        }
        if (m_location == places::ControlPanel) {
            navigate(places::Computer);
            return;
        }
        if (m_location.startsWith(QLatin1String("::")) || places::isWeb(m_location))
            return;
        const QFileInfo info(m_location);
        navigate(info.isRoot() ? places::Computer : info.absolutePath());
    });
    actionButton(m_upAction, false);
    toolbar->addWidget(new BandSeparator(toolbar, BUTTON_HEIGHT));
    actionButton(m_searchAction, true);
    actionButton(m_foldersAction, true);
    toolbar->addWidget(new BandSeparator(toolbar, BUTTON_HEIGHT));
    auto *views = new BandButton(toolbar, BUTTON_HEIGHT, 24, BandButton::WholeDropDown);
    views->setIcon(stripIcon(VIEWS));
    views->setToolTip(tr("Views"));
    views->setMenu(m_viewsMenu);
    toolbar->addWidget(views);

    addToolBarBreak();
    auto *addressBar = new RebarToolBar(tr("Address Bar"), this, false);
    addToolBar(addressBar);
    addressBar->setMovable(false);
    toolbar->toggleViewAction()->setText(tr("&Standard Buttons"));
    addressBar->toggleViewAction()->setText(tr("&Address Bar"));
    m_toolbarsMenu->addAction(toolbar->toggleViewAction());
    m_toolbarsMenu->addAction(addressBar->toggleViewAction());
    // One widget for the whole row: a toolbar folds away a stretching child of its own.
    // On XP the row is its combo box's 22 pixels; Go is 2 after it and 7 from the edge.
    const int ROW_HEIGHT = 22;
    auto *row = new QWidget(addressBar);
    row->setFixedHeight(ROW_HEIGHT);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 0, 7, 0);
    rowLayout->setSpacing(4);
    auto *addressLabel = new QLabel(tr("Address"), row);
    addressLabel->setForegroundRole(QPalette::PlaceholderText);
    // The style's Rebar TextColor on its band (127 124 115 in Luna), the buttons' text under Windows Classic (as XP's)
    addressLabel->setPalette([&] {
        QPalette palette = addressLabel->palette();
        ShellTheme *theme = ShellTheme::instance();
        const w4w::Renderer *renderer = theme->renderer();
        palette.setColor(QPalette::PlaceholderText, renderer ? renderer->colour(w4w::Part{QStringLiteral("rebar"), {}, {}, 0, {}}, QStringLiteral("textcolor")).value_or(QColor(127, 124, 115))
                                                             : theme->system(QStringLiteral("btntext"), Qt::black));
        return palette;
    }());
    rowLayout->addWidget(addressLabel);
    m_address = new QComboBox(row);
    m_address->setEditable(true);
    m_address->setInsertPolicy(QComboBox::NoInsert);
    m_address->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_address->setMinimumContentsLength(10);
    m_address->setFixedHeight(ROW_HEIGHT);
    rowLayout->addWidget(m_address, 1);
    rowLayout->addSpacing(2 - rowLayout->spacing());
    auto *goButton = new BandButton(row, ROW_HEIGHT, 20);
    goButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rowLayout->addWidget(goButton);
    addressBar->addWidget(row);
    // XP's Go is shell32.dll's 20-pixel bitmap, not its 16-pixel icon
    QIcon goIcon = fileicons::shell(fileicons::Go);
    if (const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")))) {
        const QImage normal = xpfiles::bitmap(*shell32, BITMAP_GO), hot = xpfiles::bitmap(*shell32, BITMAP_GO_HOT);
        if (!normal.isNull()) {
            goIcon = QIcon(QPixmap::fromImage(normal));
            if (!hot.isNull())
                goIcon.addPixmap(QPixmap::fromImage(hot), QIcon::Active);
        }
    }
    auto *go = new QAction(goIcon, tr("Go"), this);
    goButton->setDefaultAction(go);
    connect(go, &QAction::triggered, this, [this] {
        const QString text = m_address->currentText().trimmed();
        for (const QString &place : {places::Computer, places::RecycleBin, places::ControlPanel})
            if (text.compare(places::title(place), Qt::CaseInsensitive) == 0) {
                navigate(place);
                return;
            }
        if (places::isWeb(text)) {
            navigate(text);
            return;
        }
        const QString expanded = text.startsWith(QLatin1Char('~')) ? QDir::homePath() + text.mid(1) : text;
        if (QFileInfo(expanded).isDir())
            navigate(expanded);
        else if (QFileInfo::exists(expanded) || expanded.contains(QLatin1String("://")))
            programs::open(expanded);
        else
            QMessageBox::warning(this, windowTitle(), tr("Cannot find '%1'. Make sure the path or Internet address is correct.").arg(text));
    });
    connect(m_address->lineEdit(), &QLineEdit::returnPressed, go, &QAction::trigger);
}

void FileBrowser::addHistoryMenu(QToolButton *button, bool back)
{
    auto *menu = new QMenu(button);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, back] {
        menu->clear();
        const QStringList &list = back ? m_back : m_forward;
        for (int i = int(list.size()) - 1; i >= 0; --i) {
            menu->addAction(places::icon(list[i]), places::title(list[i]), this, [this, i, back] {
                QStringList &from = back ? m_back : m_forward;
                QStringList &to = back ? m_forward : m_back;
                to.append(m_location);
                while (from.size() > i + 1)
                    to.append(from.takeLast());
                navigate(from.takeLast(), false);
            });
        }
    });
    button->setMenu(menu);
}

FileBrowser::View FileBrowser::shownView() const
{
    return m_view == Filmstrip && !m_pictureFolder ? Thumbnails : m_view;
}

QWidget *FileBrowser::folderPage() const
{
    switch (shownView()) {
    case Details: return m_details;
    case Filmstrip: return m_film;
    default: return m_list;
    }
}

void FileBrowser::placeList(bool inFilmstrip)
{
    const bool there = m_list->parentWidget() == m_film;
    if (there == inFilmstrip)
        return;
    if (inFilmstrip) {
        m_pages->removeWidget(m_list);
        static_cast<QVBoxLayout *>(m_film->layout())->addWidget(m_list);
        m_list->show();
    } else {
        m_film->layout()->removeWidget(m_list);
        m_pages->insertWidget(0, m_list);
        m_list->setMinimumHeight(0);
        m_list->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void FileBrowser::selectFirstPicture()
{
    if (shownView() != Filmstrip || m_list->selectionModel()->hasSelection())
        return;
    const QModelIndex root = m_list->rootIndex();
    for (int row = 0; row < m_model->rowCount(root); ++row) {
        const QModelIndex index = m_model->index(row, 0, root);
        if (pictures::isPicture(m_model->path(index))) {
            m_list->setCurrentIndex(index);
            m_list->scrollTo(index);
            return;
        }
    }
}

void FileBrowser::stepFilmstrip(int step)
{
    const QModelIndex root = m_list->rootIndex();
    const int rows = m_model->rowCount(root);
    if (rows == 0)
        return;
    const QModelIndex current = m_list->currentIndex();
    const int row = current.isValid() ? (current.row() + step + rows) % rows : 0;
    const QModelIndex next = m_model->index(row, 0, root);
    m_list->setCurrentIndex(next);
    m_list->scrollTo(next);
}

QStringList FileBrowser::selectedPictures() const
{
    QStringList found;
    for (const QString &path : selectedPaths())
        if (pictures::isPicture(path))
            found.append(path);
    return found;
}

void FileBrowser::rotateSelected(bool clockwise)
{
    for (const QString &path : selectedPictures()) {
        if (!pictures::rotate(path, clockwise, this))
            break;
        pictures::Thumbnails::instance()->forget(path);
    }
    m_filmPreview->refresh();
    m_list->viewport()->update();
    updatePane();
}

void FileBrowser::startSlideShow()
{
    // The selected pictures if there are several, the folder's otherwise, from the selected one
    QStringList chosen = selectedPictures();
    if (chosen.size() > 1) {
        pictures::SlideShow::start(chosen);
        return;
    }
    const QStringList all = pictures::picturesIn(currentFolder());
    pictures::SlideShow::start(all, chosen.isEmpty() ? 0 : int(all.indexOf(chosen.first())));
}

void FileBrowser::setAsBackground(const QString &path)
{
    LookChoice choice = ShellTheme::instance()->choice();
    choice.wallpaper = path;
    ShellTheme::write(choice);
}

static int listColumnWidth(QListView *list)
{
    int width = ShellTheme::instance()->scaled(64);
    const QFontMetrics metrics(list->font());
    for (int row = 0; row < list->model()->rowCount(list->rootIndex()); ++row)
        width = qMax(width, metrics.horizontalAdvance(list->model()->index(row, 0, list->rootIndex()).data().toString()) + ShellTheme::instance()->scaled(24));
    return qMin(width, list->screen()->geometry().width());
}

void FileBrowser::configureList(QListView *list, View shown)
{
    auto *delegate = delegateOf(list);
    list->setFlow(QListView::LeftToRight);
    list->setMovement(m_autoArrange ? QListView::Static : QListView::Free);
    list->setWrapping(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    switch (shown) {
    case Filmstrip:
        // The thumbnails on one row, which scrolls sideways
        delegate->setMode(ItemDelegate::ThumbnailMode);
        list->setViewMode(QListView::IconMode);
        list->setGridSize(QSize(120, 134));
        list->setIconSize(QSize(96, 96));
        list->setWrapping(false);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setFixedHeight(134 + 4 + list->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, list));
        break;
    case Thumbnails:
        delegate->setMode(ItemDelegate::ThumbnailMode);
        list->setViewMode(QListView::IconMode);
        list->setGridSize(QSize(120, 134));
        list->setIconSize(QSize(96, 96));
        break;
    case Tiles:
        delegate->setMode(ItemDelegate::TilesMode);
        list->setViewMode(QListView::IconMode);
        list->setGridSize(QSize(218, 56));
        list->setIconSize(QSize(48, 48));
        break;
    case Icons:
        delegate->setMode(ItemDelegate::IconsMode);
        list->setViewMode(QListView::IconMode);
        list->setGridSize(QSize(75, 75));
        list->setIconSize(QSize(32, 32));
        break;
    case List: {
        delegate->setMode(ItemDelegate::Plain);
        list->setViewMode(QListView::ListMode);
        // (IconMode lets items be dragged about; here a drag is a file's.)
        list->setFlow(QListView::TopToBottom);
        int width = listColumnWidth(list);
        list->setGridSize(QSize(width, compactRowHeight(list->fontMetrics())));
        list->setIconSize(QSize(16, 16));
        break;
    }
    case Details:
        break;
    }
}

void FileBrowser::setView(View view)
{
    m_view = view;
    const View shown = shownView();
    m_filmstripAction->setVisible(m_pictureFolder);
    for (QAction *action : m_viewGroup->actions())
        action->setChecked(action->data().toInt() == int(shown));
    placeList(shown == Filmstrip);
    configureList(m_list, shown);
    if (m_location == places::Computer || (m_location == places::ControlPanel && m_pages->currentWidget() != m_categoryArea)) {
        static_cast<VirtualFilter *>(m_virtualFilter)->showGroups(shown != List && shown != Details);
        m_virtualFilter->sort(shown == List || shown == Details ? m_virtualDetails->header()->sortIndicatorSection() : -1,
                              m_virtualDetails->header()->sortIndicatorOrder());
        configureList(m_virtualList, shown);
        if (shown == Tiles || shown == Icons || shown == Thumbnails) {
            m_virtualList->setGridSize(QSize());
            m_virtualList->setSpacing(0);
        }
        m_pages->setCurrentWidget(shown == Details ? static_cast<QWidget *>(m_virtualDetails) : m_virtualList);
    }
    if (!m_location.startsWith(QLatin1String("::")) && !places::isWeb(m_location) && !blocked())
        m_pages->setCurrentWidget(folderPage());
    // (Once the page is there: the selection is read from the view it shows.)
    if (shown == Filmstrip) {
        const QStringList paths = selectedPaths();
        m_filmPreview->setPath(paths.size() == 1 ? paths.first() : QString());
        selectFirstPicture();
    }
    m_list->doItemsLayout();
}

void FileBrowser::applyGroups()
{
    if (!m_detailsDelegate)
        return;
    static_cast<DetailsDelegate *>(m_detailsDelegate)->setGroups(m_groups, m_details->header()->sortIndicatorSection());
    m_details->setUniformRowHeights(!m_groups);
    m_details->doItemsLayout();
    m_details->viewport()->update();
}

void FileBrowser::setFoldersShown(bool shown)
{
    m_foldersShown = shown;
    if (m_foldersAction)
        m_foldersAction->setChecked(shown);
    if (m_searchShown && shown) {
        m_searchShown = false;
        m_searchAction->setChecked(false);
    }
    if (m_searchShown)
        m_left->setCurrentIndex(2);
    else if (shown)
        m_left->setCurrentIndex(1);
    else
        m_left->setCurrentIndex(0);
    // The Common Tasks pane is fixed and touches the folder; a bar can be sized.
    m_splitter->setHandleWidth(shown || m_searchShown ? 4 : 0);
    // XP's "Use Windows classic folders" hides the pane unless a bar is open.
    m_left->setVisible(shown || m_searchShown || !m_noTasks);
    if (shown)
        selectInTree(m_location);
}

void FileBrowser::showSearchBar(bool shown)
{
    m_searchShown = shown;
    m_searchAction->setChecked(shown);
    if (shown) {
        m_foldersShown = false;
        m_foldersAction->setChecked(false);
        m_searchCompanion->setCurrentFolder(currentFolder());
    }
    setFoldersShown(m_foldersShown);
}

// A folder of the tree: its subfolders, read when it opens.
void FileBrowser::fillTreeItem(QTreeWidgetItem *item)
{
    const QString location = item->data(0, Qt::UserRole).toString();
    if (location.startsWith(QLatin1String("::")) || item->data(0, Qt::UserRole + 1).toBool())
        return;
    item->setData(0, Qt::UserRole + 1, true);
    QDir::Filters filter = QDir::Dirs | QDir::NoDotAndDotDot;
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    if (settings.value(QStringLiteral("showHidden"), false).toBool())
        filter |= QDir::Hidden;
    const QFileInfoList children = QDir(location).entryInfoList(filter, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &child : children) {
        auto *childItem = new QTreeWidgetItem(QStringList(child.fileName()));
        childItem->setIcon(0, fileicons::forFile(child));
        childItem->setData(0, Qt::UserRole, child.absoluteFilePath());
        childItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->addChild(childItem);
    }
    if (children.isEmpty())
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
}

// Opens the tree down to LOCATION, from the deepest place that holds it.
void FileBrowser::selectInTree(const QString &location)
{
    if (!m_tree || !m_foldersShown)
        return;
    QTreeWidgetItem *best = nullptr;
    int bestLength = -1;
    std::function<void(QTreeWidgetItem *)> look = [&](QTreeWidgetItem *item) {
        const QString place = item->data(0, Qt::UserRole).toString();
        const bool holds = place == location || (!place.startsWith(QLatin1String("::")) && location.startsWith(place == QLatin1String("/") ? place : place + QLatin1Char('/')));
        if (holds && place.size() > bestLength) {
            best = item;
            bestLength = int(place.size());
        }
        for (int i = 0; i < item->childCount(); ++i)
            look(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        look(m_tree->topLevelItem(i));
    if (!best)
        return;
    while (best->data(0, Qt::UserRole).toString() != location) {
        best->setExpanded(true);
        fillTreeItem(best);
        QTreeWidgetItem *next = nullptr;
        for (int i = 0; i < best->childCount() && !next; ++i) {
            const QString child = best->child(i)->data(0, Qt::UserRole).toString();
            if (child == location || location.startsWith(child + QLatin1Char('/')))
                next = best->child(i);
        }
        if (!next)
            break;
        best = next;
    }
    m_tree->setCurrentItem(best);
    m_tree->scrollToItem(best);
}

QString FileBrowser::currentFolder() const
{
    return m_location.startsWith(QLatin1String("::")) ? (m_location == places::RecycleBin ? fileops::trashFiles() : QString()) : m_location;
}

QAbstractItemView *FileBrowser::currentView() const
{
    if (m_location == places::Search)
        return m_results;
    if (m_pages->currentWidget() == m_details)
        return m_details;
    if (m_pages->currentWidget() == m_virtualList)
        return m_virtualList;
    if (m_pages->currentWidget() == m_virtualDetails)
        return m_virtualDetails;
    return m_list;
}

QStringList FileBrowser::selectedPaths() const
{
    if (m_location == places::Search) {
        QStringList paths;
        const QModelIndexList rows = m_results->selectionModel()->selectedRows();
        for (const QModelIndex &row : rows)
            paths.append(row.data(Qt::UserRole).toString());
        return paths;
    }
    QStringList paths;
    QAbstractItemView *view = currentView();
    if (view == m_virtualList || view == m_virtualDetails)
        return paths;
    const QModelIndexList rows = view->selectionModel()->selectedIndexes();
    for (const QModelIndex &index : rows)
        if (index.column() == 0)
            paths << m_model->path(index);
    return paths;
}

void FileBrowser::navigate(const QString &location, bool remember)
{
    QString target = location;
    const bool web = places::isWeb(target);
    if (!target.startsWith(QLatin1String("::")) && !web)
        target = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
    if (remember && target != m_location && !m_location.isEmpty()) {
        m_back.append(m_location);
        m_forward.clear();
        // XP's click on going somewhere
        sounds::play(QStringLiteral("Windows XP Start.wav"));
    }
    m_searchTimer->stop();
    m_location = target;
    setWindowTitle(places::title(target));
    setWindowIcon(places::icon(target));
    m_address->blockSignals(true);
    m_address->clear();
    m_address->addItem(places::icon(target), target.startsWith(QLatin1String("::")) ? places::title(target) : target);
    if (m_searchCompanion)
        m_searchCompanion->setCurrentFolder(target.startsWith(QLatin1String("::")) || web ? QDir::homePath() : target);
    m_address->setCurrentIndex(0);
    m_address->blockSignals(false);
    m_backButton->setEnabled(!m_back.isEmpty());
    m_forwardButton->setEnabled(!m_forward.isEmpty());
    m_upAction->setEnabled(target != places::Search && !web && target != QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));

    const QString folder = target == places::RecycleBin ? fileops::trashFiles() : target;
    if (web) {
        showWebPage(target);
        updatePane();
        updateActions();
        updateStatus();
        return;
    }
    if (!folder.startsWith(QLatin1String("::"))) {
        QDir().mkpath(target == places::RecycleBin ? folder : QString());
        const QModelIndex root = m_files->setRootPath(folder);
        const QModelIndex shown = m_model->mapFromSource(root);
        m_list->setRootIndex(shown);
        m_details->setRootIndex(shown);
        m_list->clearSelection();
        m_details->clearSelection();
        if (blocked()) {
            m_warning->setDrive(target == QLatin1String("/"));
            m_pages->setCurrentWidget(m_warning);
        } else {
            m_pages->setCurrentWidget(folderPage());
        }
    } else {
        showVirtual(target);
    }
    // Picture folders have Filmstrip, which then stays theirs.
    const bool pictureFolder = !web && !folder.startsWith(QLatin1String("::")) && target != places::RecycleBin && pictures::isPictureFolder(folder);
    if (pictureFolder != m_pictureFolder || (m_view == Filmstrip && !folder.startsWith(QLatin1String("::")))) {
        m_pictureFolder = pictureFolder;
        setView(m_view);
    }
    selectInTree(target);
    updatePane();
    updateActions();
    updateStatus();
}

void FileBrowser::addVirtual(const QString &text, const QIcon &icon, std::function<void()> action, const QString &detail)
{
    auto *item = new QStandardItem(icon, text);
    item->setEditable(false);
    if (!detail.isEmpty())
        item->setData(detail, DetailRole);
    item->setData(QVariant::fromValue(m_actions.size()), ActionRole);
    m_actions.append(std::move(action));
    QList<QStandardItem *> row{item, new QStandardItem(detail.section(QLatin1Char('\n'), 0, 0))};
    m_virtual->appendRow(row);
}

void FileBrowser::addVirtualHeader(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setData(true, HeaderRole);
    item->setFlags(Qt::ItemIsEnabled);
    m_virtual->appendRow(item);
}

void FileBrowser::fillComputer()
{
    addVirtualHeader(xpfiles::shellString(9425, "Files Stored on This Computer"));
    const auto folder = [this](int icon, const QString &name, const QString &path) {
        addVirtual(name, fileicons::shell(icon), [this, path] { navigate(path); }, tr("File Folder"));
        m_virtual->item(m_virtual->rowCount() - 1)->setData(path, PathRole);
    };
    folder(fileicons::Documents, xpfiles::shellString(9227, "My Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    folder(fileicons::Folder, QFileInfo(QDir::homePath()).fileName(), QDir::homePath());
    addVirtualHeader(xpfiles::shellString(STRING_HARD_DISKS, "Hard Disk Drives"));
    QList<QStorageInfo> removable;
    for (const QStorageInfo &drive : QStorageInfo::mountedVolumes()) {
        const QString path = drive.rootPath();
        if (!drive.isValid() || !drive.isReady() || drive.bytesTotal() <= 0)
            continue;
        if (path.startsWith(QLatin1String("/sys")) || path.startsWith(QLatin1String("/proc")) || path.startsWith(QLatin1String("/run/user")) || path.startsWith(QLatin1String("/dev")) || path.startsWith(QLatin1String("/boot")))
            continue;
        const QByteArray type = drive.fileSystemType();
        if (type == "tmpfs" || type == "overlay" || type == "squashfs" || type == "devtmpfs" || type == "efivarfs")
            continue;
        // btrfs and the like mount subvolumes of one disk: the disk appears once.
        static const QStringList subvolumes{QStringLiteral("/home"), QStringLiteral("/var"), QStringLiteral("/root"), QStringLiteral("/srv"), QStringLiteral("/usr"), QStringLiteral("/tmp")};
        if (std::any_of(subvolumes.cbegin(), subvolumes.cend(), [&](const QString &p) { return path == p || path.startsWith(p + QLatin1Char('/')); }))
            continue;
        if (path.startsWith(QLatin1String("/run/media")) || path.startsWith(QLatin1String("/media")) || path.startsWith(QLatin1String("/mnt"))) {
            removable << drive;
            continue;
        }
        const QString name = drive.displayName().isEmpty() || drive.displayName() == path ? xpfiles::shellString(9402, "Local Disk") : drive.displayName();
        addVirtual(QStringLiteral("%1 (%2)").arg(name, path), fileicons::shell(fileicons::Drive), [this, path] { navigate(path); },
                   tr("%1 free of %2").arg(xpSize(drive.bytesAvailable()), xpSize(drive.bytesTotal())));
        const int row = m_virtual->rowCount() - 1;
        m_virtual->item(row)->setData(path, PathRole);
        m_virtual->setItem(row, 1, new QStandardItem(xpfiles::shellString(9402, "Local Disk")));
        m_virtual->setItem(row, 2, new QStandardItem(xpSize(drive.bytesTotal())));
        m_virtual->setItem(row, 3, new QStandardItem(xpSize(drive.bytesAvailable())));
        m_virtual->item(row, 2)->setData(qulonglong(drive.bytesTotal()), Qt::UserRole);
        m_virtual->item(row, 3)->setData(qulonglong(drive.bytesAvailable()), Qt::UserRole);
        for (int column : {2, 3}) m_virtual->item(row, column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (!removable.isEmpty()) {
        addVirtualHeader(tr("Devices with Removable Storage"));
        for (const QStorageInfo &drive : std::as_const(removable)) {
            const QString path = drive.rootPath();
            addVirtual(drive.displayName(), fileicons::shell(fileicons::Removable), [this, path] { navigate(path); },
                       tr("%1 free of %2").arg(xpSize(drive.bytesAvailable()), xpSize(drive.bytesTotal())));
            const int row = m_virtual->rowCount() - 1;
            m_virtual->item(row)->setData(path, PathRole);
            m_virtual->setItem(row, 1, new QStandardItem(tr("Removable Disk")));
            m_virtual->setItem(row, 2, new QStandardItem(xpSize(drive.bytesTotal())));
            m_virtual->setItem(row, 3, new QStandardItem(xpSize(drive.bytesAvailable())));
            m_virtual->item(row, 2)->setData(qulonglong(drive.bytesTotal()), Qt::UserRole);
            m_virtual->item(row, 3)->setData(qulonglong(drive.bytesAvailable()), Qt::UserRole);
            for (int column : {2, 3}) m_virtual->item(row, column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

void FileBrowser::fillControlPanel()
{
    for (const controlpanel::Applet &applet : controlpanel::applets()) {
        const QString id = applet.id;
        addVirtual(applet.name, applet.icon, [id] { controlpanel::open(id); }, applet.description);
    }
}

void FileBrowser::showVirtual(const QString &location)
{
    if (location == places::Search) {
        m_pages->setCurrentWidget(m_results);
        if (!m_searchShown)
            showSearchBar(true);
        return;
    }
    m_virtual->removeRows(0, m_virtual->rowCount());
    const int columns = location == places::Computer ? 4 : 2;
    const bool changedColumns = m_virtual->columnCount() != columns;
    m_virtual->setColumnCount(columns);
    m_virtual->setHorizontalHeaderLabels(location == places::Computer ? QStringList{tr("Name"), tr("Type"), tr("Total Size"), tr("Free Space")}
                                                                     : QStringList{tr("Name"), tr("Comments")});
    // Total Size and Free Space to the right, as shell32's table has them
    for (int column = 2; location == places::Computer && column < 4; ++column)
        m_virtual->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (changedColumns) {
        if (location == places::Computer)
            setColumnChars(m_virtualDetails, {20, 25, 16, 16});
        else
            setColumnChars(m_virtualDetails, {20, 20});
    }
    m_actions.clear();
    if (location == places::Computer) {
        fillComputer();
    } else if (location.startsWith(places::ControlPanel + QLatin1Char('/'))) {
        static_cast<ControlPanelCategories *>(m_categoryPage)->setCategory(location.section(QLatin1Char('/'), 1).toInt());
        m_pages->setCurrentWidget(m_categoryArea);
        return;
    } else if (location == places::ControlPanel) {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        if (!settings.value(QStringLiteral("classicPanel"), false).toBool()) {
            static_cast<ControlPanelCategories *>(m_categoryPage)->setCategory(-1);
            m_pages->setCurrentWidget(m_categoryArea);
            return;
        }
        fillControlPanel();
    }
    m_pages->setCurrentWidget(m_virtualList);
    setView(m_view);
    m_virtualList->doItemsLayout();
}

bool FileBrowser::blocked() const
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    return isSystemFolder(m_location) && !settings.value(QStringLiteral("shownSystemFolders")).toStringList().contains(m_location);
}

// XP remembers, for each of them, that its contents were asked for.
void FileBrowser::setSystemFolderShown(bool shown)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    QStringList folders = settings.value(QStringLiteral("shownSystemFolders")).toStringList();
    folders.removeAll(m_location);
    if (shown)
        folders.append(m_location);
    settings.setValue(QStringLiteral("shownSystemFolders"), folders);
    settings.sync();
    navigate(m_location, false);
}

void FileBrowser::openPath(const QString &path)
{
    if (QFileInfo(path).isDir()) {
        navigate(path);
        return;
    }
    if (QFileInfo(path).suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(path)) {
            programs::launch(*program);
            return;
        }
    }
    // pictures open in Windows Picture and Fax Viewer, its Preview being their default verb on XP
    if (pictures::isPicture(path)) {
        pictures::Viewer::open(path);
        return;
    }
    programs::open(path);
}

void FileBrowser::activate(const QModelIndex &index)
{
    if (index.model() == m_virtual || index.model() == m_virtualFilter) {
        const QVariant slot = index.data(ActionRole);
        if (slot.isValid() && slot.toInt() < m_actions.size() && m_actions[slot.toInt()]) {
            const auto action = m_actions[slot.toInt()];
            action();
        }
        return;
    }
    if (m_location == places::RecycleBin)
        return;
    openPath(m_model->path(index));
}

void FileBrowser::updateActions()
{
    const bool files = !m_location.startsWith(QLatin1String("::"));
    const QStringList selected = selectedPaths();
    const bool some = !selected.isEmpty() && (files || m_location == places::RecycleBin);
    m_deleteAction->setEnabled(some);
    m_cutAction->setEnabled(some && files);
    m_copyAction->setEnabled(some && files);
    m_moveAction->setEnabled(some && files);
    m_copyToAction->setEnabled(some && files);
    m_renameAction->setEnabled(selected.size() == 1 && files);
    m_propertiesAction->setEnabled(some || files || m_location == places::RecycleBin);
    m_pasteAction->setEnabled(files && fileops::clipboardHasFiles());
    m_newFolderAction->setEnabled(files);
    m_newTextAction->setEnabled(files);
}

void FileBrowser::updateStatus()
{
    const QStringList selected = selectedPaths();
    QString text;
    if (!selected.isEmpty()) {
        text = tr("%n object(s) selected", nullptr, int(selected.size()));
    } else if (m_location == places::Search) {
        text = tr("%n object(s) found", nullptr, m_resultsModel->rowCount());
    } else if (places::isWeb(m_location)) {
        text = tr("Done");
    } else if (!m_location.startsWith(QLatin1String("::")) || m_location == places::RecycleBin) {
        const int count = m_model->rowCount(m_list->rootIndex());
        text = tr("%n object(s)", nullptr, count);
    } else {
        text = tr("%n object(s)", nullptr, m_virtual->rowCount());
    }
    statusBar()->showMessage(text);
}

void FileBrowser::updatePane()
{
    QList<PaneGroup> groups;
    const QStringList selected = selectedPaths();
    const auto place = [this](const QString &target) {
        return PaneTask{places::title(target), places::icon(target), [this, target] { navigate(target); }};
    };
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    PaneGroup others{xpfiles::shellString(STRING_OTHER_PLACES, "Other Places"), false, {}};
    PaneGroup details{xpfiles::shellString(STRING_DETAILS, "Details"), false, {}};
    // Collapsed until opened, as XP's own shell32 shows it
    details.collapsed = true;
    if (m_location == places::Computer) {
        groups.append({xpfiles::shellString(STRING_SYSTEM_TASKS, "System Tasks"), true, {
            {xpfiles::shellString(STRING_SYSTEM_INFO, "View system information"), fileicons::shell(fileicons::SystemInfo), [] { controlpanel::open(QStringLiteral("system")); }},
            {xpfiles::shellString(STRING_ADD_REMOVE, "Add or remove programs"), fileicons::shell(fileicons::AddRemove), [] { controlpanel::open(QStringLiteral("programs")); }},
            {xpfiles::shellString(STRING_CHANGE, "Change a setting"), fileicons::shell(fileicons::ControlPanel), [this] { navigate(places::ControlPanel); }},
        }});
        others.tasks = {place(documents), place(places::ControlPanel)};
        details.tasks = {{places::title(places::Computer), {}, {}, true}, {tr("System Folder"), {}, {}}};
    } else if (m_location == places::RecycleBin) {
        PaneGroup tasks{tr("Recycle Bin Tasks"), true, {
            {xpfiles::shellString(STRING_EMPTY, "Empty the Recycle Bin"), fileicons::shell(fileicons::RecycleEmpty), [this] { fileops::emptyTrash(this); }},
        }};
        if (!selected.isEmpty())
            tasks.tasks.append({tr("Restore this item"), fileicons::shell(fileicons::Document), [this] { fileops::restoreFromTrash(selectedPaths(), this); }});
        groups.append(tasks);
        others.tasks = {place(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)), place(documents), place(places::Computer)};
    } else if (m_location.startsWith(places::ControlPanel + QLatin1Char('/'))) {
        groups.append({xpfiles::shellString(STRING_SEE_ALSO, "See Also"), false, {
            {tr("Folder Options"), fileicons::shell(fileicons::Folder), [] { controlpanel::open(QStringLiteral("folders")); }},
            {places::title(places::ControlPanel), fileicons::shell(fileicons::ControlPanel), [this] { navigate(places::ControlPanel); }},
        }});
        m_pane->setGroups(groups);
        return;
    } else if (m_location == places::ControlPanel) {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        const bool classic = settings.value(QStringLiteral("classicPanel"), false).toBool();
        PaneGroup panel{places::title(places::ControlPanel), true, {
            {xpfiles::shellString(classic ? STRING_SWITCH_CATEGORY : STRING_SWITCH_CLASSIC, classic ? "Switch to Category View" : "Switch to Classic View"),
             fileicons::shell(fileicons::ControlPanel), [this, classic] {
                 QSettings write(settingsPath(), QSettings::IniFormat);
                 write.setValue(QStringLiteral("classicPanel"), !classic);
                 write.sync();
                 showVirtual(places::ControlPanel);
                 updatePane();
             }},
        }};
        panel.icon = fileicons::shell(fileicons::ControlPanel);
        groups.append(panel);
        // XP's Windows Update and Help and Support: Linux's own, when it has them
        PaneGroup seeAlso{xpfiles::shellString(STRING_SEE_ALSO, "See Also"), false, {}};
        const auto programOf = [](std::initializer_list<const char *> names) {
            for (const char *name : names)
                if (const QString found = QStandardPaths::findExecutable(QLatin1String(name)); !found.isEmpty())
                    return found;
            return QString();
        };
        if (const QString updater = programOf({"plasma-discover", "gnome-software", "pamac-manager", "update-manager"}); !updater.isEmpty())
            seeAlso.tasks.append({xpfiles::shellString(32038, "Windows Update"), fileicons::shell(47), [updater] { QProcess::startDetached(updater, {}); }});
        if (const QString help = programOf({"khelpcenter", "yelp"}); !help.isEmpty())
            seeAlso.tasks.append({xpfiles::shellString(32036, "Help and Support"), fileicons::shell(24), [help] { QProcess::startDetached(help, {}); }});
        if (seeAlso.tasks.isEmpty())
            seeAlso.tasks.append({tr("Folder Options"), fileicons::shell(fileicons::Folder), [] { controlpanel::open(QStringLiteral("folders")); }});
        groups.append(seeAlso);
        m_pane->setGroups(groups);
        return;
    } else if (m_location == places::Search || places::isWeb(m_location)) {
        m_pane->setGroups(groups);
        return;
    } else if (isSystemFolder(m_location) && blocked()) {
        const bool drive = m_location == QLatin1String("/");
        groups.append({xpfiles::shellString(STRING_SYSTEM_TASKS, "System Tasks"), true, {
            {drive ? xpfiles::shellString(31319, "Show the contents of this drive") : xpfiles::shellString(31323, "Show the contents of this folder"), fileicons::shell(fileicons::ShowContents), [this] { setSystemFolderShown(true); }},
            {xpfiles::shellString(STRING_ADD_REMOVE, "Add or remove programs"), fileicons::shell(fileicons::AddRemove), [] { controlpanel::open(QStringLiteral("programs")); }},
            {tr("Search for files or folders"), fileicons::shell(fileicons::Search), [this] { showSearchBar(true); }},
        }});
        others.tasks = {place(places::Computer), place(documents)};
        details.tasks = {{places::title(m_location), {}, {}, true}, {drive ? xpfiles::shellString(9402, "Local Disk") : fileicons::typeName(QFileInfo(m_location)), {}, {}}};
        if (drive) {
            const QStorageInfo disk(m_location);
            details.tasks.append({tr("File System: %1").arg(QString::fromLatin1(disk.fileSystemType())), {}, {}});
            details.tasks.append({tr("Free Space: %1").arg(xpSize(disk.bytesAvailable())), {}, {}});
            details.tasks.append({tr("Total Size: %1").arg(xpSize(disk.bytesTotal())), {}, {}});
        }
    } else {
        if (isSystemFolder(m_location)) {
            const bool drive = m_location == QLatin1String("/");
            groups.append({xpfiles::shellString(STRING_SYSTEM_TASKS, "System Tasks"), true, {
                {drive ? xpfiles::shellString(31321, "Hide the contents of this drive") : xpfiles::shellString(31325, "Hide the contents of this folder"), fileicons::shell(fileicons::ShowContents), [this] { setSystemFolderShown(false); }},
            }});
        }
        // Picture folders, and pictures chosen elsewhere: the Picture Tasks first, in the main header
        const QStringList chosenPictures = selectedPictures();
        const bool picturesChosen = !selected.isEmpty() && chosenPictures.size() == selected.size();
        if (m_pictureFolder || picturesChosen) {
            PaneGroup pictureTasks{xpfiles::shellString(STRING_PICTURE_TASKS, "Picture Tasks"), true, {}};
            if (picturesChosen || !pictures::picturesIn(m_location).isEmpty())
                pictureTasks.tasks.append({xpfiles::shellString(STRING_SLIDE_SHOW, "View as a slide show"), pictures::viewerIcon(pictures::SlideShowButton), [this] { startSlideShow(); }});
            if (chosenPictures.size() == 1 && selected.size() == 1)
                pictureTasks.tasks.append({xpfiles::shellString(STRING_BACKGROUND, "Set as desktop background"), fileicons::shell(35),
                                           [this, path = chosenPictures.first()] { setAsBackground(path); }});
            if (!pictureTasks.tasks.isEmpty())
                groups.append(pictureTasks);
        }
        // An ordinary section, as XP's own shell32 shows it: only special ones (System Tasks, Picture Tasks)
        // have the blue header.
        PaneGroup tasks{xpfiles::shellString(STRING_FILE_TASKS, "File and Folder Tasks"), false, {}};
        if (selected.isEmpty()) {
            tasks.tasks.append({xpfiles::shellString(STRING_NEW_FOLDER, "Make a new folder"), fileicons::shell(fileicons::NewFolder), [this] { newFolder(); }});
        } else {
            // XP's words for what is selected: a file, a folder or several items
            const bool several = selected.size() > 1, folder = !several && QFileInfo(selected.first()).isDir();
            const auto words = [&](int fileId, const char *fileText, int folderId, const char *folderText, int manyId, const char *manyText) {
                return several ? xpfiles::shellString(manyId, manyText) : folder ? xpfiles::shellString(folderId, folderText) : xpfiles::shellString(fileId, fileText);
            };
            if (!several)
                tasks.tasks.append({folder ? xpfiles::shellString(31254, "Rename this folder") : xpfiles::shellString(STRING_RENAME, "Rename this file"), fileicons::shell(fileicons::RenameTask), [this] { renameSelected(); }});
            tasks.tasks.append({words(STRING_MOVE, "Move this file", 31256, "Move this folder", 31264, "Move the selected items"), fileicons::shell(fileicons::MoveTask), [this] { moveOrCopyTo(true); }});
            tasks.tasks.append({words(STRING_COPY, "Copy this file", 31258, "Copy this folder", 31266, "Copy the selected items"), fileicons::shell(fileicons::CopyTask), [this] { moveOrCopyTo(false); }});
            tasks.tasks.append({words(STRING_DELETE, "Delete this file", 31262, "Delete this folder", 31270, "Delete the selected items"), fileicons::shell(fileicons::DeleteTask), [this] { deleteSelected(false); }});
        }
        groups.append(tasks);
        const QFileInfo info(m_location);
        if (!info.isRoot())
            others.tasks.append(place(info.absolutePath()));
        if (m_location != documents)
            others.tasks.append(place(documents));
        others.tasks.append(place(places::Computer));
        // XP's Details: a picture's preview over its name, then "Label: value" lines
        const auto labelled = [](int label, const char *fallback, const QString &value) {
            return PaneTask{xpfiles::shellString(STRING_LABELLED, "%1!ls!: %2!ls!").replace(QLatin1String("%1!ls!"), xpfiles::shellString(label, fallback))
                                .replace(QLatin1String("%2!ls!"), value), {}, {}};
        };
        const QFileInfo shown(selected.size() == 1 ? selected.first() : m_location);
        if (selected.size() > 1) {
            details.tasks.append({xpfiles::shellString(STRING_ITEMS_SELECTED, "%ld items selected.").replace(QLatin1String("%ld"), QString::number(selected.size())), {}, {}, true});
            qint64 total = 0;
            bool folders = false;
            for (const QString &path : selected) {
                const QFileInfo item(path);
                folders |= item.isDir();
                total += item.size();
            }
            // Only files' sizes add up here; a folder's would have to be counted.
            if (!folders)
                details.tasks.append({xpfiles::shellString(STRING_TOTAL_SIZE, "Total File Size: %ls").replace(QLatin1String("%ls"), xpSize(total)), {}, {}});
        } else {
            const bool picture = selected.size() == 1 && pictures::isPicture(shown.absoluteFilePath());
            if (picture) {
                const qreal ratio = devicePixelRatioF();
                QImage preview = pictures::load(shown.absoluteFilePath(), QSize(ShellTheme::instance()->scaled(PREVIEW_SIDE), ShellTheme::instance()->scaled(PREVIEW_SIDE)) * ratio);
                preview.setDevicePixelRatio(ratio);
                if (!preview.isNull())
                    details.tasks.append({QString(), {}, {}, false, preview});
            }
            details.tasks.append({places::title(shown.absoluteFilePath()), {}, {}, true});
            details.tasks.append({fileicons::typeName(shown), {}, {}});
            if (picture) {
                if (const QSize size = pictures::dimensions(shown.absoluteFilePath()); size.isValid())
                    details.tasks.append(labelled(STRING_DIMENSIONS, "Dimensions", pictures::viewerString(308, "%1!d! x %2!d!")
                                                                                      .replace(QLatin1String("%1!d!"), QString::number(size.width()))
                                                                                      .replace(QLatin1String("%2!d!"), QString::number(size.height()))));
            }
            details.tasks.append(labelled(STRING_DATE_MODIFIED, "Date Modified", QLocale().toString(shown.lastModified(), QLocale::ShortFormat)));
            if (!shown.isDir())
                details.tasks.append(labelled(STRING_SIZE, "Size", xpSize(shown.size())));
        }
    }
    if (!others.tasks.isEmpty())
        groups.append(others);
    if (!details.tasks.isEmpty())
        groups.append(details);
    m_pane->setGroups(groups);
}

void FileBrowser::placeEdgeLines()
{
    // A visual style's; Windows Classic has none.
    const bool themed = ShellTheme::instance()->renderer();
    m_splitter->setContentsMargins(themed ? 1 : 0, 0, 0, themed ? 1 : 0);
    const QRect area = m_splitter->geometry();
    m_leftLine->setGeometry(area.left(), area.top(), 1, area.height());
    m_bottomLine->setGeometry(area.left(), area.bottom(), area.width(), 1);
    m_leftLine->setVisible(themed);
    m_bottomLine->setVisible(themed);
    m_leftLine->raise();
    m_bottomLine->raise();
}

bool FileBrowser::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_splitter && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        placeEdgeLines();
    // Under a visual style, Details shades the column it is sorted by, down to the bottom;
    // the rows are drawn over it.
    QTreeView *report = m_details && watched == m_details->viewport() ? m_details
                      : m_virtualDetails && watched == m_virtualDetails->viewport() ? m_virtualDetails : nullptr;
    if (report && event->type() == QEvent::Paint && ShellTheme::instance()->renderer()) {
        const int column = report->header()->sortIndicatorSection();
        if (column >= 0 && column < report->model()->columnCount()) {
            QPainter painter(report->viewport());
            const int x = report->header()->sectionViewportPosition(column);
            painter.fillRect(QRect(x, 0, report->header()->sectionSize(column), report->viewport()->height()), SORTED_COLUMN);
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete) {
            deleteSelected(key->modifiers() & Qt::ShiftModifier);
            return true;
        }
        if (key->key() == Qt::Key_F2) {
            renameSelected();
            return true;
        }
        if (key->key() == Qt::Key_Backspace) {
            m_upAction->trigger();
            return true;
        }
    }
    // (Events come while the views are still being made.)
    if ((m_list && watched == m_list->viewport()) || (m_details && watched == m_details->viewport())) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDropEvent *>(event);
            if (drag->mimeData()->hasUrls() && !currentFolder().isEmpty()) {
                drag->setDropAction(drag->modifiers() & Qt::ControlModifier ? Qt::CopyAction : Qt::MoveAction);
                drag->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            QStringList sources;
            for (const QUrl &url : drop->mimeData()->urls())
                if (url.isLocalFile())
                    sources << url.toLocalFile();
            QAbstractItemView *view = currentView();
            const QModelIndex under = view->indexAt(drop->position().toPoint());
            QString target = currentFolder();
            if (under.isValid() && view != m_virtualList && QFileInfo(m_model->path(under)).isDir())
                target = m_model->path(under);
            if (!sources.isEmpty() && !target.isEmpty() && !sources.contains(target))
                fileops::transfer(sources, target, drop->modifiers() & Qt::ControlModifier ? fileops::Action::Copy : fileops::Action::Move, this);
            drop->setDropAction(Qt::IgnoreAction);
            drop->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void FileBrowser::showViewContextMenu(QAbstractItemView *view, QPoint at)
{
    QModelIndex index = at.x() < 0 || at.y() < 0 ? view->currentIndex() : view->indexAt(at);
    const bool onItem = index.isValid() && (index.flags() & Qt::ItemIsSelectable);
    if (onItem) {
        index = index.siblingAtColumn(0);
        const auto command = view->selectionModel()->isSelected(index) ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
        view->selectionModel()->setCurrentIndex(index, command);
        if (at.x() < 0 || at.y() < 0) at = view->visualRect(index).center();
    } else {
        view->clearSelection();
    }
    showContextMenu(view->viewport()->mapToGlobal(at), onItem);
}

void FileBrowser::showContextMenu(QPoint global, bool onItem)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString folder = currentFolder();
    if (onItem && (currentView() == m_virtualList || currentView() == m_virtualDetails)) {
        const QModelIndex index = currentView()->currentIndex().siblingAtColumn(0);
        auto *open = menu->addAction(tr("&Open"), this, [this, index] { activate(index); });
        menu->setDefaultAction(open);
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty()) {
            menu->addSeparator();
            menu->addAction(tr("P&roperties"), this, [this, path] { showItemProperties({path}, this); });
        }
    } else if (onItem && m_location == places::RecycleBin) {
        menu->addAction(tr("R&estore"), this, [this] { fileops::restoreFromTrash(selectedPaths(), this); });
        menu->addSeparator();
        menu->addAction(tr("&Delete"), this, [this] { deleteSelected(true); });
    } else if (onItem && !folder.isEmpty()) {
        fillItemMenu(menu, selectedPaths());
    } else if (!folder.isEmpty() && m_location != places::RecycleBin) {
        fillBackgroundMenu(menu, folder);
    } else {
        menu->addMenu(m_viewsMenu)->setText(tr("&View"));
        menu->addAction(tr("R&efresh"), this, [this] { navigate(m_location, false); });
        if (m_location == places::RecycleBin)
            menu->addAction(tr("P&roperties"), this, &FileBrowser::recycleBinProperties);
        if (m_location == places::Computer) {
            menu->addSeparator();
            menu->addAction(tr("P&roperties"), this, [] { controlpanel::open(QStringLiteral("system")); });
        }
    }
    menu->popup(global);
}

using namespace shellmenus;

void FileBrowser::fillItemMenu(QMenu *menu, const QStringList &selected)
{
    const bool folderItem = selected.size() == 1 && QFileInfo(selected.first()).isDir();
    // The verbs first, the default one in bold: a picture's is shimgvw.dll's Preview, Open the desktop's program
    const bool picturesOnly = !selected.isEmpty() && selectedPictures().size() == selected.size();
    if (picturesOnly) {
        QAction *preview = menu->addAction(pictures::viewerString(550, "Pre&view"), this, [this] {
            const QStringList paths = selectedPaths();
            if (!paths.isEmpty())
                pictures::Viewer::open(paths.first());
        });
        QFont bold = preview->font();
        bold.setBold(true);
        preview->setFont(bold);
        menu->setDefaultAction(preview);
    }
    QAction *open = menu->addAction(xpfiles::shellString(STRING_OPEN, "&Open"), this, [this, picturesOnly] {
        const QStringList paths = selectedPaths();
        if (paths.isEmpty())
            return;
        if (picturesOnly)
            programs::open(paths.first());
        else
            openPath(paths.first());
    });
    if (!picturesOnly)
        menu->setDefaultAction(open);
    if (!picturesOnly) {
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
    }
    if (folderItem) {
        menu->addAction(xpfiles::shellString(STRING_EXPLORE, "E&xplore"), this, [path = selected.first()] {
            auto *window = new FileBrowser(path);
            window->setFoldersShown(true);
            window->show();
        });
        menu->addAction(xpfiles::shellString(STRING_SEARCH, "S&earch..."), this, [this, path = selected.first()] {
            navigate(path);
            showSearchBar(true);
        });
        menu->addAction(tr("Open Command &Prompt Here"), this, [path = selected.first()] { programs::openCommandPrompt(path); });
    }
    if (!folderItem && selected.size() == 1) {
        QMenu *with = menu->addMenu(tr("Open &With"));
        const QList<programs::Program> all = programs::allPrograms();
        const int shown = qMin<int>(12, int(all.size()));
        for (int i = 0; i < shown; ++i)
            with->addAction(programs::iconOf(all[i]), all[i].name, this,
                            [path = selected.first(), program = all[i]] { programs::launchWith(program, {path}); });
        if (shown)
            with->addSeparator();
        with->addAction(tr("&Choose Program..."), this, [path = selected.first(), this] {
            if (const auto program = programs::chooseProgram(path, this))
                programs::launchWith(*program, {path});
        });
        with->setEnabled(!all.isEmpty());
    }
    // What the Picture and Fax Viewer adds for pictures, in its words
    if (selectedPictures().size() == selected.size()) {
        menu->addSeparator();
        menu->addAction(pictures::viewerString(557, "Rotate Cloc&kwise"), this, [this] { rotateSelected(true); });
        menu->addAction(pictures::viewerString(558, "Rotate Cou&nter Clockwise"), this, [this] { rotateSelected(false); });
        if (selected.size() == 1) {
            menu->addSeparator();
            menu->addAction(pictures::viewerString(560, "Set as Desktop &Background"), this, [this, path = selected.first()] { setAsBackground(path); });
        }
    }
    // The file type's verbs (txtfile's Edit/Print, AVIFile's Play) before Send To.
    addFileVerbs(menu, selected, this);
    if (!shell32())
        return;
    // Send To, what XP's SendTo folder has
    menu->addSeparator();
    addSendTo(menu, selected, this);
    // Cut, Copy, Paste (into a folder), Create Shortcut, Delete, Rename
    menu->addSeparator();
    QHash<int, QAction *> edit = xpprogram::fillMenu(menu, *shell32(), MENU_ITEM);
    const auto on = [&edit](int command, auto slot) {
        if (QAction *action = edit.value(command))
            QObject::connect(action, &QAction::triggered, slot);
    };
    on(CMD_CUT, [this] { fileops::copyToClipboard(selectedPaths(), true); });
    on(CMD_COPY, [this] { fileops::copyToClipboard(selectedPaths(), false); });
    if (QAction *paste = edit.value(CMD_PASTE)) {
        // only into a folder, as shell32 keeps it
        if (!folderItem)
            menu->removeAction(paste);
        paste->setEnabled(fileops::clipboardHasFiles());
        connect(paste, &QAction::triggered, this, [this, selected] { fileops::pasteInto(selected.first(), this); });
    }
    on(CMD_SHORTCUT, [this, selected] { makeShortcuts(selected, QFileInfo(selected.first()).absolutePath()); });
    on(CMD_DELETE, [this] { deleteSelected(false); });
    on(CMD_RENAME, [this] { renameSelected(); });
    if (QAction *rename = edit.value(CMD_RENAME))
        rename->setEnabled(selected.size() == 1);
    menu->addSeparator();
    QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
    if (QAction *action = properties.value(CMD_PROPERTIES))
        connect(action, &QAction::triggered, this, &FileBrowser::showProperties);
}

void FileBrowser::fillBackgroundMenu(QMenu *menu, const QString &folder)
{
    if (!shell32())
        return;
    QHash<int, QAction *> items = xpprogram::fillMenu(menu, *shell32(), MENU_BACKGROUND);
    // View: this window's views, Filmstrip in picture folders only
    const QHash<int, View> views{{CMD_FILMSTRIP, Filmstrip}, {CMD_THUMBNAILS, Thumbnails}, {CMD_TILES, Tiles}, {CMD_ICONS, Icons}, {CMD_LIST, List},
                                 {CMD_DETAILS, Details}};
    auto *group = new QActionGroup(menu);
    for (auto it = views.begin(); it != views.end(); ++it)
        if (QAction *action = items.value(it.key())) {
            action->setCheckable(true);
            action->setChecked(m_view == it.value());
            action->setVisible(it.value() != Filmstrip || m_pictureFolder);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, view = it.value()] { setView(view); });
        }
    // Arrange Icons By: the columns of a file folder before shell32's own items
    if (QAction *arrange = items.value(CMD_ARRANGE); arrange && arrange->menu()) {
        QMenu *orders = arrange->menu();
        QAction *first = orders->actions().value(0);
        for (const Order &column : shellmenus::orders()) {
            auto *action = new QAction(xpfiles::shellString(column.string, column.fallback), orders);
            connect(action, &QAction::triggered, this, [this, column = column.column] { m_details->sortByColumn(column, Qt::AscendingOrder); });
            orders->insertAction(first, action);
        }
    }
    // Auto Arrange keeps the icons on their grid; off, they can be dragged about.
    if (QAction *arrange = items.value(CMD_AUTO_ARRANGE)) {
        arrange->setCheckable(true);
        arrange->setChecked(m_autoArrange);
        connect(arrange, &QAction::triggered, this, [this, arrange](bool on) {
            m_autoArrange = on;
            QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("folder/autoArrange"), on);
            arrange->setChecked(on);
            configureList(m_list, shownView());
        });
    }
    // What this window does not have yet
    for (int command : {CMD_ALIGN, CMD_CUSTOMIZE})
        if (QAction *action = items.value(command))
            action->setEnabled(false);
    if (QAction *undo = items.value(CMD_UNDO)) {
        undo->setEnabled(bool(m_undo));
        connect(undo, &QAction::triggered, this, [this] { m_undoAction->trigger(); });
    }
    if (QAction *groups = items.value(CMD_GROUPS)) {
        groups->setCheckable(true);
        groups->setChecked(m_groups);
        connect(groups, &QAction::triggered, this, [this, groups](bool on) {
            m_groups = on;
            QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("folder/groups"), on);
            applyGroups();
            groups->setChecked(on);
        });
    }
    if (QAction *refresh = items.value(CMD_REFRESH))
        connect(refresh, &QAction::triggered, this, [this] { navigate(m_location, false); });
    const bool files = fileops::clipboardHasFiles();
    if (QAction *paste = items.value(CMD_PASTE_HERE)) {
        paste->setEnabled(files);
        connect(paste, &QAction::triggered, this, [this, folder] { fileops::pasteInto(folder, this); });
    }
    if (QAction *shortcut = items.value(CMD_PASTE_SHORTCUT)) {
        shortcut->setEnabled(files);
        connect(shortcut, &QAction::triggered, this, [folder] {
            const QList<QUrl> urls = QGuiApplication::clipboard()->mimeData()->urls();
            QStringList paths;
            for (const QUrl &url : urls)
                if (url.isLocalFile())
                    paths.append(url.toLocalFile());
            makeShortcuts(paths, folder);
        });
    }
    menu->addSeparator();
    menu->addAction(tr("Open Command &Prompt Here"), this, [folder] { programs::openCommandPrompt(folder); });
    menu->addSeparator();
    QMenu *create = menu->addMenu(tr("Ne&w"));
    create->addAction(fileicons::shell(fileicons::Folder), tr("&Folder"), this, &FileBrowser::newFolder);
    create->addAction(fileicons::shell(fileicons::Document), tr("&Text Document"), this, &FileBrowser::newTextDocument);
    create->addAction(tr("&Bitmap Image"), this, &FileBrowser::newBitmapImage);
    create->addAction(tr("&WordPad Document"), this, &FileBrowser::newWordPadDocument);
    create->addAction(tr("&Wave Sound"), this, &FileBrowser::newWaveSound);
    create->addAction(tr("&Briefcase"), this, &FileBrowser::newBriefcase);
    create->addAction(tr("Compressed (&zipped) Folder"), this, &FileBrowser::newCompressedFolder);
    menu->addSeparator();
    QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
    if (QAction *action = properties.value(CMD_PROPERTIES))
        connect(action, &QAction::triggered, this, &FileBrowser::showProperties);
}

void FileBrowser::newFolder()
{
    const QString folder = currentFolder();
    if (folder.isEmpty())
        return;
    const QString name = fileops::freeName(folder, tr("New Folder"));
    if (!QDir(folder).mkdir(name)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot create the folder '%1'.").arg(name));
        return;
    }
    m_pendingRename = QDir(folder).filePath(name);
    QTimer::singleShot(250, this, &FileBrowser::renamePending);
}

void FileBrowser::newTextDocument()
{
    const QString folder = currentFolder();
    if (folder.isEmpty())
        return;
    const QString name = fileops::freeName(folder, tr("New Text Document.txt"));
    QFile file(QDir(folder).filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot create '%1'.").arg(name));
        return;
    }
    file.close();
    m_pendingRename = file.fileName();
    QTimer::singleShot(250, this, &FileBrowser::renamePending);
}

void FileBrowser::newFile(const QString &name, const QByteArray &contents)
{
    const QString folder = currentFolder();
    if (folder.isEmpty())
        return;
    const QString free = fileops::freeName(folder, name);
    QFile file(QDir(folder).filePath(free));
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot create '%1'.").arg(free));
        return;
    }
    file.close();
    m_pendingRename = file.fileName();
    QTimer::singleShot(250, this, &FileBrowser::renamePending);
}

void FileBrowser::newShortcut()
{
    const QString folder = currentFolder();
    if (folder.isEmpty())
        return;
    const QString target = QFileDialog::getOpenFileName(this, tr("Create Shortcut"), QDir::homePath());
    if (target.isEmpty())
        return;
    const QString name = fileops::freeName(folder, tr("New Shortcut"));
    const QString link = QDir(folder).filePath(name + QStringLiteral(".lnk"));
    if (!fileops::createShortcut(target, link))
        QMessageBox::warning(this, windowTitle(), tr("Cannot create the shortcut '%1'.").arg(name));
}

void FileBrowser::newBitmapImage()
{
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::white);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "BMP");
    newFile(tr("New Bitmap Image.bmp"), bytes);
}

void FileBrowser::newWordPadDocument()
{
    newFile(tr("New WordPad Document.rtf"), QByteArray("{\\rtf1}"));
}

void FileBrowser::newWaveSound()
{
    // a 44-byte PCM header with no samples, as a new WordPad document is empty
    QByteArray wave(44, '\0');
    wave.replace(0, 4, "RIFF");
    wave.replace(8, 4, "WAVEfmt ");
    wave[16] = 16;
    wave[20] = 1;    // PCM
    wave[22] = 1;    // mono
    wave[24] = 0x44; wave[25] = 0xAC;   // 44100 Hz
    wave[28] = 0x88; wave[29] = 0x58; wave[30] = 0x01; wave[31] = 0x00;
    wave[32] = 2;    // block alignment
    wave[34] = 16;   // bits per sample
    wave.replace(36, 4, "data");
    newFile(tr("New Wave Sound.wav"), wave);
}

void FileBrowser::newBriefcase()
{
    const QString folder = currentFolder();
    if (folder.isEmpty())
        return;
    const QString name = fileops::freeName(folder, tr("New Briefcase"));
    if (!QDir(folder).mkdir(name))
        QMessageBox::warning(this, windowTitle(), tr("Cannot create the folder '%1'.").arg(name));
    else {
        m_pendingRename = QDir(folder).filePath(name);
        QTimer::singleShot(250, this, &FileBrowser::renamePending);
    }
}

void FileBrowser::newCompressedFolder()
{
    // an empty ZIP archive: its end-of-central-directory record alone
    QByteArray zip(22, '\0');
    zip.replace(0, 4, "PK\x05\x06");
    newFile(tr("New Compressed (zipped) Folder.zip"), zip);
}

void FileBrowser::renamePending()
{
    const QModelIndex index = m_model->mapFromSource(m_files->index(m_pendingRename));
    m_pendingRename.clear();
    if (!index.isValid())
        return;
    QAbstractItemView *view = currentView();
    view->setCurrentIndex(index);
    view->scrollTo(index);
    view->edit(index);
}

void FileBrowser::renameSelected()
{
    const QStringList selected = selectedPaths();
    if (selected.size() != 1 || m_location.startsWith(QLatin1String("::")))
        return;
    m_pendingRename = selected.first();
    renamePending();
}

void FileBrowser::deleteSelected(bool permanent)
{
    // What is in the Recycle Bin can only be deleted for good.
    fileops::remove(selectedPaths(), permanent || m_location == places::RecycleBin, this);
}

void FileBrowser::showProperties()
{
    const QStringList selected = selectedPaths();
    if (m_location == places::RecycleBin && selected.isEmpty()) {
        recycleBinProperties();
    } else if (!selected.isEmpty())
        showItemProperties(selected, this);
    else if (!currentFolder().isEmpty())
        showItemProperties({currentFolder()}, this);
}

// The Recycle Bin's own property sheet: its one setting that this port has, the delete confirmation.
void FileBrowser::recycleBinProperties()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Recycle Bin Properties"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *confirm = new QCheckBox(tr("Display delete confirmation dialog"), &dialog);
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("fileops"));
    confirm->setChecked(settings.value(QStringLiteral("RecycleBin/ConfirmDelete"), true).toBool());
    layout->addWidget(confirm);
    layout->addWidget(new QLabel(tr("The Recycle Bin holds deleted files until it is emptied."), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        settings.setValue(QStringLiteral("RecycleBin/ConfirmDelete"), confirm->isChecked());
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void FileBrowser::moveOrCopyTo(bool move)
{
    const QStringList selected = selectedPaths();
    if (selected.isEmpty())
        return;
    const QString target = QFileDialog::getExistingDirectory(this, move ? tr("Move Items") : tr("Copy Items"), currentFolder());
    if (!target.isEmpty())
        fileops::transfer(selected, target, move ? fileops::Action::Move : fileops::Action::Copy, this);
}

void FileBrowser::startSearch(const SearchQuery &query)
{
    m_query = std::make_shared<SearchQuery>(query);
    m_searchFound = 0;
    clearResults(m_resultsModel);
    if (m_query->folder.isEmpty())
        m_query->folder = QDir::homePath();
    m_searchPending = {m_query->folder};
    navigate(places::Search);
    // (navigate stops the timer.)
    m_searchTimer->start(0);
}

namespace {

// Never searched: what Linux makes up, and is not files
bool neverSearched(const QString &path)
{
    return path == QLatin1String("/proc") || path == QLatin1String("/sys") || path == QLatin1String("/dev") || path == QLatin1String("/run");
}

bool systemFolder(const QString &path)
{
    static const QStringList folders{QStringLiteral("/usr"), QStringLiteral("/etc"), QStringLiteral("/boot"), QStringLiteral("/opt"), QStringLiteral("/bin"), QStringLiteral("/sbin"), QStringLiteral("/lib"), QStringLiteral("/lib64"), QStringLiteral("/var")};
    return folders.contains(path);
}

bool nameMatches(const QString &name, const QString &wanted, Qt::CaseSensitivity sensitivity)
{
    if (wanted.isEmpty())
        return true;
    // Several names, as XP takes them; a "*" or "?" is a pattern, anything else a part of the name.
    for (const QString &part : wanted.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (trimmed.contains(QLatin1Char('*')) || trimmed.contains(QLatin1Char('?'))) {
            const auto options = sensitivity == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption;
            if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(trimmed), options).match(name).hasMatch())
                return true;
        } else if (name.contains(trimmed, sensitivity)) {
            return true;
        }
    }
    return false;
}

bool fileContains(const QString &path, const QByteArray &text, Qt::CaseSensitivity sensitivity)
{
    const qint64 MOST = 16 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MOST)
        return false;
    const QByteArray data = file.readAll();
    return sensitivity == Qt::CaseSensitive ? data.contains(text) : data.toLower().contains(text.toLower());
}

} // namespace

void FileBrowser::continueSearch()
{
    // A little at a time: the window stays alive while the search runs.
    QElapsedTimer budget;
    budget.start();
    const SearchQuery &query = *m_query;
    const Qt::CaseSensitivity sensitivity = query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    while (!m_searchPending.isEmpty() && budget.elapsed() < 30) {
        const QDir folder(m_searchPending.takeFirst());
        QDir::Filters filter = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (query.hidden)
            filter |= QDir::Hidden;
        const QFileInfoList children = folder.entryInfoList(filter);
        for (const QFileInfo &child : children) {
            const QString path = child.absoluteFilePath();
            if (child.isDir() && !child.isSymLink() && query.subfolders && !neverSearched(path) && (query.system || !systemFolder(path)))
                m_searchPending.append(path);
            if (!nameMatches(child.fileName(), query.name, sensitivity))
                continue;
            if (!query.extensions.isEmpty() && !query.extensions.contains(child.suffix().toLower()))
                continue;
            if (query.minSize >= 0 && (child.isDir() || child.size() < query.minSize))
                continue;
            if (query.maxSize >= 0 && (child.isDir() || child.size() > query.maxSize))
                continue;
            const QDateTime when = query.whichDate == 2 ? child.birthTime() : query.whichDate == 3 ? child.lastRead() : child.lastModified();
            if ((query.after.isValid() && when < query.after) || (query.before.isValid() && when > query.before))
                continue;
            if (!query.text.isEmpty() && (child.isDir() || !fileContains(path, query.text.toUtf8(), sensitivity)))
                continue;
            auto *name = new QStandardItem(fileicons::forFile(child), child.fileName());
            name->setData(path, Qt::UserRole);
            m_resultsModel->appendRow({name, new QStandardItem(child.absolutePath()), new QStandardItem(child.isDir() ? QString() : xpSize(child.size())),
                                       new QStandardItem(fileicons::typeName(child)), new QStandardItem(QLocale().toString(child.lastModified(), QLocale::ShortFormat))});
            ++m_searchFound;
        }
    }
    statusBar()->showMessage(m_searchPending.isEmpty() ? tr("%n object(s) found", nullptr, m_searchFound) : tr("Searching..."));
    if (m_searchPending.isEmpty())
        finishSearch();
}

void FileBrowser::finishSearch()
{
    m_searchTimer->stop();
    m_searchPending.clear();
    setColumnChars(m_results, {30, 30, 16, 20, 20});
    m_searchCompanion->searchFinished(m_searchFound);
}

void FileBrowser::searchTheWeb(const QString &text, const QString &engine)
{
    navigate(engine + QString::fromUtf8(QUrl::toPercentEncoding(text)));
}

void FileBrowser::showWebPage(const QString &address)
{
#ifdef XP_WEB_PAGES
    if (!m_web) {
        // A link that asks for a new window stays in this one, as a folder window's does.
        class Page : public QWebEngineView
        {
        public:
            using QWebEngineView::QWebEngineView;

        protected:
            QWebEngineView *createWindow(QWebEnginePage::WebWindowType) override { return this; }
        };
        m_web = new Page(m_pages);
        m_pages->addWidget(m_web);
        connect(m_web, &QWebEngineView::titleChanged, this, [this](const QString &title) {
            if (places::isWeb(m_location) && !title.isEmpty())
                setWindowTitle(title);
        });
        connect(m_web, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
            // A link followed in the page goes through the window's history.
            const QString now = url.toString();
            if (!places::isWeb(m_location) || now == m_location || now.isEmpty())
                return;
            m_back.append(m_location);
            m_forward.clear();
            m_location = now;
            m_backButton->setEnabled(true);
            m_forwardButton->setEnabled(false);
            m_address->setItemText(0, now);
        });
    }
    m_pages->setCurrentWidget(m_web);
    if (m_web->url().toString() != address)
        m_web->load(QUrl(address));
#else
    programs::open(address);
#endif
}
