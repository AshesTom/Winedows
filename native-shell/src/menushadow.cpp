// SPDX-License-Identifier: LGPL-2.1-or-later
#include "menushadow.h"

#include "x11.h"

#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QImage>
#include <QRegion>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>
#include <QtMath>

#include <xcb/xcb.h>

namespace menushadow {
namespace {

// XP's CS_DROPSHADOW profile, measured on the reference machine and written down in
// wine/patches/0033-winex11-external-drop-shadow.patch: how much light the shadow takes across its
// five pixels from the window, how it fades in along the edge and out at the corner. win32k's
// xxxAddShadow (sub_bf854347) makes the layered SysShadow window of the window's size plus five,
// and sub_bf8544b3 draws it.
const int SPREAD = 5;
const float SHADOW_EDGE[SPREAD] = {0.442f, 0.328f, 0.168f, 0.055f, 0.012f};
const float SHADOW_FADE[6] = {0.113f, 0.339f, 0.663f, 0.885f, 0.975f, 1.0f};
const float SHADOW_CORNER[SPREAD] = {0.882f, 0.665f, 0.337f, 0.118f, 0.018f};

double darkness(int across, int along, int length)
{
    float fade = along < 6 ? SHADOW_FADE[along] : 1.0f;
    // The strip's end is its corner, where the two edges meet.
    if (along >= length - SPREAD)
        fade = qMin(fade, SHADOW_CORNER[along - (length - SPREAD)]);
    return double(SHADOW_EDGE[across]) * fade;
}

// The shadow is an override-redirect window below its popup, with an L-shaped mask: the five
// columns at the popup's right and the five rows under it. Its picture is the screen beneath it,
// darkened by the profile, as XP's layered window is composited; a rootless server that cannot be
// read back gets black at the same alpha instead.
class Shadow : public QWidget
{
public:
    explicit Shadow(QWidget *target)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::WindowTransparentForInput
                              | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint)
        , m_target(target)
    {
        setProperty("win4wineShadow", true);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setWindowTitle(QStringLiteral("SysShadow"));
    }

