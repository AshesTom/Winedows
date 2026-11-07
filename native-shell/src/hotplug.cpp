// SPDX-License-Identifier: LGPL-2.1-or-later
#include "hotplug.h"

#include "balloon.h"
#include "theme.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>

namespace hotplug {

namespace {

// The notification area's own icons: beside the Security Center's 0xfffffff3 and the reminders' 0xfffffff4
const quint32 ICON_HOTPLUG = 0xfffffff5;
// stobject.dll: its icon, the tip, the right-click menu's item, the left-click menu's and the drives after a name
const int ICON_SYSTRAY = 210;
const int STRING_TIP = 211, STRING_OPEN = 215, STRING_REMOVE = 216, STRING_DRIVE = 219, STRING_DRIVES = 220;
// hotplug.dll: Safely Remove Hardware (IDD 300) and Stop a Hardware device (IDD 320), and their words
const int DIALOG_REMOVE = 300, DIALOG_CONFIRM = 320;
const int CONTROL_TREE = 301, CONTROL_STOP = 305, CONTROL_COMPONENTS = 307, CONTROL_DESCRIPTION = 309,
          CONTROL_PROPERTIES = 311, CONTROL_CLOSE = 8, CONTROL_CONFIRM_LIST = 321;
const int STRING_UNKNOWN = 1003, STRING_AT = 1006, STRING_PROBLEM_EJECTING = 2254, STRING_IN_USE = 1205,
          STRING_SAFE = 2260, STRING_SAFE_TITLE = 2261;
// A burst of udev events is one look at the disks.
const int REFRESH_DELAY_MS = 300;

const std::optional<w4w::PeResources> &stobject()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("stobject.dll")));
    return resources;
}

const std::optional<w4w::PeResources> &hotplugDll()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("hotplug.dll")));
    return resources;
}

QString string(const std::optional<w4w::PeResources> &pe, int id, const char *fallback)
{
    const QString text = pe ? xpfiles::string(*pe, id) : QString();
    return text.isEmpty() ? QString::fromUtf8(fallback) : text;
}

QImage trayIcon(int size)
{
    return stobject() ? xpfiles::icon(*stobject(), ICON_SYSTRAY, size) : QImage();
}

// lsblk's disks that can go: hot-plug or removable, not optical drives (XP ejects those, it does not
// stop them); a USB disk is XP's USB Mass Storage Device, others go by their model.
QList<Device> parseDevices(const QByteArray &json)
{
    const QJsonArray blocks = QJsonDocument::fromJson(json).object().value(QLatin1String("blockdevices")).toArray();
    QList<Device> devices;
    QHash<QString, int> byName;
    for (const QJsonValue &value : blocks) {
        const QJsonObject block = value.toObject();
        if (block.value(QLatin1String("type")).toString() != QLatin1String("disk")
            || !(block.value(QLatin1String("hotplug")).toBool() || block.value(QLatin1String("rm")).toBool()))
            continue;
        Device device;
        device.path = block.value(QLatin1String("path")).toString();
        if (block.value(QLatin1String("tran")).toString() == QLatin1String("usb"))
            device.name = QObject::tr("USB Mass Storage Device");
        else
            device.name = (block.value(QLatin1String("vendor")).toString().trimmed() + QLatin1Char(' ')
                           + block.value(QLatin1String("model")).toString().trimmed()).trimmed();
        if (device.name.isEmpty())
            device.name = string(hotplugDll(), STRING_UNKNOWN, "Unknown Device");
        byName.insert(block.value(QLatin1String("name")).toString(), int(devices.size()));
        devices.append(device);
    }
    // Their file systems: a whole-disk one, or the partitions'
    for (const QJsonValue &value : blocks) {
        const QJsonObject block = value.toObject();
        const QString parent = block.value(QLatin1String("type")).toString() == QLatin1String("disk")
            ? block.value(QLatin1String("name")).toString() : block.value(QLatin1String("pkname")).toString();
        const auto it = byName.constFind(parent);
        if (it == byName.cend())
            continue;
        Device &device = devices[*it];
        QStringList mountpoints;
        for (const QJsonValue &point : block.value(QLatin1String("mountpoints")).toArray())
            if (!point.toString().isEmpty())
                mountpoints.append(point.toString());
        if (mountpoints.isEmpty())
            continue;
        device.mounted.append(block.value(QLatin1String("path")).toString());
        const QString label = block.value(QLatin1String("label")).toString();
        device.drives.append(label.isEmpty() ? mountpoints.first().section(QLatin1Char('/'), -1) : label);
    }
    return devices;
}

} // namespace

