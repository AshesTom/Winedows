// SPDX-License-Identifier: LGPL-2.1-or-later
#include "filebrowser.h"

#include "controlpanel.h"
#include "fileicons.h"
#include "fileops.h"
#include "programs.h"
#include "searchcompanion.h"
#include "taskpane.h"
#include "theme.h"
#include "xpfiles.h"

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
#include <QDialogButtonBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
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
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
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
const int STRING_FILE_TASKS = 31233, STRING_NEW_FOLDER = 31236, STRING_RENAME = 31242, STRING_MOVE = 31244, STRING_COPY = 31246, STRING_DELETE = 31252;
const int STRING_OTHER_PLACES = 31272, STRING_DETAILS = 31274, STRING_SYSTEM_TASKS = 31232, STRING_SYSTEM_INFO = 31294, STRING_ADD_REMOVE = 31327, STRING_CHANGE = 31312, STRING_EMPTY = 31331;
const int STRING_HARD_DISKS = 9426, STRING_SWITCH_CLASSIC = 32150, STRING_SWITCH_CATEGORY = 32152, STRING_SEE_ALSO = 20868;

// Item data of the virtual lists, past the roles of QFileSystemModel
enum { ActionRole = Qt::UserRole + 100, HeaderRole, DetailRole };

// The rebar's art is one picture for the menu and the toolbars together, down to the folder itself.
void drawBand(QPainter &painter, QWidget *row, w4w::Renderer *renderer)
{
    auto *window = qobject_cast<QMainWindow *>(row->window());
    const int bottom = window && window->centralWidget() ? window->centralWidget()->y() : row->height();
    const int top = row->mapTo(row->window(), QPoint(0, 0)).y();
    renderer->draw(&painter, QRect(0, -top, row->width(), bottom), w4w::Part{QStringLiteral("rebar"), {}, {}, 0, {}});
}

