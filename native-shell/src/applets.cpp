// SPDX-License-Identifier: LGPL-2.1-or-later
#include "applets.h"

#include "fileicons.h"
#include "filebrowser.h"
#include "programs.h"
#include "xpdialog.h"
#include "theme.h"

#include <QLineEdit>
#include "x11.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextStream>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include <unistd.h>
// (A field of this C header is named as a C++ keyword.)
#define explicit explicit_
#include <xcb/xkb.h>
#undef explicit

namespace {

QString settingsPath(const char *name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/") + QLatin1String(name);
}

QString commandOutput(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(3000))
        return QString();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

// A property sheet of XP's: tabs, then OK, Cancel and Apply when APPLY is given.
class Sheet : public QDialog
{
public:
    Sheet(const QString &title, std::function<bool()> apply = nullptr)
        : m_apply(std::move(apply))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title);
        tabs = new xpdialog::SheetTabs(this);
        auto *ok = new QPushButton(QObject::tr("OK"), this);
        ok->setDefault(true);
        QList<QPushButton *> buttons{ok};
        connect(ok, &QPushButton::clicked, this, [this] {
            if (!m_apply || m_apply())
                accept();
        });
        if (m_apply) {
            auto *cancel = new QPushButton(QObject::tr("Cancel"), this);
            connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
            applyButton = new QPushButton(QObject::tr("&Apply"), this);
            connect(applyButton, &QPushButton::clicked, this, [this] { m_apply(); });
            buttons << cancel << applyButton;
        }
        xpdialog::arrangeSheet(this, tabs, buttons);
    }
    xpdialog::SheetTabs *tabs;
    QPushButton *applyButton = nullptr;

private:
    std::function<bool()> m_apply;
};

QString osName()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (file.open(QIODevice::ReadOnly))
        for (const QByteArray &line : file.readAll().split('\n'))
            if (line.startsWith("PRETTY_NAME="))
                return QString::fromUtf8(line.mid(12)).remove(QLatin1Char('"'));
    return QSysInfo::prettyProductName();
}

QString cpuName()
{
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (file.open(QIODevice::ReadOnly))
        for (const QByteArray &line : file.readAll().split('\n'))
            if (line.startsWith("model name"))
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1)).trimmed();
    return QSysInfo::currentCpuArchitecture();
}

qint64 memoryBytes()
{
    return qint64(sysconf(_SC_PHYS_PAGES)) * sysconf(_SC_PAGESIZE);
}