QString menuName(const Device &device)
{
    if (device.drives.isEmpty())
        return device.name;
    const QString drives = device.drives.join(QStringLiteral(", "));
    return device.name + (device.drives.size() == 1 ? string(stobject(), STRING_DRIVE, " - Drive(%s)")
                                                     : string(stobject(), STRING_DRIVES, " - Drives(%s)")).replace(QLatin1String("%s"), drives);
}

SafeRemoval::SafeRemoval(Tray *tray)
    : m_tray(tray)
{
    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, &SafeRemoval::showDevicesMenu);
    if (QStandardPaths::findExecutable(QStringLiteral("lsblk")).isEmpty())
        return;
    m_timer.setSingleShot(true);
    m_timer.setInterval(REFRESH_DELAY_MS);
    connect(&m_timer, &QTimer::timeout, this, &SafeRemoval::refresh);
    // A disk plugged in or out, or its partitions changed: udev's block events (mounts come with
    // udisks' change events)
    if (!QStandardPaths::findExecutable(QStringLiteral("udevadm")).isEmpty()) {
        m_monitor = new QProcess(this);
        m_monitor->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_monitor, &QProcess::readyRead, this, [this] {
            m_monitor->readAll();
            m_timer.start();
        });
        m_monitor->start(QStringLiteral("udevadm"), {QStringLiteral("monitor"), QStringLiteral("--udev"), QStringLiteral("--subsystem-match=block")});
    }
    refresh();
}

SafeRemoval::~SafeRemoval()
{
    if (m_monitor) {
        m_monitor->disconnect(this);
        m_monitor->kill();
        m_monitor->waitForFinished(500);
    }
    delete m_window;
}

void SafeRemoval::refresh()
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        update(parseDevices(process->readAllStandardOutput()));
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(QStringLiteral("lsblk"), {QStringLiteral("-J"), QStringLiteral("-o"),
                                             QStringLiteral("NAME,PATH,TYPE,RM,HOTPLUG,TRAN,VENDOR,MODEL,LABEL,MOUNTPOINTS,PKNAME")});
}

void SafeRemoval::update(const QList<Device> &devices)
{
    bool same = devices.size() == m_devices.size();
    for (int i = 0; same && i < devices.size(); ++i)
        same = devices[i].path == m_devices[i].path && devices[i].name == m_devices[i].name && devices[i].drives == m_devices[i].drives;
    m_devices = devices;
    if (same)
        return;
    Q_EMIT devicesChanged();
    showIcon();
}

// The icon while a device can be stopped, and while the balloon of the last one stopped points at it
void SafeRemoval::showIcon()
{
    if (m_devices.isEmpty() && !m_balloon) {
        m_tray->setOwnIcons(this, {}, nullptr);
        return;
    }
    TrayIcon icon;
    icon.id = ICON_HOTPLUG;
    icon.image = trayIcon(ShellTheme::instance()->scaled(16));
    icon.tooltip = string(stobject(), STRING_TIP, "Safely Remove Hardware");
    icon.name = icon.tooltip;
    m_tray->setOwnIcons(this, {icon}, [this](quint32 id, const char *op, QPoint at) { return click(id, op, at); });
}

