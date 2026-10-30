// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's own icons of the notification area, on Linux's services: the volume (PulseAudio or
// PipeWire, through pactl) and the network (NetworkManager, through nmcli). Their pictures
// come from the user's sndvol32.exe and netshell.dll.
#pragma once

#include "tray.h"

#include <QObject>
#include <QPoint>
#include <QTimer>
#include <functional>

class QProcess;

class SystemIcons : public QObject
{
    Q_OBJECT
public:
    SystemIcons(int iconSize, QObject *parent);
    ~SystemIcons() override;
    QList<TrayIcon> icons() const;
    // True if the icon is one of these.
    bool click(quint32 id, const char *op, QPoint at);

Q_SIGNALS:
    void changed();

private:
    void refreshSound();
    void refreshNetwork();
    void refreshNetworks();
    void showVolume(QPoint at);
    void showSoundMenu(QPoint at);
    void showNetworks(QPoint at);
    void showNetworkMenu(QPoint at);
    void connectTo(const QString &ssid, bool secured);
    QProcess *follow(const QString &program, const QStringList &arguments, QTimer *timer);

    int m_size;
    bool m_hasSound = false, m_hasNetwork = false;
    int m_volume = 0;
    bool m_muted = false, m_online = false, m_wireless = false;
    QString m_connection, m_networks;
    QTimer m_soundTimer, m_networkTimer;
    QList<QProcess *> m_followers;
};
