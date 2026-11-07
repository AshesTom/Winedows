// SPDX-License-Identifier: LGPL-2.1-or-later
// XP SP2's Security Center on Linux: its three essentials, its red shield in the
// notification area and its "Your computer might be at risk" balloon, in the words and
// pictures of the user's xpsp2res.dll. The states are wscsvc.dll's:
//   Firewall: ON when firewalld, ufw (enabled), nftables or iptables runs, OFF otherwise.
//   Automatic Updates: ON when the distribution's unattended updates are set up
//   (unattended-upgrades, dnf-automatic, rpm-ostree's), OFF when they are turned off,
//   CHECK SETTINGS when they are not set up (XP's "not yet configured", which alerts
//   nobody); not monitored where the distribution has none.
//   Virus Protection: ON when ClamAV's scanner runs, OFF when it is installed but stopped,
//   NOT FOUND without it.
// What the user chose in its Alert Settings and Recommendation dialogs is kept under XP's
// value names (HKLM\SOFTWARE\Microsoft\Security Center) in ~/.config/win4wine/security-center.conf.
#pragma once

#include "tray.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

class QWidget;

namespace securitycenter {

enum State { On, Off, NotMonitored, CheckSettings, NotFound };

struct Status
{
    State firewall = NotMonitored, updates = NotMonitored, virus = NotMonitored;
    QString firewallName;             // the firewall that is on
    QString virusName;                // the antivirus program found
    bool operator==(const Status &other) const { return firewall == other.firewall && updates == other.updates && virus == other.virus; }
};

// The essentials, as wscsvc.dll's alert mask has them: 1 antivirus, 2 firewall, 4 Automatic Updates
enum Essential { Virus = 1, Firewall = 2, Updates = 4 };
// The Alert Settings' choice (the *DisableNotify values): whether ESSENTIAL may alert
bool alerts(Essential essential);
void setAlerts(Essential essential, bool on);
// "I have a firewall solution / an antivirus program that I'll monitor myself" (FirewallOverride,
// AntiVirusOverride): Windows no longer watches it
bool monitoredByUser(Essential essential);
void setMonitoredByUser(Essential essential, bool on);
// What the balloon names, each problem whose essential may alert (wscntfy.exe sub_10019c3)
QStringList problems(const Status &status);

// Reads the system's state, later: DONE runs in CONTEXT's thread once it is known.
void check(QObject *context, std::function<void(const Status &)> done);
// Windows Security Center: the essentials and what to do about them.
void showWindow();
// Its Alert Settings dialog (xpsp2res.dll's 1715)
void showAlertSettings(QWidget *parent);
QImage shield(State state, int size);
// A string of xpsp2res.dll
QString string(int id, const char *fallback);

// wscntfy.exe's part: the shield while the computer is at risk, and the balloon when it becomes so.
class Alerts : public QObject
{
    Q_OBJECT
public:
    explicit Alerts(Tray *tray);
    // Looks again now: the user changed what alerts
    static void recheck();

private:
    void refresh();
    void update(const Status &status);
    void showBalloon();

    Tray *m_tray;
    QTimer m_timer;
    Status m_status;
    QStringList m_problems;
    bool m_known = false;
    quint64 m_balloon = 0;
};

} // namespace securitycenter
