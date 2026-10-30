// SPDX-License-Identifier: LGPL-2.1-or-later
#include "desktopicons.h"

#include "displayproperties.h"
#include "programs.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"

#include <QDir>
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

QString placesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-desktop.conf");
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
    if (info.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(path)) {
            const QIcon icon = programs::iconOf(*program);
            if (!icon.isNull())
                return icon.pixmap(size, size).toImage();
        }
    }
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    QIcon icon = QIcon::fromTheme(mime.iconName());
    if (icon.isNull())
        icon = QIcon::fromTheme(mime.genericIconName());
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
    bin.icon = theme->shellIcon(full ? ICON_TRASH_FULL : ICON_TRASH_EMPTY, theme->scaled(ICON_SIZE));
    const QRect area = workArea();
    bin.place = places.value(placeKey(bin.path), QPoint(area.right() + 1 - cell().width(), area.bottom() + 1 - cell().height())).toPoint();
    items.append(bin);

    QList<QFileInfo> pending;
    const QFileInfoList files = QDir(desktopFolder()).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        Item item;
        item.path = file.absoluteFilePath();
        item.name = file.fileName();
        if (file.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(item.path); program && !program->name.isEmpty())
                item.name = program->name;
        }
        item.icon = iconFor(item.path);
        const QVariant saved = places.value(placeKey(item.path));
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
    const QFont font = theme->font(QStringLiteral("Tahoma, 8"));
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
    QFontMetrics metrics(theme->font(QStringLiteral("Tahoma, 8")));
    int width = 0;
    for (const QString &line : lines)
        width = qMax(width, metrics.horizontalAdvance(line));
    const int height = int(lines.size()) * metrics.height();
    const int top = iconRect(item).bottom() + 1 + theme->scaled(TEXT_GAP);
    return QRect(item.place.x() + (cell().width() - width) / 2 - 1, top, width + 2, height);
}

void DesktopIcons::paint(QPainter &painter, const QRect &clip) const
{
    ShellTheme *theme = ShellTheme::instance();
    const QFont font = theme->font(QStringLiteral("Tahoma, 8"));
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
        for (int line = 0; line < lines.size(); ++line) {
            const QRect row(item.place.x(), text.y() + line * metrics.height(), cell().width(), metrics.height());
            if (!selected) {
                // XP's desktop text, with a shadow on the wallpaper
                painter.setPen(Qt::black);
                painter.drawText(row.translated(1, 1), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, lines[line]);
            }
            painter.setPen(Qt::white);
            painter.drawText(row, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, lines[line]);
        }
    }
}

int DesktopIcons::itemAt(QPoint at) const
{
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
    if (QFileInfo(item.path).suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path)) {
            programs::launch(*program);
            return;
        }
    }
    programs::open(item.path);
}

bool DesktopIcons::key(int key)
{
    if (m_selected < 0 || m_selected >= m_items.size())
        return false;
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        open(m_items[m_selected]);
    else if (key == Qt::Key_Delete)
        trash(m_selected);
    else if (key == Qt::Key_F2)
        rename(m_selected);
    else
        return false;
    return true;
}

void DesktopIcons::rename(int index)
{
    const Item item = m_items[index];
    if (item.trash)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(m_desktop, tr("Rename"), tr("Name:"), QLineEdit::Normal, QFileInfo(item.path).fileName(), &ok);
    if (!ok || name.isEmpty() || name.contains(QLatin1Char('/')))
        return;
    const QString target = QFileInfo(item.path).absoluteDir().filePath(name);
    if (QFile::rename(item.path, target)) {
        QSettings places(placesPath(), QSettings::IniFormat);
        places.remove(placeKey(item.path));
        places.setValue(placeKey(target), item.place);
    }
}

void DesktopIcons::trash(int index)
{
    const Item item = m_items[index];
    if (!item.trash)
        QFile::moveToTrash(item.path);
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
    auto *menu = new QMenu(m_desktop);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (index >= 0) {
        const QString path = m_items[index].path;
        const bool isTrash = m_items[index].trash;
        QAction *open = menu->addAction(tr("&Open"), this, [this, path] {
            for (const Item &item : std::as_const(m_items))
                if (item.path == path)
                    this->open(item);
        });
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        if (!isTrash) {
            menu->addSeparator();
            menu->addAction(tr("&Delete"), this, [this, path] {
                for (int i = 0; i < m_items.size(); ++i)
                    if (m_items[i].path == path)
                        trash(i);
            });
            menu->addAction(tr("Rena&me"), this, [this, path] {
                for (int i = 0; i < m_items.size(); ++i)
                    if (m_items[i].path == path)
                        rename(i);
            });
        }
    } else {
        menu->addAction(tr("Arrange &Icons"), this, &DesktopIcons::arrange);
        menu->addAction(tr("R&efresh"), this, &DesktopIcons::reload);
        menu->addSeparator();
        menu->addAction(tr("P&roperties"), this, [] { DisplayProperties::showTab(DisplayProperties::Desktop); });
    }
    menu->popup(global);
}