// A left click lists the devices to stop, once it is not a double click's first (which opens the
// window); a right click has only the window's item, in bold.
bool SafeRemoval::click(quint32 id, const char *op, QPoint at)
{
    if (id != ICON_HOTPLUG)
        return false;
    if (qstrcmp(op, "context") != 0) {
        if (m_clickTimer.isActive()) {
            m_clickTimer.stop();
            showWindow();
        } else {
            m_clickAt = at;
            m_clickTimer.start(QApplication::doubleClickInterval());
        }
        return true;
    }
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *open = menu->addAction(string(stobject(), STRING_OPEN, "&Safely Remove Hardware"), this, &SafeRemoval::showWindow);
    menu->setDefaultAction(open);
    Tray::popupMenu(menu, at);
    return true;
}

void SafeRemoval::showDevicesMenu()
{
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const Device &device : std::as_const(m_devices))
        menu->addAction(string(stobject(), STRING_REMOVE, "Safely remove %s").replace(QLatin1String("%s"), menuName(device)),
                        this, [this, device] { stop(device); });
    if (!menu->isEmpty())
        Tray::popupMenu(menu, m_clickAt);
    else
        delete menu;
}

void SafeRemoval::stop(const Device &device)
{
    // Each file system unmounted, then the disk powered off; the first failure stops there.
    auto steps = std::make_shared<QList<QStringList>>();
    for (const QString &partition : device.mounted)
        steps->append({QStringLiteral("unmount"), QStringLiteral("--no-user-interaction"), QStringLiteral("-b"), partition});
    steps->append({QStringLiteral("power-off"), QStringLiteral("--no-user-interaction"), QStringLiteral("-b"), device.path});
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, device, steps, next] {
        if (steps->isEmpty()) {
            BalloonTip tip;
            tip.icon = ICON_HOTPLUG;
            tip.kind = BalloonTip::Info;
            tip.title = string(hotplugDll(), STRING_SAFE_TITLE, "Safe To Remove Hardware");
            tip.text = string(hotplugDll(), STRING_SAFE, "The '%s' device can now be safely removed from the system.")
                           .replace(QLatin1String("%s"), device.name);
            tip.closed = [this](BalloonTip::Reason) {
                m_balloon = 0;
                showIcon();
            };
            m_balloon = m_tray->balloons()->show(tip);
            m_timer.start();
            return;
        }
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::finished, this, [this, process, device, steps, next](int code, QProcess::ExitStatus status) {
            process->deleteLater();
            if (status != QProcess::NormalExit || code != 0) {
                QMessageBox::warning(m_window, string(hotplugDll(), STRING_PROBLEM_EJECTING, "Problem Ejecting %s").replace(QLatin1String("%s"), device.name),
                                     string(hotplugDll(), STRING_IN_USE, "The device '%s' cannot be stopped because a program is still accessing it.")
                                         .replace(QLatin1String("%s"), device.name));
                m_timer.start();
                return;
            }
            steps->removeFirst();
            (*next)();
        });
        process->start(QStringLiteral("udisksctl"), steps->first());
    };
    (*next)();
}

namespace {

// Stop a Hardware device: the device and its drives, to confirm
void confirm(SafeRemoval *owner, const Device &device, QWidget *parent)
{
    if (!hotplugDll()) {
        owner->stop(device);
        return;
    }
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const xpdialog::Page page = xpdialog::build(*hotplugDll(), DIALOG_CONFIRM, dialog);
    dialog->setWindowTitle(page.caption);
    auto *list = new QListWidget(dialog);
    if (QWidget *place = page.controls.value(CONTROL_CONFIRM_LIST)) {
        list->setGeometry(place->geometry());
        place->hide();
    }
    const QIcon icon(QPixmap::fromImage(trayIcon(ShellTheme::instance()->scaled(16))));
    list->addItem(new QListWidgetItem(icon, device.name));
    for (const QString &drive : device.drives)
        list->addItem(new QListWidgetItem(icon, QStringLiteral("%1 (%2)").arg(QObject::tr("Generic volume"), drive)));
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        QObject::connect(ok, &QPushButton::clicked, dialog, [owner, device, dialog] {
            dialog->accept();
            owner->stop(device);
        });
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->setFixedSize(dialog->size());
    dialog->open();
}

} // namespace

