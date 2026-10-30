// SPDX-License-Identifier: LGPL-2.1-or-later
// The shell's current look: a visual style through w4w::Renderer, or Windows
// Classic (no renderer: its parts are drawn from the system colours). Display
// Properties writes native-look.conf; everything follows the changed() signal.
#pragma once

#include "msstyles.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QObject>
#include <memory>

// What a window's frame is drawn from: the shell's look, or one that Display Properties previews.
struct FrameStyle
{
    std::shared_ptr<w4w::Renderer> renderer;      // null for Windows Classic
    QFont captionFont;
    QColor activeCaption, activeGradient, inactiveCaption, inactiveGradient;
    QColor activeText, inactiveText, face, highlight, shadow, darkShadow, light;
    int border = 4;
    int top = 30;                                 // the frame's height over its client
};

struct LookChoice
{
    QString style;                                // the .msstyles; empty for Windows Classic
    QString colour = QStringLiteral("NormalColor"), size = QStringLiteral("NormalSize");
    QString wallpaper;
    int wallpaperStyle = 2;
    bool tile = false;
    QColor background = QColor(0, 78, 152);
};

class ShellTheme : public QObject
{
    Q_OBJECT
public:
    static ShellTheme *instance();
    w4w::Renderer *renderer() const { return m_frame.renderer.get(); }
    bool classic() const { return !m_frame.renderer; }
    int dpi() const { return m_dpi; }
    int scaled(int pixels) const { return pixels * m_dpi / 96; }
    const FrameStyle &frame() const { return m_frame; }
    QColor system(const QString &name, const QColor &fallback) const;
    QFont font(const QString &spec) const;
    LookChoice choice() const;
    QImage defaultIcon(int size) const;
    // An icon of shell32.dll's, by its resource ID; null if there is none.
    QImage shellIcon(int id, int size) const;

    // The style of a choice, as a frame style (Classic when it cannot be read).
    static FrameStyle frameStyleFor(const LookChoice &choice, int dpi);
    // Writes native-look.conf; changed() follows when the watcher sees it.
    static void write(const LookChoice &choice);
    // The first start of a session without a prefix: XP's own default look.
    static void ensureDefaultLook();
    static QString lunaPath();
    // uxtheme's continuous shadow under a text: its coverage, softened by a pixel, at OFFSET.
    static void drawShadowedText(QPainter &painter, const QRect &rect, int flags, const QString &text, const QColor &colour, const QColor &shadow, QPoint offset);
    // The file of the Common Tasks pane's art for a look: the style's own Shell folder, or
    // XP's for its colour. Empty for Windows Classic.
    static QString shellStylePath(const LookChoice &look);

    // 3D edges of Windows Classic
    void raised(QPainter &painter, const QRect &rect, bool pressed = false) const;
    void sunken(QPainter &painter, const QRect &rect) const;

Q_SIGNALS:
    void changed();

private:
    ShellTheme();
    void reload();

    FrameStyle m_frame;
    QHash<QString, QColor> m_system;
    int m_dpi = 96;
};
