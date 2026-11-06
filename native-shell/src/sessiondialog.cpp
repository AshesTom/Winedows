// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sessiondialog.h"

#include "balloon.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>

#include <functional>

namespace sessiondialog {
namespace {

// msgina.dll: the Turn Off dialog's template, background, flag and icons (32 x 32, three states each)
const int DIALOG_TURN_OFF = 20100, BITMAP_BACKGROUND = 20142, BITMAP_FLAG = 20143, BITMAP_ICONS = 20150, DIALOG_LOG_OFF = 2250;
const int CONTROL_FLAG = 20101, CONTROL_TITLE = 20102, CONTROL_OFF = 20103, CONTROL_STANDBY = 20104, CONTROL_RESTART = 20105,
          CONTROL_HIBERNATE = 20106;
const int LABEL_OFF = 20107, LABEL_STANDBY = 20108, LABEL_RESTART = 20109;
const int STRIP_OFF = 0, STRIP_STANDBY = 3, STRIP_RESTART = 6, STRIP_DISABLED = 9, ICON_SIZE = 32;
// msgina's strings: the title's face and size, the labels' face and size, the buttons' tips (0x759970ec:
// Turn Off 20129, Stand By 20130 or, when it can also hibernate, 20131, Restart 20132, Hibernate 20133)
const int STRING_TITLE_FACE = 20125, STRING_TITLE_SIZE = 20126, STRING_LABEL_FACE = 20127, STRING_LABEL_SIZE = 20128;
const int TIP_OFF = 20129, TIP_STANDBY = 20130, TIP_STANDBY_SHIFT = 20131, TIP_RESTART = 20132, TIP_HIBERNATE = 20133;
// shell32.dll's Log Off Windows with Switch User (LogoffWindowsDialog, 0x30157296): dialog 1089, its
// background 14354, flag 14355 and icons 14356 (14351 and 14353 at 256 colours and fewer), the title's
// face and size 14342/14343, the labels' 14345/14348, the tips 14349 and 14350
const int DIALOG_SWITCH = 1089, BITMAP_SWITCH_BACKGROUND = 14354, BITMAP_SWITCH_FLAG = 14355, BITMAP_SWITCH_ICONS = 14356;
const int CONTROL_SWITCH_FLAG = 14336, CONTROL_SWITCH_TITLE = 14337, CONTROL_SWITCH_USER = 14338, CONTROL_LOG_OFF = 14339,
          LABEL_SWITCH_USER = 14340, LABEL_LOG_OFF = 14341;
const int STRING_SWITCH_TITLE_FACE = 14342, STRING_SWITCH_TITLE_SIZE = 14343, STRING_SWITCH_LABEL_FACE = 14345,
          STRING_SWITCH_LABEL_SIZE = 14348, TIP_SWITCH_USER = 14349, TIP_LOG_OFF = 14350;
const int STRIP_SWITCH_USER = 0, STRIP_LOG_OFF = 3;
// How long the screen takes to fade, and the affine fit per channel of XP's final state
const int FADE_MS = 1000, FADE_STEP_MS = 40;
const double DIM[3][4] = {{0.1435, 0.6198, 0.1571, -4.37}, {0.0952, 0.6789, 0.1426, -3.11}, {0.0722, 0.6450, 0.1985, -3.15}};
// The Shift key is looked at every 50 ms (msgina's timer 0xbd85); a tip comes after the pointer rests
// for SPI_GETMOUSEHOVERTIME (400 ms, TrackMouseEvent's default), half a cursor under it
const int SHIFT_POLL_MS = 50, HOVER_MS = 400, TIP_BELOW = 16;

const std::optional<w4w::PeResources> &msgina()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("msgina.dll")));
    return resources;
}

const std::optional<w4w::PeResources> &shell32()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    return resources;
}

