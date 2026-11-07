// SPDX-License-Identifier: LGPL-2.1-or-later
#include "securitycenter.h"

#include "balloon.h"
#include "controlpanel.h"
#include "theme.h"
#include "xpfiles.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace securitycenter {
namespace {

const quint32 ICON_SECURITY = 0xfffffff3;
// xpsp2res.dll's shields: green with a check, red with a cross, yellow with an exclamation mark
const int SHIELD_ON = 10602, SHIELD_OFF = 10603, SHIELD_UNKNOWN = 10604;
// wscntfy.exe's notification icon and its balloon's (NIIF_USER): xpsp2res.dll's 6106, a red shield
// (sub_10018d5, sub_10019c3: LoadImageW at 16 x 16)
const int ICON_ALERT = 6106;
// Its words
const int STRING_UPDATES_OFF = 17003, STRING_NO_FIREWALL = 17005, STRING_NO_ANTIVIRUS = 17006, STRING_OPEN = 17008,
          STRING_WEB_SITE = 17009, STRING_ALERTS = 17010, STRING_ANTIVIRUS_OFF = 17012, STRING_CLICK = 17013, STRING_BULLET = 17014,
          STRING_AT_RISK = 17119;
// Go to Microsoft Security Web Site (sub_100194a)
const char WEB_SITE[] = "http://go.microsoft.com/fwlink/?linkid=193";
// The first look a while after the session starts, as wscntfy.exe waits for the services; then every minute.
const int FIRST_CHECK = 15000, CHECK_EVERY = 60000;
// An alert stays XP's longest.
const int BALLOON_TIMEOUT = 30000;

bool installed(const char *program)
{
    return !QStandardPaths::findExecutable(QLatin1String(program)).isEmpty();
}

const std::optional<w4w::PeResources> &xpsp2res()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("xpsp2res.dll")));
    return resources;
}

// A program's output, later
void run(QObject *context, const QString &program, const QStringList &arguments, std::function<void(const QString &)> done)
{
    auto *process = new QProcess(context);
    QObject::connect(process, &QProcess::finished, context, [process, done] {
        done(QString::fromUtf8(process->readAllStandardOutput()));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(QString());
            process->deleteLater();
        }
    });
    process->start(program, arguments);
}

bool ufwEnabled()
{
    QFile file(QStringLiteral("/etc/ufw/ufw.conf"));
    return file.open(QIODevice::ReadOnly)
        && QRegularExpression(QStringLiteral("^\\s*ENABLED\\s*=\\s*\"?yes"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption)
               .match(QString::fromUtf8(file.readAll())).hasMatch();
}

// apt's unattended upgrades: on with the package and APT::Periodic::Unattended-Upgrade set, off
// when that setting says so, not yet configured otherwise (wscsvc.dll's states 1 and 5)
State aptUnattended()
{
    if (!installed("unattended-upgrade"))
        return CheckSettings;
    const QDir folder(QStringLiteral("/etc/apt/apt.conf.d"));
    static const QRegularExpression setting(QStringLiteral("APT::Periodic::Unattended-Upgrade\\s+\"(\\w+)\""));
    State state = CheckSettings;
    for (const QString &name : folder.entryList(QDir::Files, QDir::Name)) {
        QFile file(folder.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        auto matches = setting.globalMatch(QString::fromUtf8(file.readAll()));
        while (matches.hasNext()) {
            const QString value = matches.next().captured(1);
            state = value != QLatin1String("0") && value.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0 ? On : Off;
        }
    }
    return state;
}

QSettings &choices()
{
    static QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/security-center.conf"),
                              QSettings::IniFormat);
    return settings;
}

const char *notifyName(Essential essential)
{
    return essential == Virus ? "AntiVirusDisableNotify" : essential == Firewall ? "FirewallDisableNotify" : "UpdatesDisableNotify";
}

QPointer<Alerts> alertsInstance;

} // namespace

bool alerts(Essential essential)
{
    return !choices().value(QLatin1String(notifyName(essential)), false).toBool();
}

