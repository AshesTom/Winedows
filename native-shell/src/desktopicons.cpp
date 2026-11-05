// SPDX-License-Identifier: LGPL-2.1-or-later
#include "desktopicons.h"

#include "fileicons.h"

#include "displayproperties.h"
#include "filebrowser.h"
#include "fileproperties.h"
#include "pictures.h"
#include "programs.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"
#include "xpprogram.h"
#include "shellmenus.h"
#include "controlpanel.h"
#include "fileops.h"

#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMimeDatabase>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTextLayout>
#include <QWidget>

namespace {

// XP's icon spacing at 96 DPI
const int CELL_WIDTH = 75, CELL_HEIGHT = 75, ICON_SIZE = 32, ICON_TOP = 2, TEXT_GAP = 5, TEXT_WIDTH = 73;
const int SHELL_RECYCLE_BIN = 8964;
const int ICON_FOLDER = 4, ICON_DOCUMENT = 1, ICON_TRASH_EMPTY = 32, ICON_TRASH_FULL = 33;
const char TRASH_KEY[] = "::recycle-bin";
// XP's Desktop Items: their keys, shell32's names (My Documents, My Computer, My Network Places) and icons
struct DesktopItemInfo
{
    DesktopItem item;
    const char *key, *setting;
    int name, icon;
};
const DesktopItemInfo DESKTOP_ITEMS[] = {
    {DesktopItem::Documents, "::my-documents", "MyDocuments", 9227, 235},
    {DesktopItem::Computer, "::my-computer", "MyComputer", 9216, 16},
    {DesktopItem::Network, "::my-network-places", "MyNetworkPlaces", 9217, 18},
    {DesktopItem::Internet, "::internet", "Internet", 9222, 0},
};

QString explorerSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

QString placesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-desktop.conf");
}

// The desktop's choices of its shortcut menu (Arrange Icons By, Auto Arrange, Show Desktop Icons)
template<typename T>
T desktopSetting(const QString &name, T fallback)
{
    return QSettings(explorerSettingsPath(), QSettings::IniFormat).value(QStringLiteral("desktop/") + name, fallback).template value<T>();
}

void setDesktopSetting(const QString &name, const QVariant &value)
{
    QSettings(explorerSettingsPath(), QSettings::IniFormat).setValue(QStringLiteral("desktop/") + name, value);
}

// XP keeps HideDesktopIcons per Start menu mode: Explorer\Advanced\HideDesktopIcons\NewStartPanel or
// \ClassicStartMenu, so Show Desktop Icons changed with the classic menu leaves the Start panel's icons
// as they were.
QString hideIconsSetting()
{
    const bool classic = QSettings(explorerSettingsPath(), QSettings::IniFormat)
                             .value(QStringLiteral("StartMenu/Classic"), false).toBool();
    return classic ? QStringLiteral("hideIconsClassic") : QStringLiteral("hideIcons");
}

bool desktopIconsHidden()
{
    return desktopSetting(hideIconsSetting(), false);
}

void setDesktopIconsHidden(bool hidden)
{
    setDesktopSetting(hideIconsSetting(), hidden);
}

QString desktopFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

QString trashFiles()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

// The settings' key of a place: slashes would make subgroups.
QString placeKey(const QString &path)
{
    return QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}

} // namespace

QString desktopIconKey(DesktopItem item)
{
    switch (item) {
    case DesktopItem::Documents: return QStringLiteral("MyDocuments");
    case DesktopItem::Computer: return QStringLiteral("MyComputer");
    case DesktopItem::Network: return QStringLiteral("MyNetworkPlaces");
    case DesktopItem::Internet: return QStringLiteral("Internet");
    }
    return QString();
}

QImage iconOverride(const QString &key, int size)
{
    const QString value = QSettings(explorerSettingsPath(), QSettings::IniFormat).value(QStringLiteral("desktop/icon.") + key).toString();
    const qsizetype comma = value.lastIndexOf(QLatin1Char(','));
    if (comma <= 0)
        return QImage();
    const QString file = value.left(comma), group = value.mid(comma + 1);
    if (const auto pe = xpfiles::resources(xpfiles::find(file)))
        return xpfiles::icon(*pe, group, size);
    return QImage();
}

