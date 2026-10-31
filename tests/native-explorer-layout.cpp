// SPDX-License-Identifier: LGPL-2.1-or-later
// Link with native-shell's objects except main.cpp (see native-explorer-layout.py).
#include "filebrowser.h"
#include "frames.h"
#include "theme.h"
#include "xpfiles.h"
#include "x11.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QComboBox>
#include <QFile>
#include <QHeaderView>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QStyleFactory>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleOption>
#include <QRubberBand>
#include <QTest>
#include <QTreeView>
#include <QTreeWidget>
#include <cstdio>

static int checks = 0;
static bool verify = false;
static void check(bool ok, const char *message) {
    if (verify && !ok) qFatal("%s", message);
    if (ok) ++checks;
}
static QAction *action(FileBrowser &w, const QString &name) {
    for (auto *a : w.findChildren<QAction *>()) {
        QString text=a->text(); text.remove('&');
        if (text==name && a->isCheckable()) return a;
    }
    qFatal("view action missing: %s",qPrintable(name));
    return nullptr;
}
static QAbstractItemView *visibleView(FileBrowser &w) {
    for(auto *v:w.findChildren<QAbstractItemView *>())
        if(v->isVisible() && !qobject_cast<QTreeWidget *>(v) && (qobject_cast<QListView *>(v)||qobject_cast<QTreeView *>(v))) return v;
    qFatal("no visible folder view"); return nullptr;
}
static void capture(FileBrowser &w, const QString &file) {
    const auto style=ShellTheme::instance()->frame();
    const auto client=w.grab().toImage();
    QImage image(client.width()+2*style.border,client.height()+style.top+style.border,QImage::Format_RGB32);
    image.fill(style.face); QPainter p(&image);
    FrameLook look; look.title=w.windowTitle(); look.active=true;look.canMaximize=true;look.canMinimize=true;
    look.icon=w.windowIcon().pixmap(16,16).toImage();
    drawFrame(p,image.rect(),style,look);p.drawImage(style.border,style.top,client);p.end();
    if(!image.save(file))qFatal("capture failed");
}
int main(int argc,char **argv) {
    if(argc<3)return 2;verify=argc>3;
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc,argv); xpfiles::loadFonts();
    if(auto *style=QStyleFactory::create("Win4WineXP"))app.setStyle(style);else qFatal("XP style missing");
    app.setFont(ShellTheme::instance()->systemFont("msgboxfont"));
    const QString out=QString::fromLocal8Bit(argv[1]), folder=QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out);QDir().mkpath(folder+"/Holiday Photos");
    for(const char *name:{"Alpha.txt","Beta.txt","A much longer file name for the list view.txt","budget.csv"}) {QFile f(folder+'/'+name);if(!f.open(QIODevice::WriteOnly))qFatal("fixture write failed");f.write("fixture\n");}
    FileBrowser w(folder);w.resize(640,480);w.show();w.activateWindow();QTest::qWait(700);
    for(const QString &mode:{QString("Tiles"),QString("Icons"),QString("List"),QString("Details"),QString("Thumbnails")}) {
        action(w,mode)->trigger();QTest::qWait(100);auto *v=visibleView(w);
        // The isolated X server has no window manager to restore focus after popups.
        xcb_set_input_focus(x11::connection(), XCB_INPUT_FOCUS_POINTER_ROOT, w.winId(), XCB_CURRENT_TIME);
        xcb_flush(x11::connection());
        QTest::qWait(50);v->setFocus();QTest::qWait(20);
        check(v->hasFocus() && w.isActiveWindow(), "capture the view with real X11 keyboard focus");
        auto first=v->model()->index(0,0,v->rootIndex()), second=v->model()->index(1,0,v->rootIndex());
        check(first.isValid()&&second.isValid(),"fixture items missing");
        v->selectionModel()->select(first,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);v->setCurrentIndex(first);QTest::qWait(30);
        auto r=v->visualRect(first), r2=v->visualRect(second);
        std::printf("%s first=%d,%d %dx%d next=%d,%d\n",qPrintable(mode),r.x(),r.y(),r.width(),r.height(),r2.x(),r2.y());
        if(mode=="List"||mode=="Details")check(r.height()==qMax(16,v->fontMetrics().height())+1,"XP compact rows are max(font, small icon)+1");
        if(mode=="Details") {auto *tree=qobject_cast<QTreeView *>(v);std::printf("header=%d\n",tree->header()->height());check(tree->header()->height()==v->fontMetrics().height()+4,"XP header height");}
        if(mode=="Icons") check(!v->indexAt(QPoint(r.right()-1,r.bottom()-1)).isValid(),"empty icon-cell margins must not select a file");
        if(mode=="Details") check(!v->indexAt(QPoint(r.right()-1,r.center().y())).isValid(),"empty report-label space must not select a file");
        if(mode=="List") {
            auto *list=qobject_cast<QListView *>(v);
            check(!list->indexAt(QPoint(r.right()-2,r.center().y())).isValid(),"blank space after a short List label is not an item hit");
            check(list->gridSize().width() >= v->fontMetrics().horizontalAdvance("A much longer file name for the list view.txt")+16,"List columns must fit long labels");
        }
        capture(w,out+'/'+mode+".png");
        if(mode=="Tiles") {
            auto *other=w.findChild<QComboBox *>(); if(other)other->setFocus();QTest::qWait(30);capture(w,out+"/Tiles-inactive.png");v->setFocus();
        }
        if(mode=="List") {
            // Right-click a different item: the context menu must act on that item.
            QMetaObject::invokeMethod(v,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,r2.center()));QTest::qWait(50);
            check(v->selectionModel()->isSelected(second),"right-click must select the item under the pointer");
            for(auto *menu:w.findChildren<QMenu *>())if(menu->isVisible()){
                check(menu->actionGeometry(menu->actions().first()).height()==menu->fontMetrics().height()+4,"XP context item height");
                bool folderOnly=false;for(auto *a:menu->actions())folderOnly |= a->text().contains("New &Window");
                check(!folderOnly,"a file context menu must not show folder-only actions");
                menu->grab().save(out+"/Context-menu.png");menu->close();
            }
            // A right-click within a multiple selection must retain it.
            v->selectionModel()->select(first,QItemSelectionModel::Select|QItemSelectionModel::Rows);
            QMetaObject::invokeMethod(v,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(r.x()+8,r.center().y())));
            check(v->selectionModel()->isSelected(first)&&v->selectionModel()->isSelected(second),"preserve multiple selection on right click");
            for(auto *menu:w.findChildren<QMenu *>())if(menu->isVisible())menu->close();
            QMetaObject::invokeMethod(v,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(v->viewport()->width()-2,v->viewport()->height()-2)));
            check(!v->selectionModel()->hasSelection(),"blank context click clears the item selection");
            for(auto *menu:w.findChildren<QMenu *>())if(menu->isVisible())menu->close();
        }
    }
    action(w,"List")->trigger();
    auto *list=visibleView(w); const auto selected=list->model()->index(1,0,list->rootIndex());
    list->selectionModel()->select(selected,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    action(w,"Details")->trigger();
    check(visibleView(w)->selectionModel()->isSelected(selected),"selection survives switching List to Details");
    w.navigate(places::Computer);QTest::qWait(100);
    for(const QString &mode:{QString("Tiles"),QString("Icons"),QString("List"),QString("Details")}) {
        action(w,mode)->trigger();QTest::qWait(50);auto *v=visibleView(w);
        if(mode=="Details") {
            check(qobject_cast<QTreeView *>(v)->columnWidth(0)==200,"My Computer retains its name column width across refills");
            check(qobject_cast<QTreeView *>(v),"My Computer Details must be a report view");
            check(v->model()->columnCount()>=4,"My Computer must show type, total size and free space");
        } else {
            auto *view=qobject_cast<QListView *>(v);
            check(view&&view->viewMode()==(mode=="List"?QListView::ListMode:QListView::IconMode),"My Computer must honor the selected view");
        }
        if(mode=="List"||mode=="Details")
            for(int row=0;row<v->model()->rowCount();++row)check(v->model()->index(row,0).flags()&Qt::ItemIsSelectable,"group headings must not masquerade as files in compact views");
        capture(w,out+"/My-Computer-"+mode+".png");
    }
    QSettings prefs(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/win4wine/native-explorer.conf",QSettings::IniFormat);
    prefs.setValue("classicPanel",true);prefs.sync();
    w.navigate(places::ControlPanel);QTest::qWait(50);
    check(visibleView(w)->model()->columnCount()==2,"Control Panel Details has Name and Comments, not disk capacities");
    capture(w,out+"/Control-Panel-Details.png");
    w.navigate(places::Computer);QTest::qWait(50);
    check(visibleView(w)->model()->columnCount()==4,"My Computer restores its capacity columns after Control Panel");
    QImage marquee(30,30,QImage::Format_RGB32);marquee.fill(Qt::white);QPainter mp(&marquee);
    QStyleOptionRubberBand band;band.rect=marquee.rect();band.shape=QRubberBand::Rectangle;band.palette=app.palette();
    app.style()->drawControl(QStyle::CE_RubberBand,&band,&mp);mp.end();
    const QColor fill=ShellTheme::instance()->system("menuhilight",app.palette().color(QPalette::Highlight));
    const QColor pixel=marquee.pixelColor(15,15);
    check(qAbs(pixel.red()-(fill.red()*70+255*185+127)/255)<=1 && qAbs(pixel.green()-(fill.green()*70+255*185+127)/255)<=1 && qAbs(pixel.blue()-(fill.blue()*70+255*185+127)/255)<=1,"XP marquee alpha must be 70/255");
    check(marquee.pixelColor(0,0)==app.palette().color(QPalette::Highlight),"XP marquee outline colour");
    marquee.save(out+"/Selection-marquee.png");
    const auto frame=ShellTheme::instance()->frame();
    QImage dialog(340,70,QImage::Format_RGB32);dialog.fill(Qt::black);QPainter p(&dialog);
    FrameLook look;look.title=QString(60,QLatin1Char('M'));look.active=true;look.canMinimize=false;look.canMaximize=false;
    drawFrame(p,dialog.rect(),frame,look);p.end();
    const int left=frameButtonRect(FrameButton::Minimize,dialog.rect(),frame,false).left();
    const int right=frameButtonRect(FrameButton::Close,dialog.rect(),frame,false).left()-5;
    bool title=false;for(int y=8;y<23;++y)for(int x=left;x<right;++x)title |= dialog.pixelColor(x,y)==frame.activeText;
    check(title,"dialog title must use the space freed by missing min/max buttons");dialog.save(out+"/Dialog-caption.png");
    std::printf("PASS: %d Explorer checks%s\n",checks,verify?"":" (baseline; assertions disabled)");
}