QString resourceString(const std::optional<w4w::PeResources> &pe, int id, const char *fallback)
{
    QString text = pe ? xpfiles::string(*pe, id) : QString();
    text.remove(QChar(0));
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

QString msginaString(int id, const char *fallback)
{
    return resourceString(msgina(), id, fallback);
}

QImage dimmed(const QImage &source)
{
    QImage out = source.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
            const auto channel = [&](const double *fit) { return qBound(0, qRound(fit[0] * r + fit[1] * g + fit[2] * b + fit[3]), 255); };
            row[x] = qRgb(channel(DIM[0]), channel(DIM[1]), channel(DIM[2]));
        }
    }
    return out;
}

// One of the two dialogs drawn over the greyed screen: msgina's Turn Off Computer and shell32's Log Off
// Windows. Both are a template of owner-drawn buttons over a picture.
struct ButtonSpec
{
    int control, label, strip, tip;
    Choice choice;
    bool enabled = true;
};

struct DialogSpec
{
    const std::optional<w4w::PeResources> *resources;
    int dialog, background, flag, icons, flagControl, titleControl;
    int titleFace, titleSize, labelFace, labelSize;
    QList<ButtonSpec> buttons;
    int disabledStrip = -1;           // the strip's greyed picture, when it has one
    Choice focus = Cancelled;         // the button that has the focus first
    // Turn Off's Stand By: Hibernate while Shift is held (0x7599700e), when the computer can do both
    bool shiftHibernates = false;
};

class ButtonsDialog : public QDialog
{
public:
    explicit ButtonsDialog(const DialogSpec &spec)
        : QDialog(nullptr, Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint)
        , m_spec(spec)
    {
        const w4w::PeResources &pe = **spec.resources;
        QScreen *screen = QGuiApplication::primaryScreen();
        m_desktop = screen->grabWindow(0).toImage().convertToFormat(QImage::Format_RGB32);
        m_grey = dimmed(m_desktop);
        setGeometry(screen->geometry());
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent);

        // The places come from the template, the pictures from the file; the dialog centred across and
        // a third of the way down (0x759975a5).
        m_template = new QWidget(this);
        m_template->hide();
        const xpdialog::Page page = xpdialog::build(pe, spec.dialog, m_template);
        m_client = QRect((width() - m_template->width()) / 2 + 1, (height() - m_template->height() - 2) / 3 + 2, m_template->width(), m_template->height());
        const auto place = [&](int id) {
            QWidget *control = page.controls.value(id);
            return control ? control->geometry().translated(m_client.topLeft()) : QRect();
        };
        const auto words = [&](int id) {
            QString text;
            if (auto *button = qobject_cast<QPushButton *>(page.controls.value(id)))
                text = button->text();
            else if (auto *label = qobject_cast<QLabel *>(page.controls.value(id)))
                text = label->text();
            return text;
        };
        m_flag = place(spec.flagControl);
        m_title = place(spec.titleControl);
        m_titleText = words(spec.titleControl).remove(QLatin1Char('&'));
        for (const ButtonSpec &button : spec.buttons)
            m_buttons.append({place(button.control), place(button.label), words(button.control), button});
        // Turn Off's hidden Hibernate button lends its words to Stand By
        m_hibernateText = words(CONTROL_HIBERNATE);
        m_background = xpfiles::bitmap(pe, spec.background);
        m_flagImage = xpfiles::bitmap(pe, spec.flag);
        QImage strip = xpfiles::bitmap(pe, spec.icons).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < strip.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(strip.scanLine(y));
            for (int x = 0; x < strip.width(); ++x)
                if ((row[x] & 0xffffff) == 0xff00ff)
                    row[x] = 0;
        }
        m_strip = strip;
        for (int i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].spec.choice == spec.focus && m_buttons[i].spec.enabled)
                m_focus = i;

        auto *cancel = new QPushButton(words(2), this);
        cancel->setGeometry(place(2));
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

        // ("Franklin Gothic": XP's font mapper finds the Medium, the one it has.)
        QString titleFace = resourceString(*spec.resources, spec.titleFace, "Franklin Gothic");
        if (titleFace == QLatin1String("Franklin Gothic"))
            titleFace += QStringLiteral(" Medium");
        m_titleFont = xpfiles::themeFont(QStringLiteral("%1, %2").arg(titleFace, resourceString(*spec.resources, spec.titleSize, "14")), 96);
        m_labelFont = xpfiles::themeFont(QStringLiteral("%1, %2, bold")
                                           .arg(resourceString(*spec.resources, spec.labelFace, "Tahoma"), resourceString(*spec.resources, spec.labelSize, "8")), 96);

        m_fade = new QTimer(this);
        m_fade->setInterval(FADE_STEP_MS);
        connect(m_fade, &QTimer::timeout, this, [this] {
            m_opacity = qMin(1.0, m_opacity + double(FADE_STEP_MS) / FADE_MS);
            if (m_opacity >= 1.0)
                m_fade->stop();
            update();
        });
        m_fade->start();
        m_hover.setSingleShot(true);
        m_hover.setInterval(HOVER_MS);
        connect(&m_hover, &QTimer::timeout, this, &ButtonsDialog::showTip);
        if (spec.shiftHibernates) {
            connect(&m_shift, &QTimer::timeout, this, &ButtonsDialog::lookAtShift);
            m_shift.start(SHIFT_POLL_MS);
        }
    }

    Choice choice() const { return m_choice; }

