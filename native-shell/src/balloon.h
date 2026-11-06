// SPDX-License-Identifier: LGPL-2.1-or-later
// The notification area's balloon tips, as XP shows them: one at a time and the others
// queued, each pointing at its icon, timing out only while the user is at the computer.
// The body is comctl32's own drawing in the info colours; the visual style gives only
// its close button (TOOLTIP, CLOSE). Its measures come from captures of XP.
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class BalloonWindow;

struct BalloonTip
{
    // NIIF_NONE, NIIF_INFO, NIIF_WARNING, NIIF_ERROR and NIIF_USER (the image)
    enum Icon { None, Info, Warning, Error, User };
    enum Reason { TimedOut, Dismissed, Clicked, Cancelled };

    quint32 icon = 0;                 // the tray icon it points at
    QString title, text;
    Icon kind = None;
    QImage image;
    int timeout = 0;                  // milliseconds; clamped to XP's 10 to 30 seconds
    bool sound = true;                // NIIF_NOSOUND when false
    bool closeButton = true;          // a tooltip's balloon (TTS_BALLOON without TTS_CLOSE) has none
    std::function<void(Reason)> closed;
};

class Balloons : public QObject
{
    Q_OBJECT
public:
    explicit Balloons(QObject *parent = nullptr);
    ~Balloons() override;
    // Where a tray icon is on the screen; an empty rectangle while it is not shown.
    void setLocator(std::function<QRect(quint32)> locator) { m_locator = std::move(locator); }

    // A balloon of an icon replaces the one it shows or has queued, as Shell_NotifyIcon's NIF_INFO does.
    quint64 show(const BalloonTip &tip);
    void cancel(quint64 serial);
    // The icons have moved, or some have gone (their balloons go with them).
    void iconsChanged();
    // The clock's tooltip and the icons' wait while a balloon is up.
    bool showing() const { return m_window; }

private:
    struct Entry { quint64 serial; BalloonTip tip; };
    void next();
    void finish(BalloonTip::Reason reason);
    void tick();

    std::function<QRect(quint32)> m_locator;
    QList<Entry> m_queue;
    Entry m_current{};
    QPointer<BalloonWindow> m_window;
    QTimer m_ticks;
    qint64 m_shownFor = 0;
    QElapsedTimer m_lastTick;
    quint64 m_serials = 0;
};

class BalloonWindow : public QWidget
{
    Q_OBJECT
public:
    explicit BalloonWindow(const BalloonTip &tip);
    // Its stem on ICON (a tray icon's place on the screen), the body on the screen's side of it.
    void place(const QRect &icon);
    // Its stem's tip on TIP, the body on the screen's side of it.
    void placeAt(QPoint tip);

Q_SIGNALS:
    void clicked();
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void layout();
    void placeStem(QPoint anchor, const QRect &room);
    QRegion shape() const;
    QRect closeRect() const;
    void drawClose(QPainter &painter) const;

    BalloonTip m_tip;
    QImage m_icon;
    QFont m_font, m_titleFont;
    QSize m_body;                     // the body's size, border included
    QRect m_titleRect, m_textRect;
    bool m_stemDown = true, m_stemRight = true;
    int m_stemX = 0;                  // the stem's straight edge, in the body's columns
    bool m_hot = false, m_pressed = false;
};