QImage desktopItemIcon(DesktopItem item, int size)
{
    const QImage custom = iconOverride(desktopIconKey(item), size);
    if (!custom.isNull())
        return custom;
    for (const DesktopItemInfo &info : DESKTOP_ITEMS)
        if (info.item == item)
            return ShellTheme::instance()->shellIcon(info.icon, size);
    return QImage();
}

QImage recycleBinIcon(bool full, int size)
{
    const QImage custom = iconOverride(full ? QStringLiteral("RecycleBinFull") : QStringLiteral("RecycleBinEmpty"), size);
    if (!custom.isNull())
        return custom;
    return ShellTheme::instance()->shellIcon(full ? ICON_TRASH_FULL : ICON_TRASH_EMPTY, size);
}

void setDesktopIconOverride(const QString &key, const QString &file, const QString &group)
{
    QSettings(explorerSettingsPath(), QSettings::IniFormat).setValue(QStringLiteral("desktop/icon.") + key, file + QLatin1Char(',') + group);
}

void clearDesktopIconOverride(const QString &key)
{
    QSettings(explorerSettingsPath(), QSettings::IniFormat).remove(QStringLiteral("desktop/icon.") + key);
}

bool desktopItemShown(DesktopItem item)
{
    for (const DesktopItemInfo &info : DESKTOP_ITEMS)
        if (info.item == item)
            return QSettings(explorerSettingsPath(), QSettings::IniFormat).value(QStringLiteral("desktop/") + QLatin1String(info.setting), false).toBool();
    return false;
}

void setDesktopItemShown(DesktopItem item, bool shown)
{
    for (const DesktopItemInfo &info : DESKTOP_ITEMS)
        if (info.item == item)
            QSettings(explorerSettingsPath(), QSettings::IniFormat).setValue(QStringLiteral("desktop/") + QLatin1String(info.setting), shown);
}

DesktopIcons::DesktopIcons(QWidget *desktop)
    : QObject(desktop)
    , m_desktop(desktop)
{
    QDir().mkpath(desktopFolder());
    m_watcher.addPath(desktopFolder());
    // The Recycle Bin's icon follows it, from before its first file.
    QDir().mkpath(trashFiles());
    m_watcher.addPath(trashFiles());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &DesktopIcons::reload);
    // Customize Desktop's choices, which QSettings writes as a new file each time
    m_watcher.addPath(QFileInfo(explorerSettingsPath()).absolutePath());
    reload();
}

QSize DesktopIcons::cell() const
{
    ShellTheme *theme = ShellTheme::instance();
    return QSize(theme->scaled(CELL_WIDTH), theme->scaled(CELL_HEIGHT));
}

QRect DesktopIcons::workArea() const
{
    const QList<quint32> area = x11::property32(x11::root(), x11::atom("_NET_WORKAREA"));
    QRect out = QGuiApplication::primaryScreen()->geometry();
    if (area.size() >= 4)
        out = out.intersected(QRect(int(area[0]), int(area[1]), int(area[2]), int(area[3])));
    return out;
}

QImage DesktopIcons::iconFor(const QString &path) const
{
    ShellTheme *theme = ShellTheme::instance();
    const int size = theme->scaled(ICON_SIZE);
    const QFileInfo info(path);
    if (info.isDir())
        return theme->shellIcon(ICON_FOLDER, size);
    // A program's own icon, XP's for its file types (as folder windows show them), else the icon theme's
    const QIcon icon = fileicons::forFile(info);
    if (!icon.isNull())
        return icon.pixmap(size, size).toImage();
    return theme->shellIcon(ICON_DOCUMENT, size);
}

QPoint DesktopIcons::freeCell(const QList<Item> &taken) const
{
    // Down the columns, from the top left, as XP lays icons out
    const QRect area = workArea();
    const QSize size = cell();
    for (int x = area.x(); x + size.width() <= area.right() + 1; x += size.width())
        for (int y = area.y(); y + size.height() <= area.bottom() + 1; y += size.height()) {
            const QRect wanted(QPoint(x, y), size);
            const bool used = std::any_of(taken.cbegin(), taken.cend(), [&](const Item &item) {
                return QRect(item.place, size).intersects(wanted);
            });
            if (!used)
                return QPoint(x, y);
        }
    return area.topLeft();
}