protected:
    void showEvent(QShowEvent *) override
    {
        // Above everything, without the window manager: the keyboard comes here by asking.
        grabKeyboard();
    }
    void hideEvent(QHideEvent *) override
    {
        releaseKeyboard();
        hideTip();
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        // The buttons' keys: Tab and the arrows move the focus, Enter and Space choose, the access keys
        // choose at once, Escape cancels.
        switch (event->key()) {
        case Qt::Key_Escape:
            reject();
            return;
        case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_Backtab:
            moveFocus(-1);
            return;
        case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Tab:
            moveFocus(1);
            return;
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
            if (m_focus >= 0)
                choose(m_focus);
            return;
        default:
            break;
        }
        const QString typed = event->text().toLower();
        for (int i = 0; i < m_buttons.size() && !typed.isEmpty(); ++i)
            if (m_buttons[i].spec.enabled && accessKey(text(i)) == typed.at(0)) {
                choose(i);
                return;
            }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_desktop);
        painter.setOpacity(m_opacity);
        painter.drawImage(0, 0, m_grey);
        painter.setOpacity(1);
        painter.fillRect(m_client.adjusted(-1, -1, 1, 1), Qt::black);
        painter.drawImage(m_client, m_background);
        painter.drawImage(m_flag.x() + (m_flag.width() - m_flagImage.width()) / 2, m_flag.y() + (m_flag.height() - m_flagImage.height()) / 2, m_flagImage);
        painter.setFont(m_titleFont);
        painter.setPen(Qt::white);
        painter.drawText(m_title, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_titleText);
        painter.setFont(m_labelFont);
        for (int i = 0; i < m_buttons.size(); ++i) {
            const Button &button = m_buttons[i];
            const bool enabled = button.spec.enabled;
            // Normal, pressed and lit; the one with the focus is lit too.
            const int state = !enabled ? -1 : i == m_pressed ? 1 : (i == m_hot || (m_hot < 0 && i == m_focus)) ? 2 : 0;
            const int index = state >= 0 ? button.spec.strip + state : m_spec.disabledStrip >= 0 ? m_spec.disabledStrip : button.spec.strip;
            painter.drawImage(button.icon, m_strip, QRect(0, index * ICON_SIZE, ICON_SIZE, ICON_SIZE));
            painter.setPen(enabled ? QColor(Qt::white) : QColor(160, 160, 160));
            painter.drawText(button.label, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, QString(text(i)).remove(QLatin1Char('&')));
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hot = buttonAt(event->position().toPoint());
        if (hot != m_hot) {
            m_hot = hot;
            hideTip();
            update();
        }
        // TrackMouseEvent's hover: the pointer still for a while over the button
        m_tipAt = event->globalPosition().toPoint();
        if (m_hot >= 0 && !m_tipWindow)
            m_hover.start();
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        hideTip();
        m_pressed = buttonAt(event->position().toPoint());
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int at = buttonAt(event->position().toPoint());
        const bool same = at >= 0 && at == m_pressed;
        m_pressed = -1;
        update();
        if (same)
            choose(at);
    }