// The menu bar as XP's: its row, the flag's white tile at its right end, and the band's two lines under
// it (the first in the style's Rebar.Band EdgeShadowColor, as along the tile's left side)
class BrandMenuBar : public QMenuBar
{
public:
    explicit BrandMenuBar(QWidget *parent)
        : QMenuBar(parent)
    {
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
        QPainter painter(this);
        const QColor shadow = edgeColour();
        const int row = height() - 2;
        if (w4w::Renderer *renderer = ShellTheme::instance()->renderer()) {
            // The band's art under the menu's names, not the style's flat colour; the item
            // under the pointer as the style draws it.
            drawBand(painter, this, renderer);
            for (QAction *action : actions()) {
                QStyleOptionMenuItem option;
                initStyleOption(&option, action);
                option.rect = actionGeometry(action);
                if (option.state & (QStyle::State_Selected | QStyle::State_Sunken))
                    style()->drawControl(QStyle::CE_MenuBarItem, &option, &painter, this);
                else
                    style()->drawItemText(&painter, option.rect, Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextSingleLine, option.palette,
                                          option.state & QStyle::State_Enabled, option.text, QPalette::ButtonText);
            }
        } else {
            QMenuBar::paintEvent(event);
        }
        painter.fillRect(QRect(0, row, width(), 1), shadow);
        painter.fillRect(QRect(0, row + 1, width(), 1), Qt::white);
        const QRect tile(width() - TILE_WIDTH, 0, TILE_WIDTH, row);
        painter.fillRect(QRect(tile.x() - 1, 0, 1, row), shadow);
        painter.fillRect(tile, Qt::white);
        painter.drawImage(tile.x() + FLAG_X, (row - m_flag.height()) / 2, m_flag);
    }

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

// A toolbar on the visual style's rebar art, as XP's folder windows have it
class RebarToolBar : public QToolBar
{
public:
    using QToolBar::QToolBar;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (w4w::Renderer *renderer = ShellTheme::instance()->renderer()) {
            QPainter painter(this);
            drawBand(painter, this, renderer);
        } else {
            QToolBar::paintEvent(event);
        }
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
    QString path(const QModelIndex &index) const { return files()->filePath(mapToSource(index)); }
};

// How a view draws an item: XP's Tiles and Thumbnails; My Computer's group headers
class ItemDelegate : public QStyledItemDelegate
{
    static constexpr int ICONS_TOP = 8;

public:
    enum Mode { Plain, IconsMode, TilesMode, ThumbnailMode };
    ItemDelegate(QListView *view, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_view(view)
    {
    }
    void setMode(Mode mode) { m_mode = mode; }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(HeaderRole).toBool())
            return QSize(qMax(100, m_view->viewport()->width() - 20), 26);
        if (m_mode == TilesMode)
            return QSize(252, 58);
        if (m_mode == ThumbnailMode)
            return QSize(120, 134);
        return QStyledItemDelegate::sizeHint(option, index);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(HeaderRole).toBool()) {
            painter->save();
            QFont bold = option.font;
            bold.setBold(true);
            painter->setFont(bold);
            painter->drawText(option.rect.adjusted(6, 0, 0, -4), Qt::AlignLeft | Qt::AlignVCenter, index.data().toString());
            QLinearGradient line(option.rect.bottomLeft(), option.rect.bottomLeft() + QPoint(option.rect.width() / 2, 0));
            line.setColorAt(0, ShellTheme::instance()->system(QStringLiteral("highlight"), Qt::darkBlue));
            line.setColorAt(1, Qt::white);
            painter->fillRect(QRect(option.rect.x() + 4, option.rect.bottom() - 2, option.rect.width() - 8, 1), line);
            painter->restore();
            return;
        }
        if (m_mode == Plain) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        if (m_mode == IconsMode) {
            // In its cell of 75 pixels, its icon 8 from the top, as XP has it
            QStyleOptionViewItem moved = option;
            moved.rect.translate(0, ICONS_TOP);
            QStyledItemDelegate::paint(painter, moved, index);
            return;
        }
        const bool selected = option.state & QStyle::State_Selected;
        const QPalette &palette = option.palette;
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QString name = index.data().toString();
        painter->save();
        if (m_mode == TilesMode) {
            const QRect iconRect(option.rect.x() + 4, option.rect.y() + 5, 48, 48);
            icon.paint(painter, iconRect, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);
            const QRect text = option.rect.adjusted(58, 5, -4, -5);
            if (selected)
                painter->fillRect(text.adjusted(-2, 0, 0, 0), palette.highlight());
            painter->setPen(selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text));
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
            int y = text.y() + qMax(0, (text.height() - int(lines.size()) * 14) / 2);
            for (int i = 0; i < lines.size(); ++i) {
                if (i == 1 && !selected)
                    painter->setPen(QColor(128, 128, 128));
                painter->drawText(QRect(text.x(), y, text.width(), 14), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  option.fontMetrics.elidedText(lines[i], Qt::ElideRight, text.width()));
                y += 14;
            }
        } else {
            const QRect box(option.rect.x() + 10, option.rect.y() + 4, 100, 100);
            painter->fillRect(box, Qt::white);
            painter->setPen(selected ? palette.color(QPalette::Highlight) : QColor(201, 199, 186));
            painter->drawRect(box.adjusted(0, 0, -1, -1));
            QPixmap thumbnail = this->thumbnail(index);
            if (!thumbnail.isNull()) {
                painter->drawPixmap(box.x() + (box.width() - thumbnail.width()) / 2, box.y() + (box.height() - thumbnail.height()) / 2, thumbnail);
            } else {
                icon.paint(painter, box.adjusted(26, 26, -26, -26), Qt::AlignCenter);
            }
            const QRect text(option.rect.x() + 2, box.bottom() + 4, option.rect.width() - 4, 18);
            if (selected)
                painter->fillRect(text, palette.highlight());
            painter->setPen(selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text));
            painter->drawText(text, Qt::AlignCenter | Qt::TextSingleLine, option.fontMetrics.elidedText(name, Qt::ElideRight, text.width()));
        }
        painter->restore();
    }