void DesktopIcons::reload()
{
    const QString selected = m_selected >= 0 && m_selected < m_items.size() ? m_items[m_selected].path : QString();
    QSettings places(placesPath(), QSettings::IniFormat);
    QList<Item> items;
    ShellTheme *theme = ShellTheme::instance();

    // Recycle Bin, in XP's lower right corner until it is moved
    Item bin;
    bin.path = QLatin1String(TRASH_KEY);
    bin.trash = true;
    const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    bin.name = shell32 ? xpfiles::string(*shell32, SHELL_RECYCLE_BIN) : QString();
    if (bin.name.isEmpty())
        bin.name = QStringLiteral("Recycle Bin");
    const bool full = !QDir(trashFiles()).isEmpty();
    bin.icon = recycleBinIcon(full, theme->scaled(ICON_SIZE));
    const QRect area = workArea();
    // XP's corner: its Recycle Bin sits 10 pixels left of the last cell's left and 7 higher than the
    // native one (measured on XP in Wine, which also lays the cells 82 pixels apart vertically).
    const QPoint binCorner(area.right() + 1 - cell().width() - theme->scaled(10), area.bottom() + 1 - cell().height() - theme->scaled(7));
    bin.place = places.value(placeKey(bin.path), binCorner).toPoint();
    items.append(bin);

    // XP's Desktop Items, first down the left column as XP puts them
    for (const DesktopItemInfo &info : DESKTOP_ITEMS) {
        if (!desktopItemShown(info.item))
            continue;
        Item item;
        item.path = QLatin1String(info.key);
        item.shell = true;
        item.name = shell32 ? xpfiles::string(*shell32, info.name) : QString();
        item.icon = info.item == DesktopItem::Internet ? QImage() : desktopItemIcon(info.item, theme->scaled(ICON_SIZE));
        if (info.item == DesktopItem::Internet) {
            // The default browser stands for Internet Explorer: its name and icon
            if (const auto browser = programs::defaultBrowser()) {
                item.name = browser->name;
                item.icon = programs::iconOf(*browser).pixmap(theme->scaled(ICON_SIZE), theme->scaled(ICON_SIZE)).toImage();
            }
        }
        const QVariant saved = places.value(placeKey(item.path));
        item.place = saved.isValid() ? saved.toPoint() : QPoint(-1, -1);
        items.append(item);
    }

    QList<QFileInfo> pending;
    // in the order Arrange Icons By chose: Name, Size, Type, Date Modified
    const QDir::SortFlags orders[] = {QDir::Name, QDir::Size | QDir::Reversed, QDir::Type, QDir::Time | QDir::Reversed};
    const int order = qBound(0, desktopSetting(QStringLiteral("order"), 0), 3);
    const bool automatic = desktopSetting(QStringLiteral("autoArrange"), false);
    const QFileInfoList files = QDir(desktopFolder()).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                                    QDir::DirsFirst | orders[order] | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        Item item;
        item.path = file.absoluteFilePath();
        item.name = file.fileName();
        if (file.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(item.path); program && !program->name.isEmpty())
                item.name = program->name;
        }
        item.icon = iconFor(item.path);
        // Auto Arrange keeps them in order, wherever they were put
        const QVariant saved = automatic ? QVariant() : places.value(placeKey(item.path));
        if (saved.isValid()) {
            item.place = saved.toPoint();
            items.append(item);
        } else {
            // Those without a place take the free cells once the others are placed.
            item.place = QPoint(-1, -1);
            items.append(item);
        }
    }
    QList<Item> placed;
    for (const Item &item : std::as_const(items))
        if (item.place != QPoint(-1, -1))
            placed.append(item);
    for (Item &item : items)
        if (item.place == QPoint(-1, -1)) {
            item.place = freeCell(placed);
            placed.append(item);
        }
    m_items = items;
    m_selected = -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].path == selected)
            m_selected = i;
    m_desktop->update();
}

QRect DesktopIcons::iconRect(const Item &item) const
{
    ShellTheme *theme = ShellTheme::instance();
    const int size = theme->scaled(ICON_SIZE);
    return QRect(item.place.x() + (cell().width() - size) / 2, item.place.y() + theme->scaled(ICON_TOP), size, size);
}

