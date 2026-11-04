// SPDX-License-Identifier: LGPL-2.1-or-later
#include "tray.h"

#include "balloon.h"
#include "ipcfile.h"
#include "systemicons.h"

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
    , m_system(new SystemIcons(iconSize, this))
    , m_balloons(new Balloons(this))
{
    connect(m_system, &SystemIcons::changed, this, &Tray::showAll);
    showAll();
    if (ipc.isEmpty())
        return;
    QFile size(m_ipc.filePath(QStringLiteral("tray-size")));
    if (size.open(QIODevice::WriteOnly))
        size.write(QByteArray::number(iconSize) + '\n');
    m_watcher.addPath(ipc);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Tray::directoryChanged);
    // Only the Windows keys pressed from now on count, and the balloons asked for from now on.
    m_shellKeys = readFile(m_ipc.filePath(QStringLiteral("shellkeys.tsv"))).split(' ').value(0).toInt();
    read();
    for (const TrayIcon &icon : std::as_const(m_icons))
        m_balloonSerials.insert(icon.id, readFile(m_ipc.filePath(QStringLiteral("balloon-%1.tsv").arg(icon.id))).split('\t').value(0).toLongLong());
}

void Tray::directoryChanged()
{
    const QList<QByteArray> keys = readFile(m_ipc.filePath(QStringLiteral("shellkeys.tsv"))).trimmed().split(' ');
    if (const int serial = keys.value(0).toInt(); serial > m_shellKeys) {
        m_shellKeys = serial;
        if (keys.size() == 2)
            Q_EMIT startRequested();
        else if (keys.value(1) == "windows")
            for (int i = 2; i < keys.size(); ++i)
                Q_EMIT hotkey(keys[i].toInt(nullptr, 16));
    }
    read();
    showMenus();
    readBalloons();
}

void Tray::read()
{
    const QByteArray list = readFile(m_ipc.filePath(QStringLiteral("tray.tsv")));
    QHash<quint32, QString> names;
    for (const QByteArray &line : readFile(m_ipc.filePath(QStringLiteral("tray-names.tsv"))).split('\n'))
        if (const int tab = line.indexOf('\t'); tab > 0)
            names.insert(line.left(tab).toUInt(), QString::fromUtf8(line.mid(tab + 1)));
    QList<TrayIcon> icons;
    for (const QByteArray &line : list.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 2 || !fields[0].toUInt())
            continue;
        TrayIcon icon;
        icon.id = fields[0].toUInt();
        icon.tooltip = QString::fromUtf8(fields.value(2));
        icon.name = names.value(icon.id);
        icon.image = ipcfile::readIcon(m_ipc.filePath(QStringLiteral("tray-%1.argb").arg(icon.id)));
        if (!icon.image.isNull())
            icons.append(icon);
    }
    // Compare what the icons look like: this runs for every file of the directory.
    bool same = icons.size() == m_icons.size();
    for (int i = 0; same && i < icons.size(); ++i)
        same = icons[i].id == m_icons[i].id && icons[i].tooltip == m_icons[i].tooltip && icons[i].name == m_icons[i].name && icons[i].image == m_icons[i].image;
    if (same)
        return;
    m_icons = icons;
    showAll();
}

// The applications' icons, then XP's own next to the clock
void Tray::showAll()
{
    m_shown = m_icons;
    for (const OwnIcons &own : std::as_const(m_own))
        m_shown += own.icons;
    m_shown += m_system->icons();
    Q_EMIT changed();
}

void Tray::setOwnIcons(QObject *owner, const QList<TrayIcon> &icons, ClickHandler click)
{
    auto it = std::find_if(m_own.begin(), m_own.end(), [owner](const OwnIcons &own) { return own.owner == owner; });
    if (it == m_own.end()) {
        m_own.append({owner, icons, std::move(click)});
        connect(owner, &QObject::destroyed, this, [this, owner] {
            m_own.removeIf([owner](const OwnIcons &own) { return own.owner == owner; });
            showAll();
        });
    } else {
        it->icons = icons;
        it->click = std::move(click);
    }
    showAll();
}

// balloon-ID.tsv of tray.py: "SERIAL TAB TIMEOUT TAB TITLE TAB TEXT", the text's line breaks as \n;
// a newer serial without a text takes the balloon back.
void Tray::readBalloons()
{
    for (const TrayIcon &icon : std::as_const(m_icons)) {
        const QList<QByteArray> fields = readFile(m_ipc.filePath(QStringLiteral("balloon-%1.tsv").arg(icon.id))).split('\t');
        const qint64 serial = fields.value(0).toLongLong();
        if (serial <= m_balloonSerials.value(icon.id))
            continue;
        m_balloonSerials.insert(icon.id, serial);
        if (const quint64 shown = m_balloonsShown.take(icon.id))
            m_balloons->cancel(shown);
        const QString text = QString::fromUtf8(fields.value(3)).trimmed().replace(QLatin1String("\\n"), QLatin1String("\n"));
        if (text.isEmpty())
            continue;
        BalloonTip tip;
        tip.icon = icon.id;
        tip.title = QString::fromUtf8(fields.value(2));
        tip.text = text;
        tip.timeout = fields.value(1).toInt();
        const quint32 id = icon.id;
        tip.closed = [this, id, serial](BalloonTip::Reason) {
            if (m_balloonSerials.value(id) == serial)
                m_balloonsShown.remove(id);
        };
        m_balloonsShown.insert(icon.id, m_balloons->show(tip));
    }
}

void Tray::command(const QByteArray &line)
{
    // tray.py reads *.tray files, which appear whole by their rename.
    const QString base = QStringLiteral("%1-%2").arg(QCoreApplication::applicationPid()).arg(++m_commands);
    ipcfile::write(m_ipc, base + QStringLiteral(".tray"), line + '\n');
}

void Tray::click(quint32 id, const char *op, QPoint at)
{
    if (m_system->click(id, op, at))
        return;
    for (const OwnIcons &own : std::as_const(m_own))
        if (own.click && own.click(id, op, at))
            return;
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