private:
    QPixmap thumbnail(const QModelIndex &index) const
    {
        const auto *model = dynamic_cast<const FolderModelImpl *>(index.model());
        if (!model)
            return {};
        const QString path = model->path(index);
        const QString key = path + QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch());
        if (m_thumbnails.contains(key))
            return m_thumbnails[key];
        QPixmap pixmap;
        QImageReader reader(path);
        // Only what the reader can scale as it decodes, and not huge files: this is the paint itself.
        if (!QFileInfo(path).isDir() && reader.canRead() && QFileInfo(path).size() < 32 << 20) {
            const QSize size = reader.size();
            if (size.isValid()) {
                reader.setScaledSize(size.scaled(QSize(92, 92), Qt::KeepAspectRatio).boundedTo(size));
                pixmap = QPixmap::fromImage(reader.read());
            }
        }
        if (m_thumbnails.size() > 500)
            m_thumbnails.clear();
        m_thumbnails.insert(key, pixmap);
        return pixmap;
    }

    QListView *m_view;
    Mode m_mode = Plain;
    mutable QHash<QString, QPixmap> m_thumbnails;
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
    resize(ShellTheme::instance()->scaled(640), ShellTheme::instance()->scaled(480));
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_view = View(qBound(0, settings.value(QStringLiteral("view"), int(Tiles)).toInt(), int(Details)));
    m_foldersShown = settings.value(QStringLiteral("folders"), false).toBool();

    m_files = new QFileSystemModel(this);
    m_files->setIconProvider(nullptr);
    m_files->setReadOnly(false);
    m_files->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
    m_files->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model = new FolderModel(this);
    m_model->setSourceModel(m_files);
    m_virtual = new QStandardItemModel(this);
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
        auto *view = new QListView(m_pages);
        view->setModel(model);
        view->setItemDelegate(new ItemDelegate(view, view));
        view->setResizeMode(QListView::Adjust);
        view->setUniformItemSizes(false);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->setFrameShape(QFrame::NoFrame);
        view->setWordWrap(true);
        view->setEditTriggers(QAbstractItemView::EditKeyPressed);
        view->viewport()->installEventFilter(this);
        view->installEventFilter(this);
        connect(view, &QAbstractItemView::customContextMenuRequested, this, [this, view](QPoint at) {
            showContextMenu(view->viewport()->mapToGlobal(at), view->indexAt(at).isValid());
        });
        connect(view, &QAbstractItemView::activated, this, &FileBrowser::activate);
        return view;
    };
    m_list = listView(m_model);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_pages->addWidget(m_list);
    m_details = new QTreeView(m_pages);
    m_details->setModel(m_model);
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
    m_details->setColumnWidth(0, 220);
    connect(m_details, &QAbstractItemView::customContextMenuRequested, this, [this](QPoint at) {
        showContextMenu(m_details->viewport()->mapToGlobal(at), m_details->indexAt(at).isValid());
    });
    connect(m_details, &QAbstractItemView::activated, this, &FileBrowser::activate);
    m_pages->addWidget(m_details);
    m_virtualList = listView(m_virtual);
    m_virtualList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_virtualList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pages->addWidget(m_virtualList);
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
        showContextMenu(m_results->viewport()->mapToGlobal(at), m_results->indexAt(at).isValid());
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

    for (QItemSelectionModel *selection : {m_list->selectionModel(), m_virtualList->selectionModel()})
        connect(selection, &QItemSelectionModel::selectionChanged, this, [this] {
            updatePane();
            updateActions();
            updateStatus();
        });

    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_pages);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({ShellTheme::instance()->scaled(210), width()});
    setCentralWidget(m_splitter);
    connect(m_files, &QFileSystemModel::directoryLoaded, this, &FileBrowser::updateStatus);
}