private:
    struct Button { QRect icon, label; QString text; ButtonSpec spec; };

    bool hibernating(int i) const
    {
        return m_spec.shiftHibernates && m_shiftHeld && m_buttons[i].spec.choice == StandBy;
    }
    QString text(int i) const
    {
        // (a computer that can only hibernate has Stand By called Hibernate for good)
        const bool hibernate = hibernating(i) || m_buttons[i].spec.choice == Hibernate;
        return hibernate && !m_hibernateText.isEmpty() ? m_hibernateText : m_buttons[i].text;
    }
    static QChar accessKey(const QString &words)
    {
        const qsizetype at = words.indexOf(QLatin1Char('&'));
        return at >= 0 && at + 1 < words.size() ? words.at(at + 1).toLower() : QChar();
    }
    int buttonAt(QPoint at) const
    {
        for (int i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].icon.contains(at) && m_buttons[i].spec.enabled)
                return i;
        return -1;
    }
    void moveFocus(int step)
    {
        for (int tries = 0, i = m_focus; tries < m_buttons.size(); ++tries) {
            i = (i + step + int(m_buttons.size())) % int(m_buttons.size());
            if (m_buttons[i].spec.enabled) {
                m_focus = i;
                update();
                return;
            }
        }
    }
    void choose(int i)
    {
        m_choice = hibernating(i) ? Hibernate : m_buttons[i].spec.choice;
        accept();
    }
    void lookAtShift()
    {
        const bool held = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
        if (held == m_shiftHeld)
            return;
        m_shiftHeld = held;
        // the tip over Stand By goes, as its words have changed
        if (m_hot >= 0 && m_buttons[m_hot].spec.choice == StandBy)
            hideTip();
        update();
    }
    void showTip()
    {
        if (m_hot < 0)
            return;
        const Button &button = m_buttons[m_hot];
        BalloonTip tip;
        tip.title = QString(text(m_hot)).remove(QLatin1Char('&'));
        const int words = hibernating(m_hot) ? TIP_HIBERNATE : button.spec.tip;
        tip.text = resourceString(*m_spec.resources, words, "").replace(QLatin1String("\r\n"), QLatin1String("\n")).remove(QLatin1Char('\r'));
        tip.closeButton = false;
        if (tip.text.isEmpty())
            return;
        m_tipWindow = new BalloonWindow(tip);
        m_tipWindow->placeAt(m_tipAt + QPoint(0, TIP_BELOW));
        m_tipWindow->show();
    }
    void hideTip()
    {
        m_hover.stop();
        delete m_tipWindow;
    }

    DialogSpec m_spec;
    Choice m_choice = Cancelled;
    QImage m_desktop, m_grey, m_background, m_flagImage, m_strip;
    QWidget *m_template = nullptr;
    QRect m_client, m_flag, m_title;
    QString m_titleText, m_hibernateText;
    QList<Button> m_buttons;
    QFont m_titleFont, m_labelFont;
    QTimer *m_fade = nullptr;
    QTimer m_hover, m_shift;
    QPointer<BalloonWindow> m_tipWindow;
    QPoint m_tipAt;
    double m_opacity = 0;
    int m_hot = -1, m_pressed = -1, m_focus = -1;
    bool m_shiftHeld = false;
};

} // namespace