QStringList DesktopIcons::nameLines(const QString &name) const
{
    // Two lines at most, the second with an ellipsis if it does not fit
    ShellTheme *theme = ShellTheme::instance();
    const QFont font = theme->systemFont(QStringLiteral("icontitlefont"));
    const int width = theme->scaled(TEXT_WIDTH);
    QTextLayout layout(name, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    QStringList lines;
    while (lines.size() < 2) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        const QString text = name.mid(line.textStart(), line.textLength()).trimmed();
        lines.append(text);
        if (lines.size() == 2 && line.textStart() + line.textLength() < name.size())
            lines.last() = QFontMetrics(font).elidedText(name.mid(line.textStart()), Qt::ElideRight, width);
    }
    layout.endLayout();
    return lines;
}

QRect DesktopIcons::textRect(const Item &item, const QStringList &lines) const
{
    ShellTheme *theme = ShellTheme::instance();
    QFontMetrics metrics(theme->systemFont(QStringLiteral("icontitlefont")));
    int width = 0;
    for (const QString &line : lines)
        width = qMax(width, metrics.horizontalAdvance(line));
    const int height = int(lines.size()) * metrics.height();
    const int top = iconRect(item).bottom() + 1 + theme->scaled(TEXT_GAP);
    return QRect(item.place.x() + (cell().width() - width) / 2 - 1, top, width + 2, height);
}

void DesktopIcons::paint(QPainter &painter, const QRect &clip) const
{
    // Show Desktop Icons unchecked
    if (desktopIconsHidden())
        return;
    ShellTheme *theme = ShellTheme::instance();
    const QFont font = theme->systemFont(QStringLiteral("icontitlefont"));
    painter.setFont(font);
    const QColor highlight = theme->system(QStringLiteral("highlight"), QColor(49, 106, 197));
    QFontMetrics metrics(font);
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        const QRect whole = QRect(item.place, cell());
        if (!whole.intersects(clip))
            continue;
        const QRect icon = iconRect(item);
        const bool selected = i == m_selected;
        if (selected) {
            // XP blends a selected icon with the highlight colour.
            QImage tinted = item.icon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter tint(&tinted);
            tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            QColor over = highlight;
            over.setAlpha(128);
            tint.fillRect(tinted.rect(), over);
            tint.end();
            painter.drawImage(icon.topLeft(), tinted);
        } else {
            painter.drawImage(icon.topLeft(), item.icon);
        }
        const QStringList lines = nameLines(item.name);
        const QRect text = textRect(item, lines);
        if (selected)
            painter.fillRect(text, highlight);
        if (!selected) {
            // XP's desktop text on the wallpaper: comctl32's listview draws the whole label with DrawShadowText,
            // white on a blurred black shadow 2 right and 2 down (call at 0x7741a8de)
            const QRect block(item.place.x(), text.y(), cell().width(), int(lines.size()) * metrics.height());
            ShellTheme::drawShadowedText(painter, block, Qt::AlignHCenter | Qt::AlignTop, lines.join(QLatin1Char('\n')), Qt::white, Qt::black, QPoint(2, 2));
            continue;
        }
        for (int line = 0; line < lines.size(); ++line) {
            const QRect row(item.place.x(), text.y() + line * metrics.height(), cell().width(), metrics.height());
            painter.setPen(Qt::white);
            painter.drawText(row, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, lines[line]);
        }
    }
}

int DesktopIcons::itemAt(QPoint at) const
{
    if (desktopIconsHidden())
        return -1;
    for (int i = int(m_items.size()) - 1; i >= 0; --i) {
        const Item &item = m_items[i];
        if (iconRect(item).contains(at) || textRect(item, nameLines(item.name)).contains(at))
            return i;
    }
    return -1;
}

void DesktopIcons::press(QPoint at, Qt::MouseButton button, QPoint global)
{
    const int index = itemAt(at);
    if (index != m_selected) {
        m_selected = index;
        m_desktop->update();
    }
    if (button == Qt::RightButton) {
        showMenu(index, global);
        return;
    }
    if (button == Qt::LeftButton && index >= 0) {
        m_dragged = index;
        m_pressAt = at;
        m_pressPlace = m_items[index].place;
        m_moved = false;
    }
}

void DesktopIcons::move(QPoint at)
{
    if (m_dragged < 0 || m_dragged >= m_items.size())
        return;
    if (!m_moved && (at - m_pressAt).manhattanLength() < 4)
        return;
    m_moved = true;
    m_items[m_dragged].place = m_pressPlace + at - m_pressAt;
    m_desktop->update();
}

