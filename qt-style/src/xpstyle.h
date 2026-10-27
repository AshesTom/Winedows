// SPDX-License-Identifier: LGPL-2.1-or-later
// A Qt style that draws Qt's widgets with the XP shell's current visual
// style (session.h), as XP's common controls draw theirs with uxtheme.
// Under Windows Classic, or for anything the style has no art for, it
// draws as Qt's own "Windows" style, in the session's system colours.
#pragma once

#include "msstyles.h"
#include "session.h"

#include <QPointer>
#include <QProxyStyle>
#include <QStyleOption>
#include <QList>
#include <QTimer>
#include <memory>

class QProgressBar;

namespace w4w {

class XpStyle : public QProxyStyle
{
    Q_OBJECT
public:
    XpStyle();

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sc,
                         const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    QPalette standardPalette() const override;

    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    void polish(QApplication *application) override;
    void polish(QPalette &palette) override;

private:
    void reload();
    void applyToApplication();
    bool themed() const { return m_renderer != nullptr; }
    bool draw(QPainter *painter, const QRect &rect, const QString &cls, const QString &part,
              const QString &state, int stateId) const;
    QColor system(const QString &name, const QColor &fallback) const;
    QColor partColour(const QString &cls, const QString &part, const QString &state, const QString &property,
                      const QColor &fallback) const;
    int scrollBarExtent() const;
    void drawScrollBar(const QStyleOptionSlider *option, QPainter *painter, const QWidget *widget) const;
    void drawComboBox(const QStyleOptionComboBox *option, QPainter *painter, const QWidget *widget) const;
    void drawSpinBox(const QStyleOptionSpinBox *option, QPainter *painter, const QWidget *widget) const;
    void drawSlider(const QStyleOptionSlider *option, QPainter *painter, const QWidget *widget) const;
    void drawTab(const QStyleOptionTab *option, QPainter *painter, const QWidget *widget) const;
    void drawProgress(const QStyleOptionProgressBar *option, QPainter *painter, const QWidget *widget) const;

    Look m_look;
    std::shared_ptr<Renderer> m_renderer;
    QPalette m_palette;
    QTimer m_busyTimer;
    QList<QPointer<QProgressBar>> m_busyBars;
};

} // namespace w4w