// General's words go in XP's own lines; a long one takes the next.
void showSystem()
{
    const auto sysdm = xpfiles::resources(xpfiles::find(QStringLiteral("sysdm.cpl")));
    QString title = sysdm ? xpfiles::string(*sysdm, 3) : QString();
    title.remove(QChar(0));
    // OK, Cancel and a greyed Apply, as XP's until something changes: nothing here does.
    auto *sheet = new Sheet(title.isEmpty() ? QObject::tr("System Properties") : title, [] { return true; });
    sheet->applyButton->setEnabled(false);

    auto *general = new QWidget;
    if (sysdm) {
        const xpdialog::Page page = xpdialog::build(*sysdm, 101, general);
        if (auto *logo = qobject_cast<QLabel *>(page.controls.value(51))) {
            // Its magenta is see-through; in this picture that is 255, 1, 255.
            QImage art = xpfiles::bitmap(*sysdm, 1).convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < art.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(art.scanLine(y));
                for (int x = 0; x < art.width(); ++x)
                    if (qRed(row[x]) == 255 && qGreen(row[x]) <= 1 && qBlue(row[x]) == 255)
                        row[x] = 0;
            }
            logo->setPixmap(QPixmap::fromImage(art));
            logo->setAlignment(Qt::AlignCenter);
        }
        const auto fill = [&](int id, const QString &value) {
            if (auto *label = qobject_cast<QLabel *>(page.controls.value(id)))
                label->setText(value);
        };
        fill(52, osName());
        fill(53, QObject::tr("Linux %1").arg(QSysInfo::kernelVersion()));
        fill(54, QSysInfo::currentCpuArchitecture());
        fill(56, qEnvironmentVariable("USER"));
        fill(57, QSysInfo::machineHostName());
        // CPU names are long: as XP, on a line or two, then the memory
        const QFontMetrics metrics(general->font());
        const int room = page.controls.value(63) ? page.controls.value(63)->width() : 165;
        QStringList lines{QString()};
        for (const QString &word : cpuName().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString longer = lines.last().isEmpty() ? word : lines.last() + QLatin1Char(' ') + word;
            if (metrics.horizontalAdvance(longer) > room && !lines.last().isEmpty())
                lines.append(word);
            else
                lines.last() = longer;
        }
        lines.append(QObject::tr("%1 of RAM").arg(QLocale().formattedDataSize(memoryBytes(), 2, QLocale::DataSizeTraditionalFormat)));
        const QList<int> computer{63, 64, 65, 66, 67};
        for (int i = 0; i < computer.size(); ++i)
            if (QWidget *line = page.controls.value(computer[i])) {
                fill(computer[i], lines.value(i));
                line->setVisible(i < lines.size());
            }
    }
    sheet->tabs->addTab(general, QObject::tr("General"));

    // The pages of XP's that have nothing behind them here show as XP has them, greyed.
    const auto xpPage = [&](const char *file, int id, const QString &name, const std::function<void(const xpdialog::Page &)> &fill = {}) {
        const auto resources = xpfiles::resources(xpfiles::find(QLatin1String(file)));
        if (!resources)
            return;
        auto *widget = new QWidget;
        const xpdialog::Page page = xpdialog::build(*resources, id, widget);
        if (page.controls.isEmpty()) {
            delete widget;
            return;
        }
        xpdialog::greyControls(page);
        if (fill)
            fill(page);
        sheet->tabs->addTab(widget, name);
    };
    xpPage("netid.dll", 114, QObject::tr("Computer Name"), [](const xpdialog::Page &page) {
        for (int id : {1017, 1026, 1019})
            if (QWidget *control = page.controls.value(id))
                control->hide();
        if (auto *full = qobject_cast<QLabel *>(page.controls.value(1001)))
            full->setText(QSysInfo::machineHostName());
        if (auto *group = qobject_cast<QLabel *>(page.controls.value(116)))
            group->setText(QStringLiteral("WORKGROUP"));
    });
    xpPage("sysdm.cpl", 2000, QObject::tr("Hardware"));
    xpPage("sysdm.cpl", 115, QObject::tr("Advanced"));
    xpPage("wuaucpl.cpl", 912, QObject::tr("Automatic Updates"), [](const xpdialog::Page &page) {
        // A link and its plain text share the place: XP shows one of them.
        if (QWidget *link = page.controls.value(1040))
            link->hide();
    });
    xpPage("remotepg.dll", 510, QObject::tr("Remote"), [](const xpdialog::Page &page) {
        if (auto *name = qobject_cast<QLabel *>(page.controls.value(538)))
            name->setText(QSysInfo::machineHostName());
    });
    sheet->show();
}

