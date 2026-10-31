// SPDX-License-Identifier: LGPL-2.1-or-later
#include "captionanimation.h"

#include "theme.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <QWidget>

namespace captionanimation {
namespace {

// XP's CMS_ANIMATION, and a step every 10 milliseconds
const int DURATION_MS = 250, STEP_MS = 10;
const int ICON = 16;

// The strip: drawn once at its widest, as DrawCaptionTemp draws an active caption (never the visual
// style's art): the caption's gradient, the small icon 2 pixels in, the title 2 after it. Each frame shows
// its left part.
class Strip : public QWidget
{
public:
    Strip(const QRect &from, const QRect &to, const QImage &picture)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint
                  | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
        , m_from(from)
        , m_to(to)
        , m_picture(picture)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_OpaquePaintEvent);
        connect(&m_timer, &QTimer::timeout, this, &Strip::step);
        m_timer.start(STEP_MS);
        m_clock.start();
        step();
        show();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_picture);
    }

private:
    void step()
    {
        const qint64 elapsed = qMin<qint64>(m_clock.elapsed(), DURATION_MS);
        const auto at = [&](int a, int b) { return a + int((b - a) * elapsed / DURATION_MS); };
        const int width = at(m_from.width(), m_to.width());
        if (width > 0)
            setGeometry(at(m_from.x(), m_to.x()), at(m_from.y(), m_to.y()), width, m_picture.height());
        if (elapsed >= DURATION_MS) {
            m_timer.stop();
            close();
        }
    }

    QRect m_from, m_to;
    QImage m_picture;
    QTimer m_timer;
    QElapsedTimer m_clock;
};

} // namespace

void run(const QRect &from, const QRect &to, const QString &title, const QImage &icon)
{
    if (qEnvironmentVariableIsSet("WIN4WINE_NO_ANIMATION") || from.isEmpty() || to.isEmpty())
        return;
    ShellTheme *theme = ShellTheme::instance();
    const FrameStyle &style = theme->frame();
    // One pixel shorter than SM_CYCAPTION; a rectangle taller than that is a window, whose borders XP
    // leaves out (GetWindowBorders: its frame's)
    const int height = style.top - style.border - 1;
    const auto inside = [&](QRect rect) {
        if (rect.height() > height + 1)
            rect.adjust(style.border, style.border, -style.border, 0);
        return rect;
    };
    const QRect start = inside(from), end = inside(to);
    const int widest = qMax(start.width(), end.width());
    if (widest <= 0 || height <= 0)
        return;
    QImage picture(widest, height, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&picture);
        QLinearGradient gradient(0, 0, widest, 0);
        gradient.setColorAt(0, theme->system(QStringLiteral("activecaption"), style.activeCaption));
        gradient.setColorAt(1, theme->system(QStringLiteral("gradientactivecaption"), style.activeGradient));
        painter.fillRect(picture.rect(), gradient);
        const int iconSide = theme->scaled(ICON);
        int x = 2;
        if (!icon.isNull()) {
            painter.drawImage(QRect(x, (height - iconSide) / 2, iconSide, iconSide), icon);
            x += iconSide;
        }
        x += 2;
        painter.setFont(style.captionFont);
        painter.setPen(theme->system(QStringLiteral("captiontext"), style.activeText));
        painter.drawText(QRect(x, 0, widest - x, height), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
    }
    new Strip(start, end, picture);
}

} // namespace captionanimation