void setAlerts(Essential essential, bool on)
{
    choices().setValue(QLatin1String(notifyName(essential)), !on);
}

bool monitoredByUser(Essential essential)
{
    return essential != Updates && choices().value(QLatin1String(essential == Virus ? "AntiVirusOverride" : "FirewallOverride"), false).toBool();
}

void setMonitoredByUser(Essential essential, bool on)
{
    if (essential != Updates)
        choices().setValue(QLatin1String(essential == Virus ? "AntiVirusOverride" : "FirewallOverride"), on);
}

// wscsvc.dll alerts for an essential in its bad state (1 off or missing) whose notify bit is set; the
// balloon names them in wscntfy.exe's order: firewall, Automatic Updates, antivirus.
QStringList problems(const Status &status)
{
    QStringList out;
    if (status.firewall == Off && alerts(Firewall))
        out.append(string(STRING_NO_FIREWALL, "No firewall is turned on"));
    if (status.updates == Off && alerts(Updates))
        out.append(string(STRING_UPDATES_OFF, "Automatic Updates is turned off"));
    if (status.virus == NotFound && alerts(Virus))
        out.append(string(STRING_NO_ANTIVIRUS, "Antivirus software might not be installed"));
    else if (status.virus == Off && alerts(Virus))
        out.append(string(STRING_ANTIVIRUS_OFF, "%1 is turned off").replace(QLatin1String("%1"), status.virusName));
    return out;
}

QString string(int id, const char *fallback)
{
    QString text = xpsp2res() ? xpfiles::string(*xpsp2res(), id) : QString();
    text.remove(QChar(0));
    return text.isEmpty() ? QString::fromUtf8(fallback) : text;
}

QImage alertIcon()
{
    return xpsp2res() ? xpfiles::icon(*xpsp2res(), ICON_ALERT, ShellTheme::instance()->scaled(16)) : QImage();
}

QImage shield(State state, int size)
{
    return xpsp2res() ? xpfiles::icon(*xpsp2res(), state == On ? SHIELD_ON : state == Off ? SHIELD_OFF : SHIELD_UNKNOWN, size) : QImage();
}

void check(QObject *context, std::function<void(const Status &)> done)
{
    if (!installed("systemctl")) {
        done(Status{});
        return;
    }
    // The firewalls, then ClamAV's scanner under its distributions' names
    const QStringList firewalls{QStringLiteral("firewalld"), QStringLiteral("ufw"), QStringLiteral("nftables"), QStringLiteral("iptables")};
    const QStringList scanners{QStringLiteral("clamav-daemon"), QStringLiteral("clamd@scan"), QStringLiteral("clamd")};
    run(context, QStringLiteral("systemctl"), QStringList{QStringLiteral("is-active")} + firewalls + scanners,
        [context, done, firewalls, scanners](const QString &output) {
            const QStringList active = output.split(QLatin1Char('\n'));
            const auto running = [&](int i) { return active.value(i).trimmed() == QLatin1String("active"); };
            Status status;
            for (int i = 0; i < firewalls.size() && status.firewall != On; ++i)
                if (running(i) && (firewalls[i] != QLatin1String("ufw") || ufwEnabled())) {
                    status.firewall = On;
                    status.firewallName = firewalls[i];
                }
            if (status.firewall != On)
                status.firewall = Off;
            if (monitoredByUser(Firewall))
                status.firewall = NotMonitored;
            // ClamAV: ON while its scanner runs, OFF when only installed, NOT FOUND without it
            for (int i = 0; i < scanners.size() && status.virus != On; ++i)
                if (running(int(firewalls.size()) + i))
                    status.virus = On;
            if (status.virus != On)
                status.virus = installed("clamscan") || installed("clamd") ? Off : NotFound;
            if (status.virus != NotFound)
                status.virusName = QStringLiteral("ClamAV");
            if (monitoredByUser(Virus))
                status.virus = NotMonitored;
            if (installed("apt-get")) {
                status.updates = aptUnattended();
                done(status);
            } else if (installed("dnf") || installed("dnf5") || installed("rpm-ostree")) {
                run(context, QStringLiteral("systemctl"),
                    {QStringLiteral("is-enabled"), QStringLiteral("dnf-automatic.timer"), QStringLiteral("dnf-automatic-install.timer"),
                     QStringLiteral("dnf5-automatic.timer"), QStringLiteral("rpm-ostreed-automatic.timer")},
                    [done, status](const QString &enabled) mutable {
                        // a timer enabled: on; one masked: turned off; none set up: not yet configured
                        const QStringList states = enabled.split(QLatin1Char('\n'));
                        status.updates = states.contains(QLatin1String("enabled")) ? On : states.contains(QLatin1String("masked")) ? Off : CheckSettings;
                        done(status);
                    });
            } else {
                done(status);
            }
        });
}

