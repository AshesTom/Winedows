// SPDX-License-Identifier: LGPL-2.1-or-later
// XP SP2's Security Center on Linux: its three essentials, its red shield in the
// notification area and its "Your computer might be at risk" balloon, in the words and
// pictures of the user's xpsp2res.dll.
//   Firewall: on when firewalld, ufw (enabled), nftables or iptables runs.
//   Automatic Updates: on when the distribution's unattended updates are set up
//   (unattended-upgrades, dnf-automatic, rpm-ostree's); not monitored elsewhere.
//   Virus Protection: not monitored, as XP leaves an antivirus it cannot see.
#pragma once

#include "tray.h"

#include <QObject>
#include <QTimer>
#include <functional>

namespace securitycenter {

enum State { On, Off, NotMonitored };

struct Status
{
    State firewall = NotMonitored, updates = NotMonitored, virus = NotMonitored;
    QString firewallName;             // the firewall that is on
    bool atRisk() const { return firewall == Off || updates == Off || virus == Off; }
    bool operator==(const Status &other) const { return firewall == other.firewall && updates == other.updates && virus == other.virus; }
};

// Reads the system's state, later: DONE runs in CONTEXT's thread once it is known.
void check(QObject *context, std::function<void(const Status &)> done);
// Windows Security Center: the essentials and what to do about them.
void showWindow();
QImage shield(State state, int size);
// A string of xpsp2res.dll
QString string(int id, const char *fallback);

// wscntfy.exe's part: the shield while the computer is at risk, and the balloon when it becomes so.
class Alerts : public QObject
{
    Q_OBJECT
public:
    explicit Alerts(Tray *tray);

private:
    void refresh();
    void update(const Status &status);
    void showBalloon();

    Tray *m_tray;
    QTimer m_timer;
    Status m_status;
    bool m_known = false;
    quint64 m_balloon = 0;
};

} // namespace securitycenter
