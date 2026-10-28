// SPDX-License-Identifier: LGPL-2.1-or-later
// The style's widgets side by side, to check it against the shell's look:
//   xpstyle-gallery              a window
//   xpstyle-gallery --out F.png  a capture of it (QT_QPA_PLATFORM=offscreen works)
#include "../src/xpstyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QStyleOption>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

// Every state of the main parts, drawn straight with the style:
//   xpstyle-gallery --states F.png
static QImage statesSheet(QStyle *style)
{
    QImage sheet(560, 330, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QApplication::palette().color(QPalette::Window));
    QPainter p(&sheet);
    const QStyle::State on = QStyle::State_Enabled;
    const QList<QPair<QString, QStyle::State>> four = {
        {QStringLiteral("normal"), on}, {QStringLiteral("hot"), on | QStyle::State_MouseOver},
        {QStringLiteral("pressed"), on | QStyle::State_Sunken | QStyle::State_MouseOver}, {QStringLiteral("disabled"), {}}};
    int y = 8;
    auto label = [&](const QString &text) {
        p.setPen(Qt::black);
        p.drawText(8, y + 14, text);
    };
    // Push buttons
    label(QStringLiteral("Buttons"));
    int x = 90;
    for (const auto &[name, state] : four) {
        QStyleOptionButton o;
        o.rect = QRect(x, y, 80, 23);
        o.state = state;
        o.text = name;
        o.palette = QApplication::palette();
        style->drawControl(QStyle::CE_PushButton, &o, &p);
        x += 88;
    }
    QStyleOptionButton def;
    def.rect = QRect(x, y, 80, 23);
    def.state = on;
    def.features = QStyleOptionButton::DefaultButton;
    def.text = QStringLiteral("default");
    def.palette = QApplication::palette();
    style->drawControl(QStyle::CE_PushButton, &def, &p);
    y += 34;
    // Check boxes and radio buttons
    for (bool radio : {false, true}) {
        label(radio ? QStringLiteral("Radios") : QStringLiteral("Checks"));
        x = 90;
        const QList<QStyle::State> values = radio ? QList<QStyle::State>{QStyle::State_Off, QStyle::State_On}
                                                  : QList<QStyle::State>{QStyle::State_Off, QStyle::State_On, QStyle::State_NoChange};
        for (QStyle::State value : values)
            for (const auto &[name, state] : four) {
                QStyleOptionButton o;
                o.rect = QRect(x, y + 4, 13, 13);
                o.state = state | value;
                style->drawPrimitive(radio ? QStyle::PE_IndicatorRadioButton : QStyle::PE_IndicatorCheckBox, &o, &p);
                x += 22;
            }
        y += 28;
    }
    // Scroll bars: hot, pressed and disabled arrows and thumbs
    label(QStringLiteral("Scroll"));
    x = 90;
    for (const auto &[name, state] : four) {
        QStyleOptionSlider o;
        o.rect = QRect(0, 0, 100, 17);
        o.orientation = Qt::Horizontal;
        o.state = state | QStyle::State_Horizontal;
        o.minimum = 0;
        o.maximum = state & QStyle::State_Enabled ? 100 : 0;
        o.pageStep = 30;
        o.sliderPosition = o.sliderValue = 40;
        o.subControls = QStyle::SC_All;
        o.activeSubControls = name == QLatin1String("normal") ? QStyle::SC_None : QStyle::SC_ScrollBarAddLine;
        p.save();
        p.translate(x, y);   // (Qt's scroll bar parts start at the bar's own 0,0)
        style->drawComplexControl(QStyle::CC_ScrollBar, &o, &p);
        p.restore();
        x += 110;
    }
    y += 28;
    // Combo boxes and spin boxes
    label(QStringLiteral("Combo/spin"));
    x = 90;
    for (const auto &[name, state] : four) {
        QStyleOptionComboBox o;
        o.rect = QRect(x, y, 80, 21);
        o.state = state;
        o.subControls = QStyle::SC_All;
        o.activeSubControls = QStyle::SC_ComboBoxArrow;
        o.palette = QApplication::palette();
        style->drawComplexControl(QStyle::CC_ComboBox, &o, &p);
        x += 88;
    }
    QStyleOptionSpinBox spin;
    spin.rect = QRect(x, y, 60, 21);
    spin.state = on | QStyle::State_Sunken | QStyle::State_MouseOver;
    spin.subControls = QStyle::SC_All;
    spin.activeSubControls = QStyle::SC_SpinBoxUp;
    spin.stepEnabled = QAbstractSpinBox::StepUpEnabled;
    spin.frame = true;
    spin.palette = QApplication::palette();
    style->drawComplexControl(QStyle::CC_SpinBox, &spin, &p);
    y += 32;
    // Tabs
    label(QStringLiteral("Tabs"));
    x = 90;
    for (const auto &[name, state] : QList<QPair<QString, QStyle::State>>{
             {QStringLiteral("normal"), on}, {QStringLiteral("hot"), on | QStyle::State_MouseOver},
             {QStringLiteral("selected"), on | QStyle::State_Selected}, {QStringLiteral("disabled"), {}}}) {
        QStyleOptionTab o;
        o.rect = QRect(x, y, 70, 21);
        o.state = state;
        o.text = name;
        o.shape = QTabBar::RoundedNorth;
        o.position = QStyleOptionTab::Middle;
        o.palette = QApplication::palette();
        style->drawControl(QStyle::CE_TabBarTab, &o, &p);
        x += 78;
    }
    y += 32;
    // Header sections and toolbar buttons
    label(QStringLiteral("Header/tool"));
    x = 90;
    for (int i = 0; i < 3; ++i) {
        QStyleOptionHeader o;
        o.rect = QRect(x, y, 70, 20);
        o.state = four[i].second;
        o.text = four[i].first;
        o.palette = QApplication::palette();
        style->drawControl(QStyle::CE_Header, &o, &p);
        x += 72;
    }
    for (const auto &[name, state] : QList<QPair<QString, QStyle::State>>{
             {QStringLiteral("hot"), on | QStyle::State_MouseOver | QStyle::State_AutoRaise},
             {QStringLiteral("pressed"), on | QStyle::State_Sunken | QStyle::State_AutoRaise},
             {QStringLiteral("checked"), on | QStyle::State_On | QStyle::State_AutoRaise}}) {
        QStyleOption o;
        o.rect = QRect(x, y, 60, 22);
        o.state = state;
        o.palette = QApplication::palette();
        style->drawPrimitive(QStyle::PE_PanelButtonTool, &o, &p);
        p.drawText(o.rect, Qt::AlignCenter, name);
        x += 66;
    }
    y += 32;
    // Slider thumbs
    label(QStringLiteral("Slider"));
    x = 90;
    for (const auto &[name, state] : four) {
        QStyleOptionSlider o;
        o.rect = QRect(x, y, 100, 28);
        o.orientation = Qt::Horizontal;
        o.state = state | QStyle::State_Horizontal;
        o.minimum = 0;
        o.maximum = 10;
        o.sliderPosition = o.sliderValue = 5;
        o.tickPosition = QSlider::TicksBelow;
        o.tickInterval = 1;
        o.subControls = QStyle::SC_All;
        o.activeSubControls = QStyle::SC_SliderHandle;
        style->drawComplexControl(QStyle::CC_Slider, &o, &p);
        x += 110;
    }
    y += 40;
    // Progress
    label(QStringLiteral("Progress"));
    QStyleOptionProgressBar bar;
    bar.rect = QRect(90, y, 300, 18);
    bar.state = on | QStyle::State_Horizontal;
    bar.minimum = 0;
    bar.maximum = 100;
    bar.progress = 45;
    bar.palette = QApplication::palette();
    style->drawControl(QStyle::CE_ProgressBar, &bar, &p);
    p.end();
    return sheet;
}

