// SPDX-License-Identifier: LGPL-2.1-or-later
// The Linux tray icons from bridge-wm/tray.py's files in the IPC directory
// (the same files proxy.exe turns into Shell_NotifyIcon calls for XP's shell).
#pragma once

#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>

class QMenu;

struct TrayIcon
{
    quint32 id = 0;
    QString tooltip;
    QImage image;
};

class Tray : public QObject
{
    Q_OBJECT
public:
    // IPC may be empty: no tray host, no icons.
    Tray(const QString &ipc, int iconSize);
    const QList<TrayIcon> &icons() const { return m_icons; }
    void click(quint32 id, const char *op, QPoint at);

Q_SIGNALS:
    void changed();
    // The windows key, or Ctrl+Esc, under a Linux window (shellkeys.tsv of bridge-wm)
    void startRequested();

private:
    void directoryChanged();
    void read();
    void showMenus();
    void showMenu(quint32 id, const QStringList &lines);
    void command(const QByteArray &line);

    QDir m_ipc;
    QFileSystemWatcher m_watcher;
    QList<TrayIcon> m_icons;
    QHash<quint32, int> m_menusShown;
    QByteArray m_list;
    int m_shellKeys = 0, m_commands = 0;
    QPoint m_menuPoint;
};