// "About Windows" (winver): shell32.dll's dialog, its banner and its line. The version line is
// the system's that really runs here, as in System Properties.
void showAbout()
{
    static QPointer<QDialog> open;
    if (open) {
        open->raise();
        open->activateWindow();
        return;
    }
    const auto shell = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    if (!shell)
        return;
    auto *dialog = new QDialog;
    open = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const xpdialog::Page page = xpdialog::build(*shell, 14352, dialog);
    const QString windows = QStringLiteral("Windows");
    dialog->setWindowTitle(QString(page.caption).replace(QLatin1String("%s"), windows));
    const auto picture = [&](int id, int y) {
        auto *label = new QLabel(dialog);
        const QImage image = xpfiles::bitmap(*shell, id);
        label->setPixmap(QPixmap::fromImage(image));
        label->setGeometry(0, y, image.width(), image.height());
    };
    // Banner 131, then its line 138: 256 colours, as XP's at more than 16
    picture(131, 0);
    picture(138, 72);
    const auto label = [&](int id) { return qobject_cast<QLabel *>(page.controls.value(id)); };
    if (QLabel *icon = label(12297)) {
        if (const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe"))))
            icon->setPixmap(QPixmap::fromImage(xpfiles::icon(*explorer, 100, 32)));
    }
    const auto fill = [&](int id, const QString &value) {
        if (QLabel *line = label(id))
            line->setText(value);
    };
    if (QLabel *line = label(13568))
        line->setText(line->text().replace(QLatin1String("%s"), windows));
    fill(13579, QObject::tr("%1, Linux %2").arg(osName(), QSysInfo::kernelVersion()));
    if (QLabel *line = label(13578))
        line->setText(line->text().replace(QLatin1String("%s"), QStringLiteral("1985-2001")));
    fill(13575, qEnvironmentVariable("USER"));
    fill(13576, QSysInfo::machineHostName());
    // XP's words for it: the whole number of KB
    fill(13571, QObject::tr("%1 KB").arg(QLocale().toString(memoryBytes() / 1024)));
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        QObject::connect(ok, &QPushButton::clicked, dialog, &QDialog::accept);
    }
    xpdialog::placeUpperLeft(dialog);
    dialog->show();
    dialog->activateWindow();
}

// XP's clock in the Date and Time page: an analogue face that follows the time edit
class ClockFace : public QWidget
{
public:
    ClockFace(QTimeEdit *time, QWidget *parent)
        : QWidget(parent)
        , m_time(time)
    {
        connect(time, &QTimeEdit::timeChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int side = qMin(width(), height());
        painter.translate(width() / 2.0, height() / 2.0);
        painter.scale(side / 200.0, side / 200.0);
        painter.setBrush(Qt::black);
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 60; ++i) {
            painter.save();
            painter.rotate(i * 6);
            if (i % 5 == 0) {
                painter.setBrush(QColor(0, 128, 128));
                painter.drawRect(-4, -92, 8, 8);
            } else {
                painter.setBrush(Qt::darkGray);
                painter.drawRect(-1, -90, 2, 2);
            }
            painter.restore();
        }
        const QTime t = m_time->time();
        const auto hand = [&](double angle, int length, int widthAtBase, const QColor &colour) {
            painter.save();
            painter.rotate(angle);
            painter.setBrush(colour);
            painter.drawPolygon(QPolygon({QPoint(-widthAtBase, 0), QPoint(0, -length), QPoint(widthAtBase, 0), QPoint(0, 10)}));
            painter.restore();
        };
        hand(30.0 * (t.hour() % 12) + t.minute() / 2.0, 50, 8, QColor(0, 128, 128));
        hand(6.0 * t.minute(), 74, 6, QColor(0, 128, 128));
        hand(6.0 * t.second(), 80, 1, Qt::black);
    }

private:
    QTimeEdit *m_time;
};

