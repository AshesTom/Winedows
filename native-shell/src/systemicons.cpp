// SPDX-License-Identifier: LGPL-2.1-or-later
#include "systemicons.h"

#include "controlpanel.h"
#include "xpfiles.h"

#include <QCheckBox>
#include <QFrame>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSlider>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const quint32 ICON_SOUND = 0xfffffff1, ICON_NETWORK = 0xfffffff2;
// sndvol32.exe and netshell.dll
const int ICON_VOLUME = 304, ICON_MUTED = 303, ICON_CONNECTED = 190, ICON_DISCONNECTED = 195;
// A burst of events is one refresh.
const int SOUND_DELAY_MS = 100, NETWORK_DELAY_MS = 500;
const QString SINK = QStringLiteral("@DEFAULT_SINK@");

bool installed(const char *program)
{
    return !QStandardPaths::findExecutable(QLatin1String(program)).isEmpty();
}

// A program's output, later: the shell never waits for a service.
void run(QObject *owner, const QString &program, const QStringList &arguments, std::function<void(const QString &)> done)
{
    auto *process = new QProcess(owner);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process, &QProcess::finished, owner, [process, done = std::move(done)] {
        done(QString::fromUtf8(process->readAll()));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(program, arguments);
}

// nmcli -t: fields split by ":", with "\:" inside one
QStringList terseFields(const QString &line)
{
    QStringList fields{QString()};
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('\\') && i + 1 < line.size())
            fields.last() += line[++i];
        else if (line[i] == QLatin1Char(':'))
            fields.append(QString());
        else
            fields.last() += line[i];
    }
    return fields;
}

QImage iconOf(const char *file, int group, int size)
{
    const auto resources = xpfiles::resources(xpfiles::find(QLatin1String(file)));
    return resources ? xpfiles::icon(*resources, group, size) : QImage();
}

// Above the point, on the screen
void popupAt(QWidget *popup, QPoint at)
{
    popup->adjustSize();
    QPoint place = at - QPoint(popup->width() / 2, popup->height());
    if (const QScreen *screen = QGuiApplication::screenAt(at)) {
        const QRect room = screen->availableGeometry();
        place.setX(qBound(room.left(), place.x(), room.right() - popup->width()));
        place.setY(qMax(room.top(), place.y()));
    }
    popup->move(place);
    popup->show();
}

void openVolumeControl()
{
    for (const char *program : {"pavucontrol", "pavucontrol-qt"})
        if (installed(program)) {
            QProcess::startDetached(QLatin1String(program), {});
            return;
        }
    controlpanel::open(QStringLiteral("sound"));
}

} // namespace

SystemIcons::SystemIcons(int iconSize, QObject *parent)
    : QObject(parent)
    , m_size(iconSize)
{
    for (auto [timer, delay] : {std::pair{&m_soundTimer, SOUND_DELAY_MS}, std::pair{&m_networkTimer, NETWORK_DELAY_MS}}) {
        timer->setSingleShot(true);
        timer->setInterval(delay);
    }
    connect(&m_soundTimer, &QTimer::timeout, this, &SystemIcons::refreshSound);
    connect(&m_networkTimer, &QTimer::timeout, this, &SystemIcons::refreshNetwork);
    m_hasSound = installed("pactl");
    m_hasNetwork = installed("nmcli");
    if (m_hasSound) {
        follow(QStringLiteral("pactl"), {QStringLiteral("subscribe")}, &m_soundTimer);
        refreshSound();
    }
    if (m_hasNetwork) {
        follow(QStringLiteral("nmcli"), {QStringLiteral("monitor")}, &m_networkTimer);
        refreshNetwork();
        refreshNetworks();
    }
}

SystemIcons::~SystemIcons()
{
    for (QProcess *process : std::as_const(m_followers)) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(500);
    }
}

// A program that prints a line for each change
QProcess *SystemIcons::follow(const QString &program, const QStringList &arguments, QTimer *timer)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyRead, this, [process, timer] {
        process->readAll();
        timer->start();
    });
    process->start(program, arguments);
    m_followers.append(process);
    return process;
}

QList<TrayIcon> SystemIcons::icons() const
{
    QList<TrayIcon> out;
    if (m_hasNetwork) {
        TrayIcon icon;
        icon.id = ICON_NETWORK;
        icon.image = iconOf("netshell.dll", m_online ? ICON_CONNECTED : ICON_DISCONNECTED, m_size);
        icon.tooltip = !m_online ? tr("Network cable unplugged or no network")
            : tr("%1 (%2)\nStatus: Connected").arg(m_wireless ? tr("Wireless Network Connection") : tr("Local Area Connection"), m_connection);
        if (!icon.image.isNull())
            out.append(icon);
    }
    if (m_hasSound) {
        TrayIcon icon;
        icon.id = ICON_SOUND;
        icon.image = iconOf("sndvol32.exe", m_muted ? ICON_MUTED : ICON_VOLUME, m_size);
        icon.tooltip = m_muted ? tr("Volume: muted") : tr("Volume: %1%").arg(m_volume);
        if (!icon.image.isNull())
            out.append(icon);
    }
    return out;
}

void SystemIcons::refreshSound()
{
    run(this, QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), SINK}, [this](const QString &text) {
        static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
        const auto match = percent.match(text);
        const int volume = match.hasMatch() ? match.captured(1).toInt() : m_volume;
        run(this, QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), SINK}, [this, volume](const QString &mute) {
            const bool muted = mute.contains(QLatin1String("yes"));
            if (volume != m_volume || muted != m_muted) {
                m_volume = volume;
                m_muted = muted;
                Q_EMIT changed();
            }
        });
    });
}