int main(int argc, char **argv)
{
    QApplication::setStyle(new w4w::XpStyle);
    QApplication app(argc, argv);
    QString out;
    const QStringList args = app.arguments();
    if (const int i = args.indexOf(QStringLiteral("--states")); i >= 0 && i + 1 < args.size())
        return statesSheet(QApplication::style()).save(args[i + 1]) ? 0 : 1;
    if (const int i = args.indexOf(QStringLiteral("--menu")); i >= 0 && i + 1 < args.size()) {
        // A menu with its second item selected, as the mouse leaves it.
        // Pictures as KDE's programs give them (the Accurate style leaves them out).
        QMenu menu;
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("&Open"));
        QAction *selected = menu.addAction(QStringLiteral("Save &As..."));
        auto *ticked = menu.addAction(QStringLiteral("&Status Bar"));
        ticked->setCheckable(true);
        ticked->setChecked(true);
        menu.addSeparator();
        menu.addMenu(QStringLiteral("&Recent Files"))->addAction(QStringLiteral("a.txt"));
        menu.addAction(QStringLiteral("Disabled"))->setEnabled(false);
        menu.addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("E&xit"));
        menu.popup(QPoint(0, 0));
        menu.setActiveAction(selected);
        const QString file = args[i + 1];
        QTimer::singleShot(200, &app, [&] {
            menu.grab().save(file);
            app.quit();
        });
        return app.exec();
    }
    if (const int i = args.indexOf(QStringLiteral("--out")); i >= 0 && i + 1 < args.size())
        out = args[i + 1];

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Win4Wine-XP"));
    QMenu *file = window.menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("&Open..."));
    file->addSeparator();
    file->addAction(QStringLiteral("E&xit"));
    window.menuBar()->addMenu(QStringLiteral("&Edit"));
    window.menuBar()->addMenu(QStringLiteral("&Help"));
    QToolBar *tools = window.addToolBar(QStringLiteral("Tools"));
    tools->addAction(QStringLiteral("Back"));
    QAction *checked = tools->addAction(QStringLiteral("Folders"));
    checked->setCheckable(true);
    checked->setChecked(true);
    tools->addSeparator();
    tools->addAction(QStringLiteral("Views"));
    window.statusBar()->showMessage(QStringLiteral("Ready"));

    auto *tabs = new QTabWidget;
    auto *page = new QWidget;
    auto *columns = new QHBoxLayout(page);

    auto *left = new QVBoxLayout;
    auto *buttons = new QGroupBox(QStringLiteral("Buttons"));
    auto *b = new QVBoxLayout(buttons);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    ok->setDefault(true);
    b->addWidget(ok);
    ok->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok")));
    b->addWidget(new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-cancel")), QStringLiteral("Cancel")));
    auto *disabled = new QPushButton(QStringLiteral("Disabled"));
    disabled->setEnabled(false);
    b->addWidget(disabled);
    auto *check = new QCheckBox(QStringLiteral("Check box"));
    check->setChecked(true);
    b->addWidget(check);
    auto *tristate = new QCheckBox(QStringLiteral("Mixed"));
    tristate->setTristate(true);
    tristate->setCheckState(Qt::PartiallyChecked);
    b->addWidget(tristate);
    b->addWidget(new QCheckBox(QStringLiteral("Unchecked")));
    auto *radio = new QRadioButton(QStringLiteral("Radio one"));
    radio->setChecked(true);
    b->addWidget(radio);
    b->addWidget(new QRadioButton(QStringLiteral("Radio two")));
    left->addWidget(buttons);

    auto *fields = new QGroupBox(QStringLiteral("Fields"));
    auto *f = new QFormLayout(fields);
    f->addRow(QStringLiteral("Name:"), new QLineEdit(QStringLiteral("Windows XP")));
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("Luna"), QStringLiteral("Windows Classic")});
    f->addRow(QStringLiteral("Theme:"), combo);
    auto *editable = new QComboBox;
    editable->setEditable(true);
    editable->addItem(QStringLiteral("C:\\Documents and Settings"));
    f->addRow(QStringLiteral("Path:"), editable);
    auto *spin = new QSpinBox;
    spin->setValue(42);
    f->addRow(QStringLiteral("Count:"), spin);
    left->addWidget(fields);
    columns->addLayout(left);

    auto *right = new QVBoxLayout;
    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size")});
    auto *root = new QTreeWidgetItem(tree, {QStringLiteral("My Computer"), QString()});
    for (int i = 0; i < 12; ++i)
        new QTreeWidgetItem(root, {QStringLiteral("File %1.txt").arg(i), QStringLiteral("%1 KB").arg(i * 3)});
    root->setExpanded(true);
    new QTreeWidgetItem(tree, {QStringLiteral("My Network Places"), QString()});
    tree->setCurrentItem(root->child(1));
    right->addWidget(tree);
    auto *progress = new QProgressBar;
    progress->setValue(60);
    progress->setTextVisible(false);
    right->addWidget(progress);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setValue(30);
    right->addWidget(slider);
    auto *scroll = new QScrollBar(Qt::Horizontal);
    scroll->setRange(0, 100);
    scroll->setPageStep(20);
    right->addWidget(scroll);
    right->addWidget(new QLabel(QStringLiteral("A label in the dialog font")));
    columns->addLayout(right);

    tabs->addTab(page, QStringLiteral("General"));
    tabs->addTab(new QWidget, QStringLiteral("Advanced"));
    tabs->addTab(new QWidget, QStringLiteral("Details"));
    auto *central = new QWidget;
    auto *outer = new QVBoxLayout(central);
    outer->addWidget(tabs);
    window.setCentralWidget(central);
    window.resize(640, 480);
    window.show();

    if (!out.isEmpty()) {
        QTimer::singleShot(200, &app, [&] {
            window.grab().save(out);
            app.quit();
        });
    }
    return app.exec();
}