void showDateTime()
{
    const auto cpl = xpfiles::resources(xpfiles::find(QStringLiteral("timedate.cpl")));
    QString title = cpl ? xpfiles::string(*cpl, 304) : QString();
    auto *calendar = new QCalendarWidget;
    auto *time = new QTimeEdit(QTime::currentTime());
    auto *zone = new QComboBox;
    auto *ntp = new QCheckBox;
    const QDateTime opened = QDateTime::currentDateTime();
    QPointer<QCalendarWidget> calendarGuard(calendar);
    auto *sheet = new Sheet(title.isEmpty() ? QObject::tr("Date and Time Properties") : title, [=]() -> bool {
        // Each is the system's own tool, which asks for the administrator's rights.
        QStringList failed;
        const auto run = [&](const QStringList &arguments) {
            if (QProcess::execute(QStringLiteral("timedatectl"), arguments) != 0)
                failed << arguments.first();
        };
        // What the user chose, not what a list without the system's zone happens to show.
        if (zone->currentText() != zone->property("opened").toString())
            run({QStringLiteral("set-timezone"), zone->currentText()});
        const bool wasNtp = commandOutput(QStringLiteral("timedatectl"), {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTP"), QStringLiteral("--value")}) == QLatin1String("yes");
        if (ntp->isChecked() != wasNtp)
            run({QStringLiteral("set-ntp"), ntp->isChecked() ? QStringLiteral("true") : QStringLiteral("false")});
        const QDateTime chosen(calendar->selectedDate(), time->time());
        // The fields follow the clock: only a real change of the user's sets the time.
        if (!ntp->isChecked() && qAbs(chosen.secsTo(QDateTime::currentDateTime())) > 2 && (calendar->selectedDate() != opened.date() || time->property("edited").toBool()))
            run({QStringLiteral("set-time"), chosen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
        if (!failed.isEmpty())
            QMessageBox::warning(calendar->window(), QObject::tr("Date and Time"), QObject::tr("The system refused this change (%1).").arg(failed.join(QStringLiteral(", "))));
        return failed.isEmpty();
    });
    auto *datePage = new QWidget;
    if (cpl)
        xpdialog::build(*cpl, 1, datePage);
    // Only the frames of XP's page stay; Qt's calendar and time edit take the places of its own.
    const auto controls = datePage->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *control : controls)
        if (!qobject_cast<QGroupBox *>(control))
            control->hide();
    if (datePage->width() < 100)
        datePage->setFixedSize(378, 237);
    const auto groups = datePage->findChildren<QGroupBox *>(QString(), Qt::FindDirectChildrenOnly);
    QRect dateRect(14, 22, 182, 190), timeRect(214, 22, 150, 190);
    if (groups.size() >= 2) {
        dateRect = groups[0]->geometry().adjusted(8, 18, -8, -8);
        timeRect = groups[1]->geometry().adjusted(8, 18, -8, -8);
    }
    calendar->setParent(datePage);
    calendar->setGeometry(dateRect);
    calendar->setGridVisible(false);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    auto *clock = new ClockFace(time, datePage);
    clock->setGeometry(timeRect.adjusted(0, 0, 0, -30));
    time->setParent(datePage);
    time->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    time->setGeometry(timeRect.x() + (timeRect.width() - 90) / 2, timeRect.bottom() - 26, 90, 22);
    // Time runs in the field until the user changes it.
    auto *timer = new QTimer(time);
    auto *edited = new QObject(time);
    Q_UNUSED(edited);
    QObject::connect(time, &QTimeEdit::editingFinished, time, [time] { time->setProperty("edited", true); });
    QObject::connect(timer, &QTimer::timeout, time, [time] {
        if (!time->hasFocus() && !time->property("edited").toBool())
            time->setTime(QTime::currentTime());
    });
    timer->start(1000);
    sheet->tabs->addTab(datePage, QObject::tr("Date && Time"));

    auto *zonePage = new QWidget;
    auto *zoneLayout = new QVBoxLayout(zonePage);
    zoneLayout->addWidget(new QLabel(QObject::tr("Time zone:"), zonePage));
    const QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
    for (const QByteArray &id : ids)
        zone->addItem(QString::fromUtf8(id));
    if (zone->findText(QString::fromUtf8(QTimeZone::systemTimeZoneId())) < 0)
        zone->insertItem(0, QString::fromUtf8(QTimeZone::systemTimeZoneId()));
    zone->setCurrentText(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
    zone->setProperty("opened", zone->currentText());
    zone->setParent(zonePage);
    zoneLayout->addWidget(zone);
    zoneLayout->addStretch(1);
    sheet->tabs->addTab(zonePage, QObject::tr("Time Zone"));

    auto *internetPage = new QWidget;
    auto *internetLayout = new QVBoxLayout(internetPage);
    ntp->setText(QObject::tr("Automatically synchronize with an Internet time server"));
    ntp->setChecked(commandOutput(QStringLiteral("timedatectl"), {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTP"), QStringLiteral("--value")}) == QLatin1String("yes"));
    ntp->setParent(internetPage);
    internetLayout->addWidget(ntp);
    internetLayout->addStretch(1);
    sheet->tabs->addTab(internetPage, QObject::tr("Internet Time"));
    sheet->show();
}

void showSound()
{
    auto *device = new QComboBox;
    auto *slider = new QSlider(Qt::Horizontal);
    auto *mute = new QCheckBox;
    const QString currentSink = commandOutput(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")});
    const auto cpl = xpfiles::resources(xpfiles::find(QStringLiteral("mmsys.cpl")));
    auto *sheet = new Sheet(QObject::tr("Sounds and Audio Devices Properties"), [=]() -> bool {
        const QString sink = device->currentData().toString();
        if (!sink.isEmpty() && sink != currentSink)
            QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), sink});
        QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("%1%").arg(slider->value())});
        QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), mute->isChecked() ? QStringLiteral("1") : QStringLiteral("0")});
        return true;
    });
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *picture = new QLabel(page);
    if (cpl)
        picture->setPixmap(QPixmap::fromImage(xpfiles::icon(*cpl, 3004, 32)));
    layout->addWidget(picture);
    auto *group = new QGroupBox(QObject::tr("Device volume"), page);
    auto *groupLayout = new QVBoxLayout(group);
    slider->setParent(group);
    slider->setRange(0, 100);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(10);
    const QString volume = commandOutput(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
    const int percent = volume.indexOf(QLatin1Char('%'));
    if (percent > 0) {
        int start = percent;
        while (start > 0 && volume[start - 1].isDigit())
            --start;
        slider->setValue(volume.mid(start, percent - start).toInt());
    }
    groupLayout->addWidget(slider);
    mute->setText(QObject::tr("&Mute"));
    mute->setChecked(commandOutput(QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")}).endsWith(QLatin1String("yes")));
    mute->setParent(group);
    groupLayout->addWidget(mute);
    layout->addWidget(group);
    auto *playback = new QGroupBox(QObject::tr("Sound playback"), page);
    auto *playbackLayout = new QVBoxLayout(playback);
    playbackLayout->addWidget(new QLabel(QObject::tr("Default device:"), playback));
    device->setParent(playback);
    for (const QString &line : commandOutput(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sinks")}).split(QLatin1Char('\n'))) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() >= 2)
            device->addItem(fields[1], fields[1]);
    }
    device->setCurrentIndex(qMax(0, device->findData(currentSink)));
    playbackLayout->addWidget(device);
    layout->addWidget(playback);
    layout->addStretch(1);
    page->setMinimumSize(378, 320);
    sheet->tabs->addTab(page, QObject::tr("Volume"));
    sheet->show();
}

// The pointer's speed and buttons through the X server's own requests
void applyPointer(int speed, bool swapped)
{
    // Speed 1 to 10; X's acceleration as a fraction, and how far it starts.
    const int16_t numerator = int16_t(qMax(1, speed)), denominator = 5;
    xcb_change_pointer_control(x11::connection(), numerator, denominator, 4, 1, 1);
    auto *mapping = xcb_get_pointer_mapping_reply(x11::connection(), xcb_get_pointer_mapping(x11::connection()), nullptr);
    if (mapping) {
        const int count = xcb_get_pointer_mapping_map_length(mapping);
        QByteArray map(reinterpret_cast<const char *>(xcb_get_pointer_mapping_map(mapping)), count);
        if (count >= 3) {
            map[0] = char(swapped ? 3 : 1);
            map[2] = char(swapped ? 1 : 3);
        }
        free(mapping);
        xcb_set_pointer_mapping(x11::connection(), uint8_t(map.size()), reinterpret_cast<const uint8_t *>(map.constData()));
    }
    xcb_flush(x11::connection());
}

void showMouse()
{
    QSettings settings(settingsPath("native-input.conf"), QSettings::IniFormat);
    auto *swap = new QCheckBox;
    auto *speed = new QSlider(Qt::Horizontal);
    auto *sheet = new Sheet(QObject::tr("Mouse Properties"), [=]() -> bool {
        QSettings write(settingsPath("native-input.conf"), QSettings::IniFormat);
        write.setValue(QStringLiteral("pointerSpeed"), speed->value());
        write.setValue(QStringLiteral("swapButtons"), swap->isChecked());
        applyPointer(speed->value(), swap->isChecked());
        return true;
    });
    auto *buttons = new QWidget;
    auto *buttonsLayout = new QVBoxLayout(buttons);
    auto *config = new QGroupBox(QObject::tr("Button configuration"), buttons);
    auto *configLayout = new QVBoxLayout(config);
    swap->setText(QObject::tr("&Switch primary and secondary buttons"));
    swap->setChecked(settings.value(QStringLiteral("swapButtons"), false).toBool());
    swap->setParent(config);
    configLayout->addWidget(swap);
    configLayout->addWidget(new QLabel(QObject::tr("Select this check box to make the button on the right the one you use for primary functions such as selecting and dragging."), config));
    static_cast<QLabel *>(configLayout->itemAt(1)->widget())->setWordWrap(true);
    buttonsLayout->addWidget(config);
    buttonsLayout->addStretch(1);
    buttons->setMinimumSize(378, 320);
    sheet->tabs->addTab(buttons, QObject::tr("Buttons"));
    auto *options = new QWidget;
    auto *optionsLayout = new QVBoxLayout(options);
    auto *motion = new QGroupBox(QObject::tr("Motion"), options);
    auto *motionLayout = new QVBoxLayout(motion);
    motionLayout->addWidget(new QLabel(QObject::tr("Select a pointer speed:"), motion));
    speed->setParent(motion);
    speed->setRange(1, 10);
    speed->setTickPosition(QSlider::TicksBelow);
    // What the pointer has now until something is chosen: Apply alone must not change its speed.
    int current = 5;
    if (auto *control = xcb_get_pointer_control_reply(x11::connection(), xcb_get_pointer_control(x11::connection()), nullptr)) {
        if (control->acceleration_denominator)
            current = qBound(1, control->acceleration_numerator * 5 / control->acceleration_denominator, 10);
        free(control);
    }
    speed->setValue(settings.value(QStringLiteral("pointerSpeed"), current).toInt());
    motionLayout->addWidget(speed);
    optionsLayout->addWidget(motion);
    optionsLayout->addStretch(1);
    sheet->tabs->addTab(options, QObject::tr("Pointer Options"));
    sheet->show();
}

// The keyboard's repeat, through the X server's XKB controls.
void applyRepeat(int delayStep, int rateStep)
{
    xcb_connection_t *connection = x11::connection();
    xcb_xkb_use_extension_reply_t *version = xcb_xkb_use_extension_reply(connection, xcb_xkb_use_extension(connection, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION), nullptr);
    if (!version)
        return;
    free(version);
    auto *controls = xcb_xkb_get_controls_reply(connection, xcb_xkb_get_controls(connection, XCB_XKB_ID_USE_CORE_KBD), nullptr);
    if (!controls)
        return;
    // XP's four delays; its rates of about 2 to 30 characters a second
    const uint16_t delay = uint16_t(250 * (delayStep + 1)), interval = uint16_t(1000 / (2 + rateStep));
    // Only the repeat changes: nothing is affected in the masks, and the rest is sent back as it was.
    xcb_xkb_set_controls(connection, XCB_XKB_ID_USE_CORE_KBD, 0, 0, 0, 0, 0, 0, 0, 0,
                         controls->mouseKeysDfltBtn, controls->groupsWrap, controls->accessXOption, 0, 0, XCB_XKB_BOOL_CTRL_REPEAT_KEYS,
                         delay, interval, controls->slowKeysDelay, controls->debounceDelay, controls->mouseKeysDelay,
                         controls->mouseKeysInterval, controls->mouseKeysTimeToMax, controls->mouseKeysMaxSpeed, controls->mouseKeysCurve,
                         controls->accessXTimeout, controls->accessXTimeoutMask, controls->accessXTimeoutValues, controls->accessXTimeoutOptionsMask,
                         controls->accessXTimeoutOptionsValues, controls->perKeyRepeat);
    free(controls);
    xcb_flush(connection);
}

void showKeyboard()
{
    QSettings settings(settingsPath("native-input.conf"), QSettings::IniFormat);
    auto *delay = new QSlider(Qt::Horizontal);
    auto *rate = new QSlider(Qt::Horizontal);
    auto *sheet = new Sheet(QObject::tr("Keyboard Properties"), [=]() -> bool {
        QSettings write(settingsPath("native-input.conf"), QSettings::IniFormat);
        write.setValue(QStringLiteral("repeatDelay"), delay->value());
        write.setValue(QStringLiteral("repeatRate"), rate->value());
        applyRepeat(delay->value(), rate->value());
        return true;
    });
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *group = new QGroupBox(QObject::tr("Character repeat"), page);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(new QLabel(QObject::tr("Repeat delay:"), group));
    delay->setParent(group);
    delay->setRange(0, 3);
    delay->setTickPosition(QSlider::TicksBelow);
    delay->setInvertedAppearance(true);
    delay->setValue(settings.value(QStringLiteral("repeatDelay"), 1).toInt());
    groupLayout->addWidget(delay);
    groupLayout->addWidget(new QLabel(QObject::tr("Repeat rate:"), group));
    rate->setParent(group);
    rate->setRange(0, 28);
    rate->setTickPosition(QSlider::TicksBelow);
    rate->setTickInterval(4);
    rate->setValue(settings.value(QStringLiteral("repeatRate"), 23).toInt());
    groupLayout->addWidget(rate);
    layout->addWidget(group);
    layout->addStretch(1);
    page->setMinimumSize(378, 320);
    sheet->tabs->addTab(page, QObject::tr("Speed"));
    sheet->show();
}

void showUsers()
{
    auto *list = new QListWidget;
    auto *sheet = new Sheet(QObject::tr("User Accounts"), [=]() -> bool {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return true;
        // ~/.face: the picture of the user that login screens and the Start menu read.
        const QImage picture(item->data(Qt::UserRole).toString());
        if (!picture.isNull())
            picture.scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(QDir::homePath() + QStringLiteral("/.face"), "PNG");
        return true;
    });
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(QObject::tr("Pick a new picture for your account, %1:").arg(qEnvironmentVariable("USER")), page));
    list->setParent(page);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(64, 64));
    list->setMovement(QListView::Static);
    list->setResizeMode(QListView::Adjust);
    const QString beach = xpfiles::find(QStringLiteral("beach.bmp"));
    if (!beach.isEmpty()) {
        const QFileInfoList pictures = QFileInfo(beach).absoluteDir().entryInfoList({QStringLiteral("*.bmp")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : pictures) {
            auto *item = new QListWidgetItem(QIcon(file.absoluteFilePath()), QString());
            item->setData(Qt::UserRole, file.absoluteFilePath());
            item->setToolTip(file.completeBaseName());
            list->addItem(item);
        }
    }
    layout->addWidget(list, 1);
    page->setMinimumSize(378, 320);
    sheet->tabs->addTab(page, QObject::tr("Picture"));
    sheet->show();
}

void showFolderOptions()
{
    const QString path = settingsPath("native-explorer.conf");
    QSettings settings(path, QSettings::IniFormat);
    auto *classicFolders = new QRadioButton;
    auto *tasks = new QRadioButton;
    auto *hidden = new QCheckBox;
    auto *sheet = new Sheet(QObject::tr("Folder Options"), [=]() -> bool {
        QSettings write(path, QSettings::IniFormat);
        write.setValue(QStringLiteral("classicFolders"), classicFolders->isChecked());
        write.setValue(QStringLiteral("showHidden"), hidden->isChecked());
        write.sync();
        FileBrowser::settingsChanged();
        return true;
    });
    auto *general = new QWidget;
    auto *generalLayout = new QVBoxLayout(general);
    auto *group = new QGroupBox(QObject::tr("Tasks"), general);
    auto *groupLayout = new QVBoxLayout(group);
    tasks->setText(QObject::tr("Show &common tasks in folders"));
    classicFolders->setText(QObject::tr("Use Windows c&lassic folders"));
    tasks->setParent(group);
    classicFolders->setParent(group);
    (settings.value(QStringLiteral("classicFolders"), false).toBool() ? classicFolders : tasks)->setChecked(true);
    groupLayout->addWidget(tasks);
    groupLayout->addWidget(classicFolders);
    generalLayout->addWidget(group);
    generalLayout->addStretch(1);
    general->setMinimumSize(378, 320);
    sheet->tabs->addTab(general, QObject::tr("General"));
    auto *view = new QWidget;
    auto *viewLayout = new QVBoxLayout(view);
    hidden->setText(QObject::tr("Show &hidden files and folders"));
    hidden->setChecked(settings.value(QStringLiteral("showHidden"), false).toBool());
    hidden->setParent(view);
    viewLayout->addWidget(hidden);
    viewLayout->addStretch(1);
    sheet->tabs->addTab(view, QObject::tr("View"));
    sheet->show();
}

void showPrograms()
{
    auto *dialog = new QDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Add or Remove Programs"));
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(QObject::tr("Currently installed programs:"), dialog));
    auto *list = new QListWidget(dialog);
    list->setIconSize(QSize(32, 32));
    QList<programs::Program> all;
    const auto folders = programs::folders();
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        all += it.value();
    std::sort(all.begin(), all.end(), [](const programs::Program &a, const programs::Program &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    for (const programs::Program &program : std::as_const(all))
        list->addItem(new QListWidgetItem(programs::iconOf(program), program.name));
    layout->addWidget(list, 1);
    layout->addWidget(new QLabel(QObject::tr("Programs are installed and removed with your distribution's package manager."), dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->resize(560, 460);
    dialog->show();
}

void showNetwork()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("nm-connection-editor")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("nm-connection-editor"), {});
        return;
    }
    auto *dialog = new QDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Network Connections"));
    auto *layout = new QVBoxLayout(dialog);
    auto *list = new QListWidget(dialog);
    list->setIconSize(QSize(32, 32));
    for (const QString &line : commandOutput(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE,DEVICE"), QStringLiteral("connection"), QStringLiteral("show")}).split(QLatin1Char('\n'))) {
        const QStringList fields = line.split(QLatin1Char(':'));
        if (fields.size() < 3)
            continue;
        const bool connected = !fields[2].isEmpty();
        list->addItem(new QListWidgetItem(fileicons::shell(18), QObject::tr("%1\n%2 - %3").arg(fields[0], fields[1], connected ? QObject::tr("Connected") : QObject::tr("Not connected"))));
    }
    layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->resize(420, 340);
    dialog->show();
}

} // namespace

namespace applets {

void open(const QString &id)
{
    // One window of each
    static QHash<QString, QPointer<QWidget>> windows;
    if (QWidget *window = windows.value(id); window) {
        window->raise();
        window->activateWindow();
        return;
    }
    const QWidgetList before = QApplication::topLevelWidgets();
    if (id == QLatin1String("winver")) {
        showAbout();
        return;
    }
    if (id == QLatin1String("system"))
        showSystem();
    else if (id == QLatin1String("datetime"))
        showDateTime();
    else if (id == QLatin1String("sound"))
        showSound();
    else if (id == QLatin1String("mouse"))
        showMouse();
    else if (id == QLatin1String("keyboard"))
        showKeyboard();
    else if (id == QLatin1String("users"))
        showUsers();
    else if (id == QLatin1String("folders"))
        showFolderOptions();
    else if (id == QLatin1String("programs"))
        showPrograms();
    else if (id == QLatin1String("network"))
        showNetwork();
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (!before.contains(widget) && qobject_cast<QDialog *>(widget))
            windows.insert(id, widget);
}

void applyInputSettings()
{
    QSettings settings(settingsPath("native-input.conf"), QSettings::IniFormat);
    if (settings.contains(QStringLiteral("pointerSpeed")))
        applyPointer(settings.value(QStringLiteral("pointerSpeed")).toInt(), settings.value(QStringLiteral("swapButtons")).toBool());
    if (settings.contains(QStringLiteral("repeatDelay")))
        applyRepeat(settings.value(QStringLiteral("repeatDelay")).toInt(), settings.value(QStringLiteral("repeatRate")).toInt());
}

} // namespace applets