    void follow()
    {
        QWidget *target = m_target.data();
        if (!target || !target->isVisible()) {
            hide();
            return;
        }
        const QRect frame = target->frameGeometry();
        QRect room = QRect(frame.topLeft(), frame.size() + QSize(SPREAD, SPREAD));
        const QRect screen = target->screen()->geometry();
        room = room.intersected(screen);
        if (room.width() <= 0 || room.height() <= 0) {
            hide();
            return;
        }
        const bool visible = isVisible();
        if (visible)
            hide();
        setGeometry(room);
        const QPoint origin = room.topLeft() - frame.topLeft();
        const int popupW = frame.width(), popupH = frame.height();
        // (origin is 0 or negative: the five rows and columns are clipped at the screen's edge.)
        QRegion mask;
        const QRect right(origin.x() + popupW, origin.y(), SPREAD, popupH + SPREAD);
        const QRect bottom(origin.x(), origin.y() + popupH, popupW + SPREAD, SPREAD);
        mask += right.intersected(QRect(QPoint(0, 0), room.size()));
        mask += bottom.intersected(QRect(QPoint(0, 0), room.size()));
        setMask(mask);
        capture(frame, room);
        raiseBelow(target);
        show();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_picture);
    }

private:
    void capture(const QRect &frame, const QRect &room)
    {
        QPixmap beneath = m_target->screen()->grabWindow(0, room.x(), room.y(), room.width(), room.height());
        QImage picture = beneath.isNull() ? QImage() : beneath.toImage().convertToFormat(QImage::Format_RGB32);
        if (!picture.isNull() && picture.size() != room.size())
            picture = picture.scaled(room.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        bool readBack = !picture.isNull();
        if (readBack) {
            // A rootless server can answer with an all-black picture instead of nothing. Treat it as
            // unreadable: a real black background loses nothing by the alpha path.
            bool anyLight = false;
            for (int y = 0; y < picture.height() && !anyLight; y += qMax(1, picture.height() / 8))
                for (int x = 0; x < picture.width() && !anyLight; x += qMax(1, picture.width() / 8))
                    anyLight = (picture.pixel(x, y) & 0xffffff) != 0;
            readBack = anyLight;
        }
        if (!readBack) {
            // A rootless X server cannot read the root back: black, with the profile as its alpha.
            picture = QImage(room.size(), QImage::Format_ARGB32_Premultiplied);
            picture.fill(Qt::transparent);
        } else {
            picture = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        const int popupW = frame.width(), popupH = frame.height();
        for (int y = 0; y < room.height(); ++y) {
            for (int x = 0; x < room.width(); ++x) {
                const int mx = room.x() + x - frame.x(), my = room.y() + y - frame.y();
                double dark = 0;
                if (mx >= popupW && my < popupH + SPREAD)
                    dark = darkness(qBound(0, mx - popupW, SPREAD - 1), qBound(0, my, popupH + SPREAD - 1), popupH + SPREAD);
                else if (my >= popupH && mx < popupW + SPREAD)
                    dark = darkness(qBound(0, my - popupH, SPREAD - 1), qBound(0, mx, popupW + SPREAD - 1), popupW + SPREAD);
                if (dark <= 0)
                    continue;
                if (!readBack) {
                    const int alpha = qRound(dark * 255.0);
                    picture.setPixel(x, y, qRgba(0, 0, 0, alpha));
                } else {
                    QRgb pixel = picture.pixel(x, y);
                    const int keep = qRound(255.0 * (1.0 - dark));
                    picture.setPixel(x, y, qRgba(qRed(pixel) * keep / 255, qGreen(pixel) * keep / 255, qBlue(pixel) * keep / 255, 255));
                }
            }
        }
        m_picture = picture;
    }

    void raiseBelow(QWidget *above)
    {
        xcb_connection_t *connection = x11::connection();
        if (!connection)
            return;
        const quint32 values[] = {quint32(above->winId()), XCB_STACK_MODE_BELOW};
        xcb_configure_window(connection, winId(), XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, values);
        xcb_flush(connection);
    }

    QPointer<QWidget> m_target;
    QImage m_picture;
};

class Watcher : public QObject
{
public:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (!object->isWidgetType())
            return false;
        auto *widget = static_cast<QWidget *>(object);
        if (widget->property("win4wineShadow").toBool())
            return false;
        switch (event->type()) {
        case QEvent::Show:
            if (wantsShadow(widget)) {
                Shadow *&shadow = m_shadows[widget];
                if (!shadow)
                    shadow = new Shadow(widget);
                shadow->follow();
            }
            break;
        case QEvent::Move:
        case QEvent::Resize:
            if (Shadow *shadow = m_shadows.value(widget))
                shadow->follow();
            break;
        case QEvent::Hide:
        case QEvent::Close:
            if (Shadow *shadow = m_shadows.take(widget)) {
                shadow->hide();
                shadow->deleteLater();
            }
            break;
        case QEvent::Destroy:
            if (Shadow *shadow = m_shadows.take(widget)) {
                shadow->hide();
                shadow->deleteLater();
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    static bool wantsShadow(QWidget *widget)
    {
        if (!widget->isWindow())
            return false;
        const Qt::WindowType type = Qt::WindowType(int(widget->windowFlags()) & int(Qt::WindowType_Mask));
        if (type != Qt::Popup && type != Qt::ToolTip)
            return false;
        // XP's Start panel clears CS_DROPSHADOW while it shows (docs/reverse-status.md, section 13).
        if (qstrcmp(widget->metaObject()->className(), "StartPanel") == 0)
            return false;
        return true;
    }

    QHash<QWidget *, Shadow *> m_shadows;
};

} // namespace

void install()
{
    if (qEnvironmentVariableIsSet("WIN4WINE_NO_MENU_SHADOW"))
        return;
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/win4wine/native-explorer.conf");
    // Display Properties' "Show shadows under menus" (SPI_GETDROPSHADOW), on as XP has it.
    if (!QSettings(path, QSettings::IniFormat).value(QStringLiteral("Effects/DropShadow"), true).toBool())
        return;
    static Watcher *watcher = new Watcher;
    qApp->installEventFilter(watcher);
}

} // namespace menushadow