Choice askTurnOff(bool canStandBy, bool canHibernate)
{
    if (!msgina())
        return Cancelled;
    // 0x759975a5: Stand By greyed when the computer can neither stand by nor hibernate, called
    // Hibernate when it can only hibernate; with both, Shift turns it into Hibernate. The focus starts on
    // Stand By, or on Turn Off when Stand By is greyed.
    DialogSpec spec{&msgina(), DIALOG_TURN_OFF, BITMAP_BACKGROUND, BITMAP_FLAG, BITMAP_ICONS, CONTROL_FLAG, CONTROL_TITLE,
                    STRING_TITLE_FACE, STRING_TITLE_SIZE, STRING_LABEL_FACE, STRING_LABEL_SIZE, {}, STRIP_DISABLED};
    const bool standBy = canStandBy || canHibernate;
    spec.buttons = {
        {CONTROL_STANDBY, LABEL_STANDBY, STRIP_STANDBY, canStandBy && canHibernate ? TIP_STANDBY_SHIFT : canStandBy ? TIP_STANDBY : TIP_HIBERNATE,
         canStandBy ? StandBy : Hibernate, standBy},
        {CONTROL_OFF, LABEL_OFF, STRIP_OFF, TIP_OFF, TurnOff},
        {CONTROL_RESTART, LABEL_RESTART, STRIP_RESTART, TIP_RESTART, Restart},
    };
    spec.focus = standBy ? (canStandBy ? StandBy : Hibernate) : TurnOff;
    spec.shiftHibernates = canStandBy && canHibernate;
    ButtonsDialog dialog(spec);
    dialog.exec();
    return dialog.choice();
}

QWidget *showPleaseWait(const QString &text)
{
    if (!msgina())
        return nullptr;
    const QImage picture = xpfiles::bitmap(*msgina(), BITMAP_BACKGROUND);
    if (picture.isNull())
        return nullptr;
    QPixmap shown(picture.size());
    {
        QPainter painter(&shown);
        painter.drawImage(0, 0, picture);
        painter.setPen(Qt::black);
        painter.drawRect(0, 0, shown.width() - 1, shown.height() - 1);
        painter.setFont(xpfiles::themeFont(QStringLiteral("Franklin Gothic Medium, %1").arg(msginaString(STRING_TITLE_SIZE, "14")), 96));
        painter.setPen(Qt::white);
        painter.drawText(shown.rect(), Qt::AlignCenter | Qt::TextSingleLine, text);
    }
    auto *label = new QLabel(nullptr, Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
    label->setPixmap(shown);
    label->setFixedSize(shown.size());
    const QRect screen = QGuiApplication::primaryScreen()->geometry();
    // Where the Turn Off dialog stands: one third down
    label->move((screen.width() - shown.width()) / 2, (screen.height() - shown.height()) / 3);
    label->show();
    return label;
}

Choice askLogOff(bool canSwitch)
{
    // With somewhere to switch to, XP's Welcome screen and Fast User Switching: shell32's Log Off
    // Windows with Switch User (LogoffWindowsDialog). Otherwise the plain question, msgina's 2250.
    if (canSwitch && shell32() && !xpfiles::bitmap(*shell32(), BITMAP_SWITCH_BACKGROUND).isNull()) {
        DialogSpec spec{&shell32(), DIALOG_SWITCH, BITMAP_SWITCH_BACKGROUND, BITMAP_SWITCH_FLAG, BITMAP_SWITCH_ICONS, CONTROL_SWITCH_FLAG,
                        CONTROL_SWITCH_TITLE, STRING_SWITCH_TITLE_FACE, STRING_SWITCH_TITLE_SIZE, STRING_SWITCH_LABEL_FACE,
                        STRING_SWITCH_LABEL_SIZE, {}};
        spec.buttons = {
            {CONTROL_SWITCH_USER, LABEL_SWITCH_USER, STRIP_SWITCH_USER, TIP_SWITCH_USER, SwitchUser},
            {CONTROL_LOG_OFF, LABEL_LOG_OFF, STRIP_LOG_OFF, TIP_LOG_OFF, LogOff},
        };
        spec.focus = SwitchUser;
        ButtonsDialog dialog(spec);
        dialog.exec();
        return dialog.choice();
    }
    QDialog dialog;
    if (!msgina())
        return LogOff;
    const xpdialog::Page page = xpdialog::build(*msgina(), DIALOG_LOG_OFF, &dialog);
    dialog.setWindowTitle(page.caption);
    if (auto *yes = qobject_cast<QPushButton *>(page.controls.value(1))) {
        yes->setDefault(true);
        QObject::connect(yes, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *no = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(no, &QPushButton::clicked, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted ? LogOff : Cancelled;
}

} // namespace sessiondialog
