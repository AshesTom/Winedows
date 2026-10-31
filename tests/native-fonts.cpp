// SPDX-License-Identifier: LGPL-2.1-or-later
// c++ -std=c++17 -fPIC -Iqt-style/src -Inative-shell/src tests/native-fonts.cpp
// native-shell/src/xpfiles.cpp .local/qt-style-build/libwin4wine-xp-core.a
// $(pkg-config --cflags --libs Qt6Widgets) -o /tmp/native-fonts
// QT_QPA_PLATFORM=offscreen /tmp/native-fonts .local/native-assets
#include "fontpolicy.h"
#include "session.h"
#include "xpstyle.h"
#include "xpfiles.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdio>

static void write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        qFatal("cannot write fixture");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    qputenv("WIN4WINE_ASSETS", QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath().toUtf8());
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("WIN4WINE_NATIVE_SHELL", "1");
    qunsetenv("WIN4WINE_QT_MSSTYLES");
    const QFont early = w4w::themeFont("Franklin Gothic, 14", 96);
    QApplication app(argc, argv);
    Q_UNUSED(early);
    xpfiles::loadFonts();
    int checks = 0;
    const auto expect = [&](bool condition, const char *message) {
        if (!condition) qFatal("%s", message);
        ++checks;
    };
    for (const QString &spec : {QString("Tahoma, 8"), QString("Tahoma, 8, bold"),
                               QString("Trebuchet MS, 10, bold"), QString("Franklin Gothic Medium, 14, italic"),
                               QString("Arial, 8, italic"), QString("Franklin Gothic Medium, 21"), QString("Tahoma, 24")}) {
        const auto font = w4w::themeFont(spec, 96);
        expect(!font.kerning(), "GDI labels must not kern");
        expect(font.hintingPreference() == QFont::PreferFullHinting, "full hinting at every size");
        expect(font.styleStrategy() == QFont::NoAntialias, "all sizes must disable antialiasing");
        QImage image(350,40,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter p(&image); p.setPen(Qt::black); p.setFont(font); p.drawText(0,25,QString::fromUtf8("Affichage AV fi éçà 012345")); p.end();
        bool ink = false, grey = false;
        for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const QRgb pixel=image.pixel(x,y);
            if(qRed(pixel)!=qGreen(pixel)||qRed(pixel)!=qBlue(pixel)) qFatal("subpixel colour fringes");
            if(font.styleStrategy() & QFont::NoAntialias)
                if(qRed(pixel)!=0&&qRed(pixel)!=255) qFatal("smoothed monochrome glyph");
            ink |= qRed(pixel) < 255;
            grey |= qRed(pixel)>0 && qRed(pixel)<255;
        }
        expect(ink,"glyphs must be visible");
        expect(!grey,"glyph edges must remain monochrome at every size");
    }
    expect(w4w::themeFont("Tahoma, 8",96).styleStrategy() & QFont::NoAntialias,"Tahoma 11px must stay monochrome");
    expect(w4w::themeFont("Tahoma, 14",96).styleStrategy() & QFont::NoAntialias,"Tahoma 19px must stay monochrome");
    expect(QFontInfo(w4w::themeFont("Franklin Gothic, 14",96)).family()=="Franklin Gothic Medium","XP family alias");
    expect(QFontInfo(w4w::themeFont("Arial, 8, italic",96)).family()=="Arial","load the extracted dialog font");
    QDir().mkpath(config.path()+"/win4wine");
    const QString style=xpfiles::find("luna.msstyles");
    expect(!style.isEmpty(),"XP theme fixture missing");
    for(const auto &size : {qMakePair("NormalSize",11),qMakePair("LargeFonts",13),qMakePair("ExtraLargeFonts",16)}) {
        write(config.path()+"/win4wine/native-look.conf",QString("style=%1\ncolour=NormalColor\nsize=%2\n").arg(style,QLatin1String(size.first)).toUtf8());
        const auto look=w4w::readLook(config.path());
        expect(look.messageFont.pixelSize()==size.second,"native message font must follow Appearance size");
        expect(look.menuFont.pixelSize()==size.second,"native menu font must follow Appearance size");
        expect(!look.messageFont.kerning(),"native widget labels must not kern");
    }
    QApplication::setStyle(new w4w::XpStyle);
    expect(app.font().pixelSize()==16,"Qt applications must receive the selected font size");
    expect(app.font().family()=="Tahoma","Qt applications must receive the XP dialog face");
    // Registry heights are pixels, even when the prefix and the Qt screen have different DPI.
    qunsetenv("WIN4WINE_NATIVE_SHELL");
    QByteArray lf(92,0); qToLittleEndian<qint32>(-20,lf.data()); qToLittleEndian<qint32>(700,lf.data()+16);
    const QString face="Tahoma";
    for(int i=0;i<face.size();++i) qToLittleEndian<quint16>(face[i].unicode(),lf.data()+28+2*i);
    QByteArray hex;
    for(char c:lf) { if(!hex.isEmpty())hex+=','; hex+=QByteArray(1,c).toHex(); }
    write(config.path()+"/user.reg","WINE REGISTRY Version 2\n\n[Control Panel\\\\Desktop\\\\WindowMetrics]\n\"AppliedDPI\"=dword:00000090\n\"MessageFont\"=hex:"+hex+"\n");
    const auto look=w4w::readLook(config.path());
    expect(look.messageFont.pixelSize()==20,"LOGFONT must not be rescaled against the host screen DPI");
    expect(look.messageFont.bold(),"LOGFONT weight");
    expect(look.messageFont.styleStrategy() & QFont::NoAntialias,"large LOGFONT must stay monochrome");
    std::printf("PASS: %d native font and raster checks\n",checks);
}
