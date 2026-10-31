// SPDX-License-Identifier: LGPL-2.1-or-later
// Build after qt-style: c++ -std=c++17 -fPIC -Iqt-style/src tests/qt-style-msstyles.cpp
// .local/qt-style-build/libwin4wine-xp-core.a $(pkg-config --cflags --libs Qt6Widgets) -o /tmp/qt-style-msstyles
// QT_QPA_PLATFORM=offscreen /tmp/qt-style-msstyles PATH.msstyles NormalColor
#include "xpstyle.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>

static void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        qFatal("cannot write fixture");
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString variant = QString::fromLocal8Bit(argv[2]);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
    qputenv("XDG_RUNTIME_DIR", temp.path().toUtf8());
    qputenv("WIN4WINE_PREFIX", temp.path().toUtf8());
    qputenv("WIN4WINE_NATIVE_SHELL", "1");
    qunsetenv("WIN4WINE_QT_MSSTYLES");
    qunsetenv("WIN4WINE_QT_STYLE_CLASSIC");
    QDir().mkpath(temp.path() + "/win4wine");
    writeFile(temp.path() + "/win4wine/native-look.conf",
              QString("style=%1\ncolour=%2\nsize=NormalSize\n").arg(path, variant).toUtf8());
    // A stale prefix palette must not replace the native theme's palette.
    writeFile(temp.path() + "/user.reg", "WINE REGISTRY Version 2\n\n[Control Panel\\\\Colors]\n\"ButtonFace\"=\"255 0 255\"\n");
    QApplication app(argc, argv);
    QString error;
    auto theme = w4w::Theme::load(path, variant, "NormalSize", &error);
    if (!theme)
        qFatal("%s", qPrintable(error));
    w4w::XpStyle style;
    int checks = 0;
    QColor face;
    if (!w4w::parseColour(theme->sysMetric("btnface").value_or(QString()), &face)
        || style.standardPalette().color(QPalette::Button) != face)
        qFatal("native palette differs from SysMetrics.Btnface");
    ++checks;
    qunsetenv("WIN4WINE_NATIVE_SHELL");
    if (w4w::readLook(temp.path()).colours.value("buttonface") != QColor(Qt::magenta))
        qFatal("prefix palette was not preserved outside the native session");
    qputenv("WIN4WINE_NATIVE_SHELL", "1");
    ++checks;
    const QList<QPair<QString,QPalette::ColorRole>> metrics{
        {"btnface",QPalette::Window},{"window",QPalette::Base},{"btnhighlight",QPalette::Light},
        {"light3d",QPalette::Midlight},{"btnshadow",QPalette::Mid},{"btnshadow",QPalette::Dark},
        {"dkshadow3d",QPalette::Shadow},{"highlight",QPalette::Highlight},{"highlighttext",QPalette::HighlightedText}};
    for (const auto &[metric,role] : metrics) {
        QColor expected;
        if (!w4w::parseColour(theme->sysMetric(metric).value_or(QString()),&expected)
            || style.standardPalette().color(QPalette::Active,role)!=expected)
            qFatal("palette role differs from SysMetrics.%s",qPrintable(metric));
        ++checks;
    }
    auto verify = [&](const QString &part, const QString &state, int id,
                      QStyle::PrimitiveElement primitive, QStyle::State flags, bool defaulted = false) {
        const w4w::Part p{"button", part, state, id, {}};
        const auto file = theme->property(p, "imagefile").has_value()
            ? theme->property(p, "imagefile") : theme->property(p, "imagefile1");
        if (!file)
            qFatal("missing imagefile");
        bool alpha = false;
        QImage strip = theme->bitmap(*file, &alpha);
        const int count = theme->property(p, "imagecount").value_or("1").toInt();
        const bool horizontal = theme->property(p, "imagelayout").value_or(QString()).compare("horizontal", Qt::CaseInsensitive) == 0;
        const QSize size(horizontal ? strip.width()/count : strip.width(), horizontal ? strip.height() : strip.height()/count);
        QImage art = strip.copy(QRect(horizontal ? QPoint((id-1)*size.width(),0) : QPoint(0,(id-1)*size.height()),size));
        QColor key(255,0,255);
        w4w::parseColour(theme->property(p,"transparentcolor").value_or(QString()), &key);
        if (!alpha && w4w::parseBool(theme->property(p,"transparent").value_or(QString())))
            for(int y=0;y<art.height();++y) for(int x=0;x<art.width();++x)
                if((art.pixel(x,y)&0xffffff)==(key.rgb()&0xffffff)) art.setPixel(x,y,0);
        art = art.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage actual(size,QImage::Format_ARGB32_Premultiplied); actual.fill(Qt::transparent);
        QStyleOptionButton o; o.rect=actual.rect(); o.state=flags; o.palette=style.standardPalette();
        if(defaulted) o.features=QStyleOptionButton::DefaultButton;
        QPainter painter(&actual);
        style.drawPrimitive(primitive,&o,&painter); painter.end();
        if(actual!=art) qFatal("%s/%s differs from msstyles bitmap",qPrintable(part),qPrintable(state));
        ++checks;
        // Labels must take TextColor from the same state, including disabled ones.
        const auto colour = theme->property(p,"textcolor");
        QColor text;
        if (colour && w4w::parseColour(*colour,&text)) {
            QImage label(180,35,QImage::Format_ARGB32_Premultiplied); label.fill(Qt::transparent);
            o.rect=label.rect(); o.text="Theme label";
            o.palette.setColor(QPalette::ButtonText,Qt::magenta);
            o.palette.setColor(QPalette::WindowText,Qt::magenta);
            QPainter lp(&label); QFont font("sans-serif"); font.setPixelSize(13); font.setStyleStrategy(QFont::NoAntialias); lp.setFont(font);
            style.drawControl(part=="pushbutton" ? QStyle::CE_PushButtonLabel : part=="checkbox" ? QStyle::CE_CheckBoxLabel : QStyle::CE_RadioButtonLabel,&o,&lp); lp.end();
            bool found=false;
            for(int y=0;y<label.height();++y) for(int x=0;x<label.width();++x)
                found |= label.pixelColor(x,y)==text;
            if(!found) qFatal("%s/%s ignores TextColor",qPrintable(part),qPrintable(state));
            ++checks;
        }
    };
    const QStringList states{"normal","hot","pressed","disabled"};
    const QList<QStyle::State> flags{QStyle::State_Enabled,QStyle::State_Enabled|QStyle::State_MouseOver,
        QStyle::State_Enabled|QStyle::State_Sunken,{}};
    for(int i=0;i<4;++i) verify("pushbutton",states[i],i+1,QStyle::PE_PanelButtonCommand,flags[i]);
    verify("pushbutton","defaulted",5,QStyle::PE_PanelButtonCommand,QStyle::State_Enabled,true);
    for(bool radio : {false,true}) {
        const QStringList values = radio ? QStringList{"unchecked","checked"} : QStringList{"unchecked","checked","mixed"};
        for(int v=0;v<values.size();++v) for(int i=0;i<4;++i)
            verify(radio?"radiobutton":"checkbox",values[v]+states[i],4*v+i+1,
                   radio?QStyle::PE_IndicatorRadioButton:QStyle::PE_IndicatorCheckBox,
                   flags[i] | (v==0?QStyle::State_Off:v==1?QStyle::State_On:QStyle::State_NoChange));
    }
    std::printf("PASS: %s: %d bitmap, label and palette comparisons\n",qPrintable(variant),checks);
}
