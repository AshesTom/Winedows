// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sessiondialog.h"

#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>

namespace sessiondialog {
namespace {

// msgina.dll: the Turn Off dialog's template, background, flag and icons (32 x 32, three states each)
const int DIALOG_TURN_OFF = 20100, BITMAP_BACKGROUND = 20142, BITMAP_FLAG = 20143, BITMAP_ICONS = 20150, DIALOG_LOG_OFF = 2250;
const int CONTROL_FLAG = 20101, CONTROL_TITLE = 20102, CONTROL_OFF = 20103, CONTROL_STANDBY = 20104, CONTROL_RESTART = 20105;
const int LABEL_OFF = 20107, LABEL_STANDBY = 20108, LABEL_RESTART = 20109;
const int STRIP_OFF = 0, STRIP_STANDBY = 3, STRIP_RESTART = 6, STRIP_DISABLED = 9, ICON_SIZE = 32;
// msgina's strings: the title's size, the labels' face and size
const int STRING_TITLE_SIZE = 20126, STRING_LABEL_FACE = 20127, STRING_LABEL_SIZE = 20128;
// How long the screen takes to fade, and the affine fit per channel of XP's final state
const int FADE_MS = 1000, FADE_STEP_MS = 40;
const double DIM[3][4] = {{0.1435, 0.6198, 0.1571, -4.37}, {0.0952, 0.6789, 0.1426, -3.11}, {0.0722, 0.6450, 0.1985, -3.15}};

const std::optional<w4w::PeResources> &msgina()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("msgina.dll")));
    return resources;
}

QString msginaString(int id, const char *fallback)
{
    QString text = msgina() ? xpfiles::string(*msgina(), id) : QString();
    text.remove(QChar(0));
    return text.isEmpty() ? QLatin1String(fallback) : text;
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

class TurnOffDialog : public QDialog
{
public:
    explicit TurnOffDialog(bool canStandBy)
        : QDialog(nullptr, Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint)
        , m_canStandBy(canStandBy)
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        m_desktop = screen->grabWindow(0).toImage().convertToFormat(QImage::Format_RGB32);
        m_grey = dimmed(m_desktop);
        setGeometry(screen->geometry());
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent);

        // The places come from the template, the pictures from the file.
        m_template = new QWidget(this);
        m_template->hide();
        const xpdialog::Page page = xpdialog::build(*msgina(), DIALOG_TURN_OFF, m_template);
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
            return text.remove(QLatin1Char('&'));
        };
        m_flag = place(CONTROL_FLAG);
        m_title = place(CONTROL_TITLE);
        m_titleText = words(CONTROL_TITLE);
        m_buttons = {
            {place(CONTROL_STANDBY), place(LABEL_STANDBY), words(CONTROL_STANDBY), STRIP_STANDBY, StandBy},
            {place(CONTROL_OFF), place(LABEL_OFF), words(CONTROL_OFF), STRIP_OFF, TurnOff},
            {place(CONTROL_RESTART), place(LABEL_RESTART), words(CONTROL_RESTART), STRIP_RESTART, Restart},
        };
        m_background = xpfiles::bitmap(*msgina(), BITMAP_BACKGROUND);
        m_flagImage = xpfiles::bitmap(*msgina(), BITMAP_FLAG);
        QImage strip = xpfiles::bitmap(*msgina(), BITMAP_ICONS).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < strip.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(strip.scanLine(y));
            for (int x = 0; x < strip.width(); ++x)
                if ((row[x] & 0xffffff) == 0xff00ff)
                    row[x] = 0;
        }
        m_strip = strip;

        auto *cancel = new QPushButton(words(2), this);
        cancel->setGeometry(place(2));
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

        // ("Franklin Gothic": XP's font mapper finds the Medium, the one it has.)
        m_titleFont = xpfiles::themeFont(QStringLiteral("Franklin Gothic Medium, %1").arg(msginaString(STRING_TITLE_SIZE, "14")), 96);
        m_labelFont = QFont(msginaString(STRING_LABEL_FACE, "Tahoma"));
        m_labelFont.setPixelSize(qRound(msginaString(STRING_LABEL_SIZE, "8").toInt() * 96 / 72.0));
        m_labelFont.setBold(true);
        m_labelFont.setStyleStrategy(QFont::NoAntialias);
        m_labelFont.setHintingPreference(QFont::PreferFullHinting);

        m_fade = new QTimer(this);
        m_fade->setInterval(FADE_STEP_MS);
        connect(m_fade, &QTimer::timeout, this, [this] {
            m_opacity = qMin(1.0, m_opacity + double(FADE_STEP_MS) / FADE_MS);
            if (m_opacity >= 1.0)
                m_fade->stop();
            update();
        });
        m_fade->start();
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
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
            reject();
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
            const bool enabled = button.choice != StandBy || m_canStandBy;
            // Normal, pressed and lit; the one with the focus (Turn Off) is lit too.
            const int state = !enabled ? -1 : i == m_pressed ? 1 : (i == m_hot || (m_hot < 0 && button.choice == TurnOff)) ? 2 : 0;
            const int index = state < 0 ? STRIP_DISABLED : button.strip + state;
            painter.drawImage(button.icon, m_strip, QRect(0, index * ICON_SIZE, ICON_SIZE, ICON_SIZE));
            painter.setPen(enabled ? QColor(Qt::white) : QColor(160, 160, 160));
            painter.drawText(button.label, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, button.text);
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hot = buttonAt(event->position().toPoint());
        if (hot != m_hot) {
            m_hot = hot;
            update();
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = buttonAt(event->position().toPoint());
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int at = buttonAt(event->position().toPoint());
        const bool same = at >= 0 && at == m_pressed;
        m_pressed = -1;
        update();
        if (same) {
            m_choice = m_buttons[at].choice;
            accept();
        }
    }

private:
    struct Button { QRect icon, label; QString text; int strip; Choice choice; };

    int buttonAt(QPoint at) const
    {
        for (int i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].icon.contains(at) && (m_buttons[i].choice != StandBy || m_canStandBy))
                return i;
        return -1;
    }

    bool m_canStandBy;
    Choice m_choice = Cancelled;
    QImage m_desktop, m_grey, m_background, m_flagImage, m_strip;
    QWidget *m_template = nullptr;
    QRect m_client, m_flag, m_title;
    QString m_titleText;
    QList<Button> m_buttons;
    QFont m_titleFont, m_labelFont;
    QTimer *m_fade = nullptr;
    double m_opacity = 0;
    int m_hot = -1, m_pressed = -1;
};

} // namespace

Choice askTurnOff(bool canStandBy)
{
    if (!msgina())
        return Cancelled;
    TurnOffDialog dialog(canStandBy);
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

bool askLogOff()
{
    QDialog dialog;
    if (!msgina())
        return true;
    const xpdialog::Page page = xpdialog::build(*msgina(), DIALOG_LOG_OFF, &dialog);
    dialog.setWindowTitle(page.caption);
    if (auto *yes = qobject_cast<QPushButton *>(page.controls.value(1))) {
        yes->setDefault(true);
        QObject::connect(yes, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *no = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(no, &QPushButton::clicked, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace sessiondialog