void FileBrowser::buildMenus()
{
    setMenuBar(new BrandMenuBar(this));
    QMenu *file = menuBar()->addMenu(tr("&File"));
    m_newFolderAction = file->addAction(tr("New &Folder"), this, &FileBrowser::newFolder);
    m_newTextAction = file->addAction(tr("New &Text Document"), this, &FileBrowser::newTextDocument);
    file->addSeparator();
    file->addAction(tr("Create &Shortcut"), this, [this] {
        for (const QString &path : selectedPaths()) {
            const QFileInfo info(path);
            QFile::link(path, info.absolutePath() + QLatin1Char('/') + tr("Shortcut to %1").arg(info.fileName()));
        }
    });
    m_deleteAction = file->addAction(tr("&Delete"), this, [this] { deleteSelected(false); });
    m_renameAction = file->addAction(tr("Rena&me"), this, &FileBrowser::renameSelected);
    m_propertiesAction = file->addAction(tr("P&roperties"), this, &FileBrowser::showProperties);
    file->addSeparator();
    file->addAction(tr("&Close"), this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo"), QKeySequence::Undo)->setEnabled(false);
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
        {QT_TR_NOOP("T&humbnails"), Thumbnails}, {QT_TR_NOOP("Tile&s"), Tiles}, {QT_TR_NOOP("Ico&ns"), Icons}, {QT_TR_NOOP("&List"), List}, {QT_TR_NOOP("&Details"), Details}};
    for (const auto &mode : modes) {
        QAction *action = m_viewGroup->addAction(tr(mode.name));
        action->setCheckable(true);
        action->setData(int(mode.mode));
        view->addAction(action);
        m_viewsMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, target = mode.mode] { setView(target); });
    }
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
    help->addAction(tr("&About Windows"), this, [this] {
        QMessageBox::about(this, tr("About Windows"), tr("The Windows XP shell, ported to Linux.\nIts pictures and words come from your own copy of Windows XP."));
    });
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
                QFile::link(m_location, folder + QLatin1Char('/') + name);
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
    auto *toolbar = new RebarToolBar(tr("Standard Buttons"), this);
    addToolBar(toolbar);
    toolbar->setMovable(false);
    toolbar->setContentsMargins(4, 0, 0, 0);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const auto historyButton = [&](int icon, const QString &text, bool back) {
        auto *button = new QToolButton(toolbar);
        button->setIcon(stripIcon(icon));
        button->setText(text);
        button->setToolTip(text);
        button->setAutoRaise(true);
        button->setToolButtonStyle(back ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        button->setPopupMode(QToolButton::MenuButtonPopup);
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
    m_upAction = toolbar->addAction(stripIcon(UP), tr("Up"), this, [this] {
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
    toolbar->addSeparator();
    const auto withText = [&](QAction *action) {
        toolbar->addAction(action);
        if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action)))
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    };
    withText(m_searchAction);
    withText(m_foldersAction);
    toolbar->addSeparator();
    auto *views = new QToolButton(toolbar);
    views->setIcon(stripIcon(VIEWS));
    views->setToolTip(tr("Views"));
    views->setAutoRaise(true);
    views->setMenu(m_viewsMenu);
    views->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(views);

    addToolBarBreak();
    auto *addressBar = new RebarToolBar(tr("Address Bar"), this);
    addToolBar(addressBar);
    addressBar->setMovable(false);
    toolbar->toggleViewAction()->setText(tr("&Standard Buttons"));
    addressBar->toggleViewAction()->setText(tr("&Address Bar"));
    m_toolbarsMenu->addAction(toolbar->toggleViewAction());
    m_toolbarsMenu->addAction(addressBar->toggleViewAction());
    // One widget for the whole row: a toolbar folds away a stretching child of its own.
    auto *row = new QWidget(addressBar);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 1, 2, 1);
    rowLayout->setSpacing(4);
    auto *addressLabel = new QLabel(tr("Address"), row);
    addressLabel->setForegroundRole(QPalette::PlaceholderText);
    addressLabel->setPalette([&] { QPalette palette = addressLabel->palette(); palette.setColor(QPalette::PlaceholderText, ShellTheme::instance()->system(QStringLiteral("graytext"), QColor(128, 128, 128))); return palette; }());
    rowLayout->addWidget(addressLabel);
    m_address = new QComboBox(row);
    m_address->setEditable(true);
    m_address->setInsertPolicy(QComboBox::NoInsert);
    m_address->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_address->setMinimumContentsLength(10);
    rowLayout->addWidget(m_address, 1);
    auto *goButton = new QToolButton(row);
    goButton->setAutoRaise(true);
    goButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    goButton->setIconSize(QSize(16, 16));
    rowLayout->addWidget(goButton);
    addressBar->addWidget(row);
    auto *go = new QAction(fileicons::shell(fileicons::Go), tr("Go"), this);
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

