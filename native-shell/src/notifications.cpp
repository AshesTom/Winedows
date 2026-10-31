// SPDX-License-Identifier: LGPL-2.1-or-later
#include "notifications.h"

#include "balloon.h"
#include "theme.h"
#include "xpfiles.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>

namespace {

const char SERVICE[] = "org.freedesktop.Notifications";
const char PATH[] = "/org/freedesktop/Notifications";
// The notification area's own icons: 0xfff00001 and on (SystemIcons and the Security Center take the last few).
const quint32 FIRST_ICON = 0xfff00000;
// The specification's reasons for NotificationClosed; 4 is "undefined", here a newer balloon of the same program.
const uint EXPIRED = 1, DISMISSED = 2, CLOSED = 3, REPLACED = 4;
// Without a timeout of its own, a balloon stays XP's shortest time; "never" is XP's longest.
const int DEFAULT_TIMEOUT = 10000, NEVER_TIMEOUT = 30000;
const int ICON_SIZE = 16;
// user32.dll's small information icon, for programs without one
const int ICON_INFO = 104;

// image-data: (iiibiiay), width, height, stride, alpha, bits per sample, channels, pixels
QImage imageData(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return QImage();
    const QDBusArgument argument = value.value<QDBusArgument>();
    int width = 0, height = 0, stride = 0, bits = 0, channels = 0;
    bool alpha = false;
    QByteArray pixels;
    argument.beginStructure();
    argument >> width >> height >> stride >> alpha >> bits >> channels >> pixels;
    argument.endStructure();
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || bits != 8 || channels != (alpha ? 4 : 3)
        || stride < width * channels || pixels.size() < qsizetype(stride) * (height - 1) + width * channels)
        return QImage();
    const QImage view(reinterpret_cast<const uchar *>(pixels.constData()), width, height, stride,
                      alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    return view.copy();
}

// An icon's name in the theme, a path or a file: URL
QImage namedImage(const QString &name, int size)
{
    if (name.isEmpty())
        return QImage();
    const QString path = name.startsWith(QLatin1String("file://")) ? QUrl(name).toLocalFile() : name;
    if (path.startsWith(QLatin1Char('/')))
        return QFileInfo(path).isFile() ? QIcon(path).pixmap(size, size).toImage() : QImage();
    return QIcon::fromTheme(name).pixmap(size, size).toImage();
}

QImage infoIcon(int size)
{
    static const auto user32 = xpfiles::resources(xpfiles::find(QStringLiteral("user32.dll")));
    return user32 ? xpfiles::icon(*user32, ICON_INFO, size) : QImage();
}

// The body as plain text: the specification's few tags, for programs that send them anyway
QString plainText(const QString &body)
{
    static const QRegularExpression markup(QStringLiteral("<\\s*/?\\s*(b|i|u|a|img|br|p)\\b[^>]*>|&(amp|lt|gt|quot|apos|#\\d+);"),
                                           QRegularExpression::CaseInsensitiveOption);
    QString text = body;
    if (markup.match(text).hasMatch()) {
        text.replace(QRegularExpression(QStringLiteral("<\\s*br\\s*/?\\s*>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
        text = QTextDocumentFragment::fromHtml(text.replace(QLatin1Char('\n'), QLatin1String("<br>"))).toPlainText();
    }
    return text.trimmed();
}

} // namespace

NotificationServer::NotificationServer(Tray *tray)
    : QObject(tray)
    , m_tray(tray)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    // Another desktop's server keeps the notifications; the shell only takes a free name.
    const auto reply = bus.interface()->registerService(QLatin1String(SERVICE), QDBusConnectionInterface::DontQueueService,
                                                        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        qInfo("explorer: another program shows the notifications (%s is taken)", SERVICE);
        return;
    }
    new NotificationsAdaptor(this);
    bus.registerObject(QLatin1String(PATH), this);
}

QStringList NotificationServer::capabilities() const
{
    return {QStringLiteral("body"), QStringLiteral("icon-static")};
}

uint NotificationServer::notify(const QString &appName, uint replacesId, const QString &appIcon, const QString &summary,
                                const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout)
{
    const int size = ShellTheme::instance()->scaled(ICON_SIZE);
    // The program: by its desktop file when it says, else by its name.
    QString app = hints.value(QStringLiteral("desktop-entry")).toString();
    if (app.isEmpty())
        app = appName.isEmpty() ? QStringLiteral("notify") : appName;

    uint id = 0;
    for (int i = 0; i < m_shown.size(); ++i)
        if (replacesId && m_shown[i].id == replacesId) {
            // Its balloon goes without a NotificationClosed: the notification goes on.
            id = replacesId;
            const quint64 balloon = m_shown.takeAt(i).balloon;
            m_tray->balloons()->cancel(balloon);
            break;
        }
    if (!id) {
        if (++m_ids == 0)
            ++m_ids;
        id = m_ids;
    }

    // The tray's icon is the program's; the balloon's is the notification's own picture, if any.
    QImage image;
    for (const char *key : {"image-data", "image_data", "icon_data"})
        if (image.isNull())
            image = imageData(hints.value(QLatin1String(key)));
    for (const char *key : {"image-path", "image_path"})
        if (image.isNull())
            image = namedImage(hints.value(QLatin1String(key)).toString(), size);
    QImage programIcon = namedImage(appIcon, size);
    if (programIcon.isNull())
        programIcon = namedImage(hints.value(QStringLiteral("desktop-entry")).toString(), size);
    if (!m_icons.contains(app)) {
        TrayIcon icon;
        icon.id = FIRST_ICON + (++m_iconIds & 0xfffff);
        icon.tooltip = appName.isEmpty() ? summary : appName;
        m_icons.insert(app, icon);
    }
    TrayIcon &icon = m_icons[app];
    icon.image = !programIcon.isNull() ? programIcon : !image.isNull() ? image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation) : infoIcon(size);
    publishIcons();

    BalloonTip tip;
    tip.icon = icon.id;
    // XP's balloons need a text: a summary alone is the text.
    tip.text = plainText(body);
    tip.title = summary;
    if (tip.text.isEmpty())
        std::swap(tip.text, tip.title);
    const int urgency = hints.value(QStringLiteral("urgency"), 1).toInt();
    if (urgency >= 2)
        tip.kind = BalloonTip::Error;
    else if (!image.isNull() || !programIcon.isNull()) {
        tip.kind = BalloonTip::User;
        tip.image = image.isNull() ? programIcon : image;
    } else {
        tip.kind = BalloonTip::Info;
    }
    tip.timeout = expireTimeout < 0 ? DEFAULT_TIMEOUT : expireTimeout == 0 ? NEVER_TIMEOUT : expireTimeout;
    tip.sound = !hints.value(QStringLiteral("suppress-sound")).toBool();
    tip.closed = [this, id](BalloonTip::Reason reason) {
        const auto it = std::find_if(m_shown.cbegin(), m_shown.cend(), [id](const Shown &shown) { return shown.id == id; });
        if (it == m_shown.cend())
            return;
        if (reason == BalloonTip::Clicked && it->actions.contains(QLatin1String("default")))
            Q_EMIT actionInvoked(id, QStringLiteral("default"));
        finished(id, reason == BalloonTip::TimedOut ? EXPIRED : reason == BalloonTip::Cancelled ? REPLACED : DISMISSED);
    };
    // The actions' keys, every other entry
    QStringList keys;
    for (int i = 0; i + 1 < actions.size(); i += 2)
        keys.append(actions[i]);
    m_shown.append({id, app, 0, keys});
    const quint64 balloon = m_tray->balloons()->show(tip);
    for (Shown &shown : m_shown)
        if (shown.id == id)
            shown.balloon = balloon;
    return id;
}

void NotificationServer::close(uint id)
{
    for (const Shown &shown : std::as_const(m_shown))
        if (shown.id == id) {
            const quint64 balloon = shown.balloon;
            finished(id, CLOSED);
            m_tray->balloons()->cancel(balloon);
            return;
        }
}

void NotificationServer::finished(uint id, uint reason)
{
    const auto it = std::find_if(m_shown.begin(), m_shown.end(), [id](const Shown &shown) { return shown.id == id; });
    if (it == m_shown.end())
        return;
    const QString app = it->app;
    m_shown.erase(it);
    Q_EMIT notificationClosed(id, reason);
    // The program's icon goes with its last balloon.
    if (std::none_of(m_shown.cbegin(), m_shown.cend(), [&app](const Shown &shown) { return shown.app == app; })) {
        m_icons.remove(app);
        publishIcons();
    }
}

void NotificationServer::publishIcons()
{
    m_tray->setOwnIcons(this, m_icons.values(), [this](quint32 id, const char *, QPoint) {
        // A click on a program's icon shows nothing of its own: it opens the waiting balloon's program, as its default action.
        for (const Shown &shown : std::as_const(m_shown))
            if (m_icons.value(shown.app).id == id) {
                if (shown.actions.contains(QLatin1String("default")))
                    Q_EMIT actionInvoked(shown.id, QStringLiteral("default"));
                return true;
            }
        return false;
    });
}

NotificationsAdaptor::NotificationsAdaptor(NotificationServer *server)
    : QDBusAbstractAdaptor(server)
    , m_server(server)
{
    connect(server, &NotificationServer::notificationClosed, this, &NotificationsAdaptor::NotificationClosed);
    connect(server, &NotificationServer::actionInvoked, this, &NotificationsAdaptor::ActionInvoked);
}

QStringList NotificationsAdaptor::GetCapabilities()
{
    return m_server->capabilities();
}

uint NotificationsAdaptor::Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary,
                                  const QString &body, const QStringList &actions, const QVariantMap &hints, int expire_timeout)
{
    return m_server->notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout);
}

void NotificationsAdaptor::CloseNotification(uint id)
{
    m_server->close(id);
}

QString NotificationsAdaptor::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = QStringLiteral("Winedows");
    version = QStringLiteral("1.0");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("XP notification area");
}