void DesktopIcons::release()
{
    if (m_moved && m_dragged >= 0 && m_dragged < m_items.size())
        savePlace(m_items[m_dragged]);
    // an auto-arranged desktop puts a moved icon back in its place
    if (m_moved && desktopSetting(QStringLiteral("autoArrange"), false))
        arrange();
    m_dragged = -1;
    m_moved = false;
}

void DesktopIcons::savePlace(const Item &item)
{
    QSettings places(placesPath(), QSettings::IniFormat);
    places.setValue(placeKey(item.path), item.place);
}

void DesktopIcons::doubleClick(QPoint at)
{
    const int index = itemAt(at);
    if (index >= 0)
        open(m_items[index]);
}

void DesktopIcons::open(const Item &item)
{
    if (item.trash) {
        programs::open(QStringLiteral("trash:///"));
        return;
    }
    if (item.shell) {
        if (item.path == QLatin1String("::my-documents"))
            FileBrowser::open(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        else if (item.path == QLatin1String("::my-computer"))
            FileBrowser::open(places::Computer);
        else if (item.path == QLatin1String("::my-network-places"))
            FileBrowser::open(places::Network);
        else if (const auto browser = programs::defaultBrowser())
            programs::launch(*browser);
        return;
    }
    if (QFileInfo(item.path).suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path)) {
            programs::launch(*program);
            return;
        }
    }
    if (pictures::isPicture(item.path))
        pictures::Viewer::open(item.path);
    else
        programs::open(item.path);
}

bool DesktopIcons::key(int key, Qt::KeyboardModifiers modifiers)
{
    if (m_selected < 0 || m_selected >= m_items.size())
        return false;
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        open(m_items[m_selected]);
    else if (key == Qt::Key_Delete)
        trash(m_selected, modifiers & Qt::ShiftModifier);
    else if (key == Qt::Key_F2)
        rename(m_selected);
    else
        return false;
    return true;
}

void DesktopIcons::rename(int index)
{
    const Item item = m_items[index];
    if (item.trash || item.shell)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(m_desktop, tr("Rename"), tr("Name:"), QLineEdit::Normal, QFileInfo(item.path).fileName(), &ok);
    if (!ok || name.isEmpty() || name.contains(QLatin1Char('/')))
        return;
    const QString target = QFileInfo(item.path).absoluteDir().filePath(name);
    if (QFile::rename(item.path, target)) {
        fileops::recordRename(item.path, target);
        QSettings places(placesPath(), QSettings::IniFormat);
        places.remove(placeKey(item.path));
        places.setValue(placeKey(target), item.place);
    }
}

// The desktop's Delete: the shell's, which asks, shows its progress and can be undone; Shift for good
void DesktopIcons::trash(int index, bool permanent)
{
    const Item item = m_items[index];
    if (!item.trash && !item.shell)
        fileops::remove({item.path}, permanent, m_desktop);
}

void DesktopIcons::arrange()
{
    QSettings places(placesPath(), QSettings::IniFormat);
    for (const Item &item : std::as_const(m_items))
        if (!item.trash)
            places.remove(placeKey(item.path));
    reload();
}