void FileBrowser::setView(View view)
{
    m_view = view;
    for (QAction *action : m_viewGroup->actions())
        action->setChecked(action->data().toInt() == int(view));
    auto *delegate = delegateOf(m_list);
    switch (view) {
    case Thumbnails:
        delegate->setMode(ItemDelegate::ThumbnailMode);
        m_list->setViewMode(QListView::IconMode);
        m_list->setGridSize(QSize(120, 134));
        m_list->setIconSize(QSize(96, 96));
        break;
    case Tiles:
        delegate->setMode(ItemDelegate::TilesMode);
        m_list->setViewMode(QListView::IconMode);
        m_list->setGridSize(QSize(256, 62));
        m_list->setIconSize(QSize(48, 48));
        break;
    case Icons:
        delegate->setMode(ItemDelegate::IconsMode);
        m_list->setViewMode(QListView::IconMode);
        m_list->setGridSize(QSize(75, 75));
        m_list->setIconSize(QSize(32, 32));
        break;
    case List:
        delegate->setMode(ItemDelegate::Plain);
        m_list->setViewMode(QListView::ListMode);
        m_list->setFlow(QListView::TopToBottom);
        m_list->setWrapping(true);
        m_list->setGridSize(QSize(180, 18));
        m_list->setIconSize(QSize(16, 16));
        break;
    case Details:
        break;
    }
    // IconMode lets items be dragged about; here a drag is a file's. (And a flow set for List stays.)
    if (view != List) {
        m_list->setFlow(QListView::LeftToRight);
        m_list->setMovement(QListView::Static);
        m_list->setWrapping(true);
    }
    if (!m_location.startsWith(QLatin1String("::")) && !blocked())
        m_pages->setCurrentWidget(view == Details ? static_cast<QWidget *>(m_details) : m_list);
    m_list->doItemsLayout();
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
    if (view == m_virtualList)
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
            m_pages->setCurrentWidget(m_view == Details ? static_cast<QWidget *>(m_details) : m_list);
        }
    } else {
        showVirtual(target);
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
    m_virtual->appendRow(item);
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
    }
    if (!removable.isEmpty()) {
        addVirtualHeader(tr("Devices with Removable Storage"));
        for (const QStorageInfo &drive : std::as_const(removable)) {
            const QString path = drive.rootPath();
            addVirtual(drive.displayName(), fileicons::shell(fileicons::Removable), [this, path] { navigate(path); },
                       tr("%1 free of %2").arg(xpSize(drive.bytesAvailable()), xpSize(drive.bytesTotal())));
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
    m_virtual->clear();
    m_actions.clear();
    auto *delegate = delegateOf(m_virtualList);
    m_virtualList->setViewMode(QListView::IconMode);
    m_virtualList->setMovement(QListView::Static);
    m_virtualList->setWrapping(true);
    m_virtualList->setGridSize(QSize());
    m_virtualList->setSpacing(2);
    m_virtualList->setIconSize(QSize(48, 48));
    delegate->setMode(ItemDelegate::TilesMode);
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
    programs::open(path);
}

void FileBrowser::activate(const QModelIndex &index)
{
    if (index.model() == m_virtual) {
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
    m_propertiesAction->setEnabled(some || files);
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
    if (m_location == places::Computer) {
        groups.append({xpfiles::shellString(STRING_SYSTEM_TASKS, "System Tasks"), true, {
            {xpfiles::shellString(STRING_SYSTEM_INFO, "View system information"), fileicons::shell(fileicons::Computer), [] { controlpanel::open(QStringLiteral("system")); }},
            {xpfiles::shellString(STRING_ADD_REMOVE, "Add or remove programs"), fileicons::shell(fileicons::Application), [] { controlpanel::open(QStringLiteral("programs")); }},
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
            {drive ? xpfiles::shellString(31319, "Show the contents of this drive") : xpfiles::shellString(31323, "Show the contents of this folder"), fileicons::shell(fileicons::FolderOpen), [this] { setSystemFolderShown(true); }},
            {xpfiles::shellString(STRING_ADD_REMOVE, "Add or remove programs"), fileicons::shell(fileicons::Application), [] { controlpanel::open(QStringLiteral("programs")); }},
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
                {drive ? xpfiles::shellString(31321, "Hide the contents of this drive") : xpfiles::shellString(31325, "Hide the contents of this folder"), fileicons::shell(fileicons::FolderOpen), [this] { setSystemFolderShown(false); }},
            }});
        }
        PaneGroup tasks{xpfiles::shellString(STRING_FILE_TASKS, "File and Folder Tasks"), !isSystemFolder(m_location), {}};
        if (selected.isEmpty()) {
            tasks.tasks.append({xpfiles::shellString(STRING_NEW_FOLDER, "Make a new folder"), fileicons::shell(fileicons::FolderOpen), [this] { newFolder(); }});
        } else {
            // XP's words for what is selected: a file, a folder or several items
            const bool several = selected.size() > 1, folder = !several && QFileInfo(selected.first()).isDir();
            const auto words = [&](int fileId, const char *fileText, int folderId, const char *folderText, int manyId, const char *manyText) {
                return several ? xpfiles::shellString(manyId, manyText) : folder ? xpfiles::shellString(folderId, folderText) : xpfiles::shellString(fileId, fileText);
            };
            if (!several)
                tasks.tasks.append({folder ? xpfiles::shellString(31254, "Rename this folder") : xpfiles::shellString(STRING_RENAME, "Rename this file"), fileicons::shell(fileicons::Document), [this] { renameSelected(); }});
            tasks.tasks.append({words(STRING_MOVE, "Move this file", 31256, "Move this folder", 31264, "Move the selected items"), stripIcon(MOVE_TO), [this] { moveOrCopyTo(true); }});
            tasks.tasks.append({words(STRING_COPY, "Copy this file", 31258, "Copy this folder", 31266, "Copy the selected items"), stripIcon(COPY_TO), [this] { moveOrCopyTo(false); }});
            tasks.tasks.append({words(STRING_DELETE, "Delete this file", 31262, "Delete this folder", 31270, "Delete the selected items"), stripIcon(DELETE_ICON), [this] { deleteSelected(false); }});
        }
        groups.append(tasks);
        const QFileInfo info(m_location);
        if (!info.isRoot())
            others.tasks.append(place(info.absolutePath()));
        if (m_location != documents)
            others.tasks.append(place(documents));
        others.tasks.append(place(places::Computer));
        const QFileInfo shown(selected.size() == 1 ? selected.first() : m_location);
        details.tasks.append({selected.size() > 1 ? tr("%1 items selected.").arg(selected.size()) : places::title(shown.absoluteFilePath()), {}, {}, true});
        if (selected.size() <= 1) {
            details.tasks.append({fileicons::typeName(shown), {}, {}});
            details.tasks.append({tr("Date Modified: %1").arg(QLocale().toString(shown.lastModified(), QLocale::ShortFormat)), {}, {}});
            if (!shown.isDir())
                details.tasks.append({tr("Size: %1").arg(xpSize(shown.size())), {}, {}});
        }
    }
    if (!others.tasks.isEmpty())
        groups.append(others);
    if (!details.tasks.isEmpty())
        groups.append(details);
    m_pane->setGroups(groups);
}

bool FileBrowser::eventFilter(QObject *watched, QEvent *event)
{
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

void FileBrowser::showContextMenu(QPoint global, bool onItem)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString folder = currentFolder();
    if (onItem && m_location == places::RecycleBin) {
        menu->addAction(tr("R&estore"), this, [this] { fileops::restoreFromTrash(selectedPaths(), this); });
        menu->addSeparator();
        menu->addAction(tr("&Delete"), this, [this] { deleteSelected(true); });
    } else if (onItem && !folder.isEmpty()) {
        const QStringList selected = selectedPaths();
        QAction *open = menu->addAction(tr("&Open"), this, [this] {
            const QStringList paths = selectedPaths();
            if (!paths.isEmpty())
                openPath(paths.first());
        });
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        if (selected.size() == 1 && QFileInfo(selected.first()).isDir())
            menu->addAction(tr("Open in New &Window"), this, [path = selected.first()] { auto *window = new FileBrowser(path); window->show(); });
        menu->addSeparator();
        menu->addAction(m_cutAction);
        menu->addAction(m_copyAction);
        menu->addSeparator();
        menu->addAction(tr("&Delete"), this, [this] { deleteSelected(false); });
        menu->addAction(m_renameAction);
        menu->addSeparator();
        menu->addAction(m_propertiesAction);
    } else if (!folder.isEmpty() && m_location != places::RecycleBin) {
        menu->addMenu(m_viewsMenu)->setText(tr("&View"));
        menu->addAction(tr("R&efresh"), this, [this] { navigate(m_location, false); });
        menu->addSeparator();
        menu->addAction(m_pasteAction);
        menu->addSeparator();
        QMenu *create = menu->addMenu(tr("Ne&w"));
        create->addAction(fileicons::shell(fileicons::Folder), tr("&Folder"), this, &FileBrowser::newFolder);
        create->addAction(fileicons::shell(fileicons::Document), tr("&Text Document"), this, &FileBrowser::newTextDocument);
        menu->addSeparator();
        menu->addAction(m_propertiesAction);
    } else {
        menu->addAction(tr("R&efresh"), this, [this] { navigate(m_location, false); });
    }
    menu->popup(global);
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
    if (!selected.isEmpty())
        showItemProperties(selected, this);
    else if (!currentFolder().isEmpty())
        showItemProperties({currentFolder()}, this);
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
    m_results->resizeColumnToContents(0);
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