Alerts::Alerts(Tray *tray)
    : QObject(tray)
    , m_tray(tray)
{
    alertsInstance = this;
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Alerts::refresh);
    m_timer.start(FIRST_CHECK);
}

void Alerts::recheck()
{
    if (alertsInstance && alertsInstance->m_known)
        alertsInstance->refresh();
}

void Alerts::refresh()
{
    check(this, [this](const Status &status) {
        update(status);
        m_timer.start(CHECK_EVERY);
    });
}

void Alerts::update(const Status &status)
{
    const bool wasAtRisk = m_known && !m_problems.isEmpty();
    const QStringList found = problems(status);
    const bool newProblem = !m_known || found != m_problems;
    m_status = status;
    m_problems = found;
    m_known = true;
    if (found.isEmpty()) {
        if (m_balloon)
            m_tray->balloons()->cancel(m_balloon);
        m_tray->setOwnIcons(this, {}, nullptr);
        return;
    }
    TrayIcon icon;
    icon.id = ICON_SECURITY;
    icon.image = alertIcon();
    icon.tooltip = string(STRING_ALERTS, "Windows Security Alerts");
    m_tray->setOwnIcons(this, {icon}, [](quint32 id, const char *op, QPoint at) {
        if (id != ICON_SECURITY)
            return false;
        if (qstrcmp(op, "context") == 0) {
            auto *menu = new QMenu;
            menu->setAttribute(Qt::WA_DeleteOnClose);
            // wscntfy.exe's menu (0x65 and 0x66), the first the default
            QAction *open = menu->addAction(string(STRING_OPEN, "Open Security Center"), [] { controlpanel::open(QStringLiteral("security")); });
            menu->addAction(string(STRING_WEB_SITE, "Go to Microsoft Security Web Site"),
                            [] { QDesktopServices::openUrl(QUrl(QLatin1String(WEB_SITE))); });
            menu->setDefaultAction(open);
            Tray::popupMenu(menu, at);
        } else {
            controlpanel::open(QStringLiteral("security"));
        }
        return true;
    });
    if (!wasAtRisk || newProblem)
        showBalloon();
}

void Alerts::showBalloon()
{
    const QStringList &found = m_problems;
    BalloonTip tip;
    tip.icon = ICON_SECURITY;
    tip.title = string(STRING_AT_RISK, "Your computer might be at risk");
    // sub_10019c3: each problem on its line (firewall, Automatic Updates, virus protection), after
    // string 17014's "· " when there are several; a blank line, then what to do.
    const QString bullet = found.size() > 1 ? string(STRING_BULLET, "\u00b7 ") : QString();
    for (const QString &problem : found)
        tip.text += bullet + problem + QLatin1Char('\n');
    tip.text += QLatin1Char('\n') + string(STRING_CLICK, "Click this balloon to fix this problem.");
    tip.kind = BalloonTip::User;
    tip.image = alertIcon();
    tip.timeout = BALLOON_TIMEOUT;
    tip.closed = [this](BalloonTip::Reason reason) {
        m_balloon = 0;
        if (reason == BalloonTip::Clicked)
            controlpanel::open(QStringLiteral("security"));
    };
    m_balloon = m_tray->balloons()->show(tip);
}

} // namespace securitycenter