void SafeRemoval::showWindow()
{
    if (m_window) {
        m_window->raise();
        m_window->activateWindow();
        return;
    }
    if (!hotplugDll())
        return;
    auto *dialog = new QDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_window = dialog;
    const xpdialog::Page page = xpdialog::build(*hotplugDll(), DIALOG_REMOVE, dialog);
    dialog->setWindowTitle(page.caption);
    dialog->setWindowIcon(QIcon(QPixmap::fromImage(trayIcon(16))));
    // DS_CONTEXTHELP: the "?" button
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint);
    auto *tree = new QTreeWidget(dialog);
    if (QWidget *place = page.controls.value(CONTROL_TREE)) {
        tree->setGeometry(place->geometry());
        place->hide();
        for (QLabel *label : dialog->findChildren<QLabel *>())
            if (label->buddy() == place)
                label->setBuddy(tree);
    }
    tree->setHeaderHidden(true);
    tree->setIconSize(QSize(16, 16));
    auto *description = qobject_cast<QLabel *>(page.controls.value(CONTROL_DESCRIPTION));
    auto *stopButton = qobject_cast<QPushButton *>(page.controls.value(CONTROL_STOP));
    auto *components = qobject_cast<QCheckBox *>(page.controls.value(CONTROL_COMPONENTS));
    if (QWidget *properties = page.controls.value(CONTROL_PROPERTIES))
        properties->setEnabled(false);
    // The devices, with their drives under them when the components are shown
    const auto fill = [this, tree, components] {
        const QString selected = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
        tree->clear();
        const QIcon icon(QPixmap::fromImage(trayIcon(ShellTheme::instance()->scaled(16))));
        for (const Device &device : std::as_const(m_devices)) {
            auto *item = new QTreeWidgetItem(tree, {device.name});
            item->setIcon(0, icon);
            item->setData(0, Qt::UserRole, device.path);
            if (components && components->isChecked())
                for (const QString &drive : device.drives)
                    new QTreeWidgetItem(item, {QStringLiteral("%1 (%2)").arg(QObject::tr("Generic volume"), drive)});
            if (device.path == selected || !tree->currentItem())
                tree->setCurrentItem(item);
        }
        tree->expandAll();
        tree->setRootIsDecorated(components && components->isChecked());
    };
    const auto current = [this, tree]() -> const Device * {
        QTreeWidgetItem *item = tree->currentItem();
        while (item && item->parent())
            item = item->parent();
        const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
        for (const Device &device : std::as_const(m_devices))
            if (device.path == path)
                return &device;
        return nullptr;
    };
    connect(tree, &QTreeWidget::currentItemChanged, dialog, [current, description, stopButton] {
        const Device *device = current();
        if (description)
            description->setText(device ? device->name + QLatin1Char(' ') + string(hotplugDll(), STRING_AT, "at %s").replace(QLatin1String("%s"), device->path) : QString());
        if (stopButton)
            stopButton->setEnabled(device);
    });
    if (components)
        connect(components, &QCheckBox::toggled, dialog, fill);
    connect(this, &SafeRemoval::devicesChanged, dialog, fill);
    if (stopButton)
        connect(stopButton, &QPushButton::clicked, dialog, [this, current, dialog] {
            if (const Device *device = current())
                confirm(this, *device, dialog);
        });
    if (auto *close = qobject_cast<QPushButton *>(page.controls.value(CONTROL_CLOSE)))
        connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    fill();
    // The list has the focus, its first device chosen
    tree->setFocus();
    if (stopButton)
        stopButton->setEnabled(current());
    dialog->setFixedSize(dialog->size());
    dialog->show();
}

} // namespace hotplug
