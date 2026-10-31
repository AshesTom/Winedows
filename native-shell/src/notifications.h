// SPDX-License-Identifier: LGPL-2.1-or-later
// org.freedesktop.Notifications, when no other server has it on the session bus: Linux
// programs' notifications become XP's balloons. As on XP, where a balloon always comes
// from an icon, each program has an icon in the notification area while its balloons are
// shown or waiting; one balloon of a program replaces the one it has up.
#pragma once

#include "tray.h"

#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

class NotificationServer : public QObject
{
    Q_OBJECT
public:
    explicit NotificationServer(Tray *tray);

    QStringList capabilities() const;
    uint notify(const QString &appName, uint replacesId, const QString &appIcon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout);
    void close(uint id);

Q_SIGNALS:
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &actionKey);

private:
    struct Shown { uint id; QString app; quint64 balloon; QStringList actions; };
    void publishIcons();
    void finished(uint id, uint reason);

    Tray *m_tray;
    QHash<QString, TrayIcon> m_icons;  // by program
    QList<Shown> m_shown;
    uint m_ids = 0;
    quint32 m_iconIds = 0;
};

class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationsAdaptor(NotificationServer *server);

public Q_SLOTS:
    QStringList GetCapabilities();
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expire_timeout);
    void CloseNotification(uint id);
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);

private:
    NotificationServer *m_server;
};
