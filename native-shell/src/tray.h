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
#include <functional>

class Balloons;
class QMenu;
class SystemIcons;

struct TrayIcon
{
    quint32 id = 0;
    QString tooltip;
    QImage image;
    QString name;   // what it stays known by (tray-names.tsv), whatever its tooltip says
};

class Tray : public QObject
{
    Q_OBJECT
public:
    // IPC may be empty: no tray host, no icons.
    Tray(const QString &ipc, int iconSize);
    const QList<TrayIcon> &icons() const { return m_shown; }
    void click(quint32 id, const char *op, QPoint at);
    Balloons *balloons() const { return m_balloons; }
    // Icons of the shell's own services (the Security Center, the notifications), after the
    // applications' and before XP's system icons. CLICK says whether it took a click.
    using ClickHandler = std::function<bool(quint32 id, const char *op, QPoint at)>;
    void setOwnIcons(QObject *owner, const QList<TrayIcon> &icons, ClickHandler click);

    enum { MOD_CONTROL = 2, MOD_SHIFT = 4 };   // RegisterHotKey's, for hotkey()

Q_SIGNALS:
    void changed();
    // The windows key, or Ctrl+Esc, under a Linux window (shellkeys.tsv of bridge-wm)
    void startRequested();
    // A key with the Windows key, by its virtual key, and Shift or Ctrl held with it
    void hotkey(int key, int modifiers);

private:
    void directoryChanged();
    void read();
    void showMenus();
    void showMenu(quint32 id, const QStringList &lines);
    void command(const QByteArray &line);
    void showAll();
    void readBalloons();

    struct OwnIcons { QObject *owner; QList<TrayIcon> icons; ClickHandler click; };
    QDir m_ipc;
    QFileSystemWatcher m_watcher;
    QList<TrayIcon> m_icons, m_shown;
    QList<OwnIcons> m_own;
    SystemIcons *m_system = nullptr;
    Balloons *m_balloons = nullptr;
    // The applications' balloons by their icons: the last serial of balloon-ID.tsv, and the balloon shown for it
    QHash<quint32, qint64> m_balloonSerials;
    QHash<quint32, quint64> m_balloonsShown;
    QHash<quint32, int> m_menusShown;
    QByteArray m_list;
    int m_shellKeys = 0, m_commands = 0;
    QPoint m_menuPoint;
};
