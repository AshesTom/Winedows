// SPDX-License-Identifier: LGPL-2.1-or-later
// Safely Remove Hardware on Linux: systray's hot-plug icon (stobject.dll) while a removable
// disk is plugged in, its menus, hotplug.dll's window and its "Safe To Remove Hardware"
// balloon, in the words and pictures of the user's XP files. The disks are lsblk's hot-plug
// or removable ones; stopping one unmounts its file systems and powers it off (udisksctl).
#pragma once

#include "tray.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class QProcess;
class QWidget;

namespace hotplug {

struct Device
{
    QString path;                     // the disk, /dev/sdb
    QString name;                     // as XP names it: USB Mass Storage Device
    QStringList drives;               // its file systems, by their labels (XP's drive letters)
    QStringList mounted;              // the partitions to unmount before it stops
};

class SafeRemoval : public QObject
{
    Q_OBJECT
public:
    explicit SafeRemoval(Tray *tray);
    ~SafeRemoval() override;
    const QList<Device> &devices() const { return m_devices; }
    // hotplug.dll's window
    void showWindow();
    // Unmounts and powers off DEVICE; the balloon says when it can go.
    void stop(const Device &device);

Q_SIGNALS:
    void devicesChanged();

private:
    void refresh();
    void update(const QList<Device> &devices);
    bool click(quint32 id, const char *op, QPoint at);
    void showDevicesMenu();
    void showIcon();

    Tray *m_tray;
    QList<Device> m_devices;
    QTimer m_timer, m_clickTimer;
    QPoint m_clickAt;
    quint64 m_balloon = 0;            // "Safe To Remove Hardware", while it is shown
    QProcess *m_monitor = nullptr;
    QPointer<QWidget> m_window;
};

// " - Drive(E:)", " - Drives(E:, F:)" after the device's name (stobject.dll's 219 and 220)
QString menuName(const Device &device);

} // namespace hotplug
