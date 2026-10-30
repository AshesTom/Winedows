// SPDX-License-Identifier: LGPL-2.1-or-later
#include "tray.h"

#include "ipcfile.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <QtEndian>

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

Tray::Tray(const QString &ipc, int iconSize)
    : m_ipc(ipc)
{
    if (ipc.isEmpty())
        return;
    QFile size(m_ipc.filePath(QStringLiteral("tray-size")));
    if (size.open(QIODevice::WriteOnly))
        size.write(QByteArray::number(iconSize) + '\n');
    m_watcher.addPath(ipc);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Tray::directoryChanged);
    // Only the Windows keys pressed from now on count.
    m_shellKeys = readFile(m_ipc.filePath(QStringLiteral("shellkeys.tsv"))).split(' ').value(0).toInt();
    read();
}

void Tray::directoryChanged()
{
    const QList<QByteArray> keys = readFile(m_ipc.filePath(QStringLiteral("shellkeys.tsv"))).trimmed().split(' ');
    if (const int serial = keys.value(0).toInt(); serial > m_shellKeys) {
        m_shellKeys = serial;
        // Win+R and the like belong to XP's hotkeys, which have no native code yet.
        if (keys.size() == 2)
            Q_EMIT startRequested();
    }
    read();
    showMenus();
}

void Tray::read()
{
    const QByteArray list = readFile(m_ipc.filePath(QStringLiteral("tray.tsv")));
    QList<TrayIcon> icons;
    for (const QByteArray &line : list.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 2 || !fields[0].toUInt())
            continue;
        TrayIcon icon;
        icon.id = fields[0].toUInt();
        icon.tooltip = QString::fromUtf8(fields.value(2));
        icon.image = ipcfile::readIcon(m_ipc.filePath(QStringLiteral("tray-%1.argb").arg(icon.id)));
        if (!icon.image.isNull())
            icons.append(icon);
    }
    // Compare what the icons look like: this runs for every file of the directory.
    bool same = icons.size() == m_icons.size();
    for (int i = 0; same && i < icons.size(); ++i)
        same = icons[i].id == m_icons[i].id && icons[i].tooltip == m_icons[i].tooltip && icons[i].image == m_icons[i].image;
    if (same)
        return;
    m_icons = icons;
    Q_EMIT changed();
}

void Tray::command(const QByteArray &line)
{
    // tray.py reads *.tray files, which appear whole by their rename.
    const QString base = QStringLiteral("%1-%2").arg(QCoreApplication::applicationPid()).arg(++m_commands);
    ipcfile::write(m_ipc, base + QStringLiteral(".tray"), line + '\n');
}

void Tray::click(quint32 id, const char *op, QPoint at)
{
    m_menuPoint = at;
    command(QByteArray(op) + ' ' + QByteArray::number(id) + ' ' + QByteArray::number(at.x()) + ' ' + QByteArray::number(at.y()));
}

void Tray::showMenus()
{
    for (const TrayIcon &icon : std::as_const(m_icons)) {
        const QStringList lines = QString::fromUtf8(readFile(m_ipc.filePath(QStringLiteral("menu-%1.tsv").arg(icon.id)))).split(QLatin1Char('\n'));
        const int serial = lines.value(0).toInt();
        if (serial > m_menusShown.value(icon.id)) {
            m_menusShown.insert(icon.id, serial);
            showMenu(icon.id, lines);
        }
    }
}

void Tray::showMenu(quint32 id, const QStringList &lines)
{
    const int serial = lines.value(0).toInt();
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QList<QMenu *> stack{menu};
    QAction *last = nullptr;
    for (int i = 1; i < lines.size(); ++i) {
        // depth, item, flags, label
        const QStringList fields = lines[i].split(QLatin1Char('\t'));
        if (fields.size() < 4)
            continue;
        const int depth = fields[0].toInt();
        if (depth < 0 || depth > stack.size())
            continue;
        if (depth == stack.size() && last) {
            auto *sub = new QMenu(stack.last());
            last->setMenu(sub);
            stack.append(sub);
        }
        while (stack.size() > depth + 1)
            stack.removeLast();
        if (fields[2].contains(QLatin1Char('s'))) {
            stack.last()->addSeparator();
            last = nullptr;
            continue;
        }
        last = stack.last()->addAction(fields[3]);
        last->setEnabled(!fields[2].contains(QLatin1Char('d')));
        if (fields[2].contains(QLatin1Char('c')) || fields[2].contains(QLatin1Char('r'))) {
            last->setCheckable(true);
            last->setChecked(true);
        }
        const QByteArray item = fields[1].toLatin1();
        connect(last, &QAction::triggered, this, [this, id, serial, item] {
            command("event " + QByteArray::number(id) + ' ' + QByteArray::number(serial) + ' ' + item);
        });
    }
    menu->popup(m_menuPoint.isNull() ? QCursor::pos() : m_menuPoint - QPoint(0, menu->sizeHint().height()));
}
