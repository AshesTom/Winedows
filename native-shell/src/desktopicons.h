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