void DesktopIcons::showMenu(int index, QPoint global)
{
    // shell32.dll's own menus, as its desktop view puts them together
    using namespace shellmenus;
    auto *menu = new QMenu(m_desktop);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto connectTo = [](const QHash<int, QAction *> &actions, int command, auto slot) {
        if (QAction *action = actions.value(command))
            QObject::connect(action, &QAction::triggered, slot);
    };
    if (index >= 0) {
        const Item item = m_items[index];
        const QString path = item.path;
        const auto find = [this](const QString &wanted) {
            for (int i = 0; i < m_items.size(); ++i)
                if (m_items[i].path == wanted)
                    return i;
            return -1;
        };
        QAction *open = menu->addAction(xpfiles::shellString(STRING_OPEN, "&Open"), this, [this, path, find] {
            if (const int i = find(path); i >= 0)
                this->open(m_items[i]);
        });
        menu->setDefaultAction(open);
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        const bool folder = item.trash || item.shell || QFileInfo(path).isDir();
        if (folder && path != QLatin1String("::internet"))
            menu->addAction(xpfiles::shellString(STRING_EXPLORE, "E&xplore"), this, [this, path, find] {
                if (const int i = find(path); i >= 0)
                    this->open(m_items[i]);
            });
        if (!shell32()) {
            menu->popup(global);
            return;
        }
        if (item.trash) {
            // The Recycle Bin: Empty Recycle Bin (menu 227), then its Properties
            const QHash<int, QAction *> bin = xpprogram::fillMenu(menu, *shell32(), MENU_EMPTY_BIN);
            if (QAction *empty = bin.value(CMD_EMPTY_BIN))
                empty->setEnabled(!QDir(trashFiles()).isEmpty());
            connectTo(bin, CMD_EMPTY_BIN, [this] { fileops::emptyTrash(m_desktop); });
            menu->addSeparator();
            const QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
            if (QAction *action = properties.value(CMD_PROPERTIES))
                action->setEnabled(false);
        } else if (item.shell) {
            if (path == QLatin1String("::my-computer") || path == QLatin1String("::my-documents"))
                menu->addAction(xpfiles::shellString(STRING_SEARCH, "S&earch..."), this, [path] {
                    FileBrowser::open(path == QLatin1String("::my-computer") ? places::Search
                                                                            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
                });
            if (path == QLatin1String("::my-computer"))
                menu->addAction(FileBrowser::manageText(), this, [] { FileBrowser::manageComputer(); });
            // My Computer's Map Network Drive and Disconnect Network Drive (menu 225), greyed as in folder windows
            if (path == QLatin1String("::my-computer")) {
                menu->addSeparator();
                for (QAction *action : xpprogram::fillMenu(menu, *shell32(), 225))
                    action->setEnabled(false);
            }
            menu->addSeparator();
            const QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
            connectTo(properties, CMD_PROPERTIES, [path] {
                if (path == QLatin1String("::my-computer"))
                    controlpanel::open(QStringLiteral("system"));
            });
            if (QAction *action = properties.value(CMD_PROPERTIES))
                action->setEnabled(path == QLatin1String("::my-computer"));
        } else {
            // A file of the Desktop folder: Send To, Cut, Copy, Create Shortcut, Delete, Rename, Properties
            menu->addSeparator();
            addSendTo(menu, {path}, m_desktop);
            menu->addSeparator();
            const QHash<int, QAction *> edit = xpprogram::fillMenu(menu, *shell32(), MENU_ITEM);
            connectTo(edit, CMD_CUT, [path] { fileops::copyToClipboard({path}, true); });
            connectTo(edit, CMD_COPY, [path] { fileops::copyToClipboard({path}, false); });
            if (QAction *paste = edit.value(CMD_PASTE)) {
                // only into a folder, as shell32 keeps it
                if (!QFileInfo(path).isDir())
                    menu->removeAction(paste);
                paste->setEnabled(fileops::clipboardHasFiles());
                connect(paste, &QAction::triggered, this, [this, path] { fileops::pasteInto(path, m_desktop); });
            }
            connectTo(edit, CMD_SHORTCUT, [path] { makeShortcuts({path}, QFileInfo(path).absolutePath()); });
            connectTo(edit, CMD_DELETE, [this, path, find] {
                if (const int i = find(path); i >= 0)
                    trash(i);
            });
            connectTo(edit, CMD_RENAME, [this, path, find] {
                if (const int i = find(path); i >= 0)
                    rename(i);
            });
            menu->addSeparator();
            const QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
            connectTo(properties, CMD_PROPERTIES, [this, path] { fileproperties::show({path}, m_desktop); });
        }
        menu->popup(global);
        return;
    }
    if (!shell32()) {
        menu->addAction(tr("R&efresh"), this, &DesktopIcons::reload);
        menu->popup(global);
        return;
    }
    // The background: menu 215 without View and Customize This Folder, which the desktop has not
    QHash<int, QAction *> items = xpprogram::fillMenu(menu, *shell32(), MENU_BACKGROUND);
    if (QAction *view = items.value(CMD_VIEW))
        menu->removeAction(view);
    if (QAction *customize = items.value(CMD_CUSTOMIZE)) {
        const QList<QAction *> all = menu->actions();
        const int at = int(all.indexOf(customize));
        menu->removeAction(customize);
        // and the separator before it
        if (at > 0 && all[at - 1]->isSeparator())
            menu->removeAction(all[at - 1]);
    }
    // the leading separator View leaves
    if (const QList<QAction *> all = menu->actions(); !all.isEmpty() && all.first()->isSeparator())
        menu->removeAction(all.first());
    // Arrange Icons By: the columns, Auto Arrange and Align to Grid, then menu 218's desktop items
    if (QAction *arrange = items.value(CMD_ARRANGE); arrange && arrange->menu()) {
        QMenu *orders = arrange->menu();
        QAction *first = orders->actions().value(0);
        for (const Order &order : shellmenus::orders()) {
            auto *action = new QAction(xpfiles::shellString(order.string, order.fallback), orders);
            connect(action, &QAction::triggered, this, [this, column = order.column] { arrangeBy(column); });
            orders->insertAction(first, action);
        }
        if (QAction *groups = items.value(CMD_GROUPS))
            orders->removeAction(groups);
        if (QAction *automatic = items.value(CMD_AUTO_ARRANGE)) {
            automatic->setCheckable(true);
            automatic->setChecked(desktopSetting(QStringLiteral("autoArrange"), false));
            connect(automatic, &QAction::toggled, this, [this](bool on) {
                setDesktopSetting(QStringLiteral("autoArrange"), on);
                if (on)
                    arrangeBy(desktopSetting(QStringLiteral("order"), 0));
            });
        }
        // icons always keep to the grid here
        if (QAction *align = items.value(CMD_ALIGN)) {
            align->setCheckable(true);
            align->setChecked(true);
            align->setEnabled(false);
        }
        orders->addSeparator();
        const QHash<int, QAction *> desktop = xpprogram::fillMenu(orders, *shell32(), MENU_DESKTOP);
        if (QAction *show = desktop.value(CMD_SHOW_ICONS)) {
            show->setCheckable(true);
            show->setChecked(!desktopIconsHidden());
            connect(show, &QAction::toggled, this, [this](bool on) {
                setDesktopIconsHidden(!on);
                m_desktop->update();
            });
        }
        for (int command : {CMD_LOCK_WEB, CMD_CLEANUP})
            if (QAction *action = desktop.value(command))
                action->setEnabled(false);
    }
    connectTo(items, CMD_REFRESH, [this] { reload(); });
    const bool files = fileops::clipboardHasFiles();
    const QString folder = desktopFolder();
    if (QAction *paste = items.value(CMD_PASTE_HERE)) {
        paste->setEnabled(files);
        connect(paste, &QAction::triggered, this, [this, folder] { fileops::pasteInto(folder, m_desktop); });
    }
    if (QAction *shortcut = items.value(CMD_PASTE_SHORTCUT)) {
        shortcut->setEnabled(files);
        connect(shortcut, &QAction::triggered, this, [folder] {
            QStringList paths;
            for (const QUrl &url : QGuiApplication::clipboard()->mimeData()->urls())
                if (url.isLocalFile())
                    paths.append(url.toLocalFile());
            makeShortcuts(paths, folder);
        });
    }
    if (QAction *undo = items.value(CMD_UNDO)) {
        undo->setText(fileops::UndoStack::instance()->menuText());
        undo->setEnabled(fileops::UndoStack::instance()->canUndo());
        connect(undo, &QAction::triggered, this, [this] { fileops::UndoStack::instance()->undo(m_desktop); });
    }
    menu->addSeparator();
    QMenu *create = menu->addMenu(tr("Ne&w"));
    create->addAction(fileicons::shell(fileicons::Folder), tr("&Folder"), this, [this, folder] {
        QDir(folder).mkdir(fileops::freeName(folder, tr("New Folder")));
    });
    create->addAction(fileicons::shell(fileicons::Document), tr("&Text Document"), this, [folder] {
        QFile file(QDir(folder).filePath(fileops::freeName(folder, tr("New Text Document.txt"))));
        file.open(QIODevice::WriteOnly);
    });
    menu->addSeparator();
    const QHash<int, QAction *> properties = xpprogram::fillMenu(menu, *shell32(), MENU_PROPERTIES);
    connectTo(properties, CMD_PROPERTIES, [] { DisplayProperties::showTab(DisplayProperties::Desktop); });
    menu->popup(global);
}

void DesktopIcons::arrangeBy(int column)
{
    // Arrange Icons By: the files in that order, down the columns again
    setDesktopSetting(QStringLiteral("order"), column);
    arrange();
}
