// SPDX-License-Identifier: LGPL-2.1-or-later
// The desktop's icons: Recycle Bin and the files of the user's Desktop folder,
// in XP's grid, with their places remembered. Desktop draws them and passes
// them its mouse and keys.
#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>

class QPainter;
class QWidget;

// XP's Desktop Items: the icons of its own the desktop may show (shell32's Customize Desktop), kept in
// native-explorer.conf's [desktop]. None by default, as on a new XP SP2 account.
enum class DesktopItem { Documents, Computer, Network, Internet };
bool desktopItemShown(DesktopItem item);
void setDesktopItemShown(DesktopItem item, bool shown);
// Customize Desktop's Change Icon and Restore Default: a file and an icon group, kept as
// desktop/icon.<key>; no setting means the item's XP default.
QImage desktopItemIcon(DesktopItem item, int size);
QImage recycleBinIcon(bool full, int size);
void setDesktopIconOverride(const QString &key, const QString &file, const QString &group);
void clearDesktopIconOverride(const QString &key);

class DesktopIcons : public QObject
{
    Q_OBJECT
public:
    explicit DesktopIcons(QWidget *desktop);
    void reload();
    void paint(QPainter &painter, const QRect &clip) const;
    void press(QPoint at, Qt::MouseButton button, QPoint global);
    void move(QPoint at);
    void release();
    void doubleClick(QPoint at);
    bool key(int key);

private:
    struct Item
    {
        QString path, name;
        QImage icon;
        QPoint place;       // the cell's top-left, on the desktop
        bool trash = false;
        bool shell = false;   // one of XP's Desktop Items: no file to rename or delete
    };

    QSize cell() const;
    QRect workArea() const;
    QRect iconRect(const Item &item) const;
    QRect textRect(const Item &item, const QStringList &lines) const;
    QStringList nameLines(const QString &name) const;
    int itemAt(QPoint at) const;
    QPoint freeCell(const QList<Item> &taken) const;
    void open(const Item &item);
    void rename(int index);
    void trash(int index);
    void arrange();
    void arrangeBy(int column);
    void savePlace(const Item &item);
    void showMenu(int index, QPoint global);
    QImage iconFor(const QString &path) const;

    QWidget *m_desktop;
    QFileSystemWatcher m_watcher;
    QList<Item> m_items;
    int m_selected = -1, m_dragged = -1;
    QPoint m_pressAt, m_pressPlace;
    bool m_moved = false;
};