void SystemIcons::refreshNetwork()
{
    run(this, QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE,STATE,CONNECTION"), QStringLiteral("device")}, [this](const QString &text) {
        bool online = false, wireless = false;
        QString connection;
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList fields = terseFields(line);
            const QString type = fields.value(0);
            if (fields.value(1) != QLatin1String("connected") || (type != QLatin1String("wifi") && type != QLatin1String("ethernet")))
                continue;
            // A cable is what XP would name first.
            if (!online || type == QLatin1String("ethernet")) {
                online = true;
                wireless = type == QLatin1String("wifi");
                connection = fields.value(2);
            }
        }
        if (online != m_online || wireless != m_wireless || connection != m_connection) {
            m_online = online;
            m_wireless = wireless;
            m_connection = connection;
            Q_EMIT changed();
        }
    });
}

bool SystemIcons::click(quint32 id, const char *op, QPoint at)
{
    if (id != ICON_SOUND && id != ICON_NETWORK)
        return false;
    const bool context = qstrcmp(op, "context") == 0, activate = qstrcmp(op, "activate") == 0;
    if (id == ICON_SOUND && activate)
        showVolume(at);
    else if (id == ICON_SOUND && context)
        showSoundMenu(at);
    else if (activate)
        showNetworks(at);
    else if (context)
        showNetworkMenu(at);
    return true;
}

// XP's little Volume window: the slider and Mute
void SystemIcons::showVolume(QPoint at)
{
    auto *popup = new QFrame(nullptr, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::WinPanel);
    popup->setFrameShadow(QFrame::Raised);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    layout->addWidget(new QLabel(tr("Volume"), popup), 0, Qt::AlignHCenter);
    auto *slider = new QSlider(Qt::Vertical, popup);
    slider->setRange(0, 100);
    slider->setPageStep(10);
    slider->setTickPosition(QSlider::TicksBothSides);
    slider->setTickInterval(10);
    slider->setFixedHeight(100);
    slider->setValue(qMin(m_volume, 100));
    layout->addWidget(slider, 0, Qt::AlignHCenter);
    auto *mute = new QCheckBox(tr("&Mute"), popup);
    mute->setChecked(m_muted);
    layout->addWidget(mute);
    connect(slider, &QSlider::valueChanged, this, [](int value) {
        QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), SINK, QStringLiteral("%1%").arg(value)});
    });
    connect(mute, &QCheckBox::toggled, this, [](bool on) {
        QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), SINK, on ? QStringLiteral("1") : QStringLiteral("0")});
    });
    popupAt(popup, at);
    slider->setFocus();
}

void SystemIcons::showSoundMenu(QPoint at)
{
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QFont bold = menu->font();
    bold.setBold(true);
    QAction *open = menu->addAction(tr("&Open Volume Control"), this, [] { openVolumeControl(); });
    open->setFont(bold);
    menu->addAction(tr("&Adjust Audio Properties"), this, [] { controlpanel::open(QStringLiteral("sound")); });
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
}

// What is in reach, read ahead: a menu opens at once, whatever NetworkManager is doing.
void SystemIcons::refreshNetworks()
{
    run(this, QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"), QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"), QStringLiteral("no")},
        [this](const QString &text) { m_networks = text; });
}

// The networks in reach, the current one checked: XP's "Choose a wireless network" as a list
void SystemIcons::showNetworks(QPoint at)
{
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QStringList seen;
    for (const QString &line : m_networks.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = terseFields(line);
        const QString ssid = fields.value(1);
        if (fields.size() < 4 || ssid.isEmpty() || seen.contains(ssid))
            continue;
        seen.append(ssid);
        const bool secured = !fields.value(3).isEmpty();
        QAction *action = menu->addAction(tr("%1 (%2%)%3").arg(ssid, fields.value(2), secured ? tr(", secured") : QString()));
        action->setCheckable(true);
        action->setChecked(fields.value(0) == QLatin1String("*"));
        connect(action, &QAction::triggered, this, [this, ssid, secured] { connectTo(ssid, secured); });
    }
    if (!seen.isEmpty())
        menu->addSeparator();
    menu->addAction(tr("Open Network &Connections"), this, [] { controlpanel::open(QStringLiteral("network")); });
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
    refreshNetworks();
}

void SystemIcons::showNetworkMenu(QPoint at)
{
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(tr("&View Available Wireless Networks"), this, [this, at] { showNetworks(at); });
    menu->addSeparator();
    menu->addAction(tr("Open Network &Connections"), this, [] { controlpanel::open(QStringLiteral("network")); });
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
}

void SystemIcons::connectTo(const QString &ssid, bool secured)
{
    // A network joined before has its key already.
    run(this, QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME"), QStringLiteral("connection"), QStringLiteral("show")}, [this, ssid, secured](const QString &saved) {
        QString key;
        if (secured && !terseFields(saved).join(QLatin1Char('\n')).split(QLatin1Char('\n')).contains(ssid)) {
            bool ok = false;
            key = QInputDialog::getText(nullptr, tr("Wireless Network Connection"), tr("The network '%1' requires a network key.\n\nNetwork &key:").arg(ssid), QLineEdit::Password, QString(), &ok);
            if (!ok)
                return;
        }
        // The key goes by its standard input: a command line can be read by anyone.
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::finished, this, [process, ssid](int code) {
            if (code != 0)
                QMessageBox::warning(nullptr, tr("Wireless Network Connection"), tr("Could not connect to '%1'.\n%2").arg(ssid, QString::fromUtf8(process->readAll()).trimmed()));
            process->deleteLater();
        });
        process->start(QStringLiteral("nmcli"), {QStringLiteral("--ask"), QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("connect"), ssid});
        if (!key.isEmpty())
            process->write(key.toUtf8() + '\n');
        process->closeWriteChannel();
    });
}
