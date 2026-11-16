// SPDX-License-Identifier: LGPL-2.1-or-later
#include "minesweeper.h"

#include "applets.h"
#include "sounds.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>

#include <functional>

namespace {

// winmine.exe's resources
enum {
    IDM_MENU = 500,
    IDM_NEW = 510, IDM_EXIT = 512, IDM_BEGINNER = 521, IDM_INTERMEDIATE = 522, IDM_EXPERT = 523, IDM_CUSTOM = 524,
    IDM_SOUND = 526, IDM_MARKS = 527, IDM_BEST = 528, IDM_COLOR = 529,
    IDM_HELP = 590, IDM_HELP_SEARCH = 591, IDM_HELP_USING = 592, IDM_ABOUT = 593,
    IDS_NAME = 1, IDS_SECONDS = 7, IDS_ANONYMOUS = 8, IDS_FASTEST = 9, IDS_ABOUT = 12, IDS_AUTHORS = 13,
    IDD_CUSTOM = 80, IDC_HEIGHT = 141, IDC_WIDTH = 142, IDC_MINES = 143,
    IDD_NAME = 600, IDC_NAME_TEXT = 601, IDC_NAME = 602, IDC_NAME_OK = 100,
    IDD_BEST = 700, IDC_TIME1 = 701, IDC_NAME1 = 702, IDC_RESET = 707,
    IDB_TILES = 410, IDB_DIGITS = 420, IDB_FACES = 430,
    IDW_TICK = 432, IDW_WIN = 433, IDW_LOSE = 434,
    IDI_MAIN = 100,
};

// A square's byte, as winmine keeps it: its picture in the low bits
enum : uchar {
    MINE = 0x80, SHOWN = 0x40, PICTURE = 0x1f,
    EMPTY = 0, QUESTION_DOWN = 9, MINE_SHOWN = 10, WRONG = 11, EXPLODED = 12, QUESTION = 13, FLAG = 14, BLANK = 15, EDGE = 16,
};
enum Face { Smile, Ooh, Dead, Cool, SmileDown };
enum Level { Beginner, Intermediate, Expert, Custom };

const int TILE = 16, DIGIT_W = 13, DIGIT_H = 23, FACE = 24;
// The field's place in the window, and what surrounds it
const int LEFT = 12, TOP = 55, RIGHT = 12, BOTTOM = 12, PANEL_Y = 16;

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/winmine.conf");
}

// GDI's LineTo, which leaves the last point out
void line(QPainter &p, const QColor &colour, int x1, int y1, int x2, int y2)
{
    if (x1 == x2)
        p.fillRect(QRect(x1, qMin(y1, y2) + (y2 < y1 ? 1 : 0), 1, qAbs(y2 - y1)), colour);
    else
        p.fillRect(QRect(qMin(x1, x2) + (x2 < x1 ? 1 : 0), y1, qAbs(x2 - x1), 1), colour);
}

// winmine's borders: WIDTH lines, the left and top in one colour and the right and bottom in the
// other; RAISED 1 is white first, 0 grey first, 2 grey all round
void border(QPainter &p, int x1, int y1, int x2, int y2, int width, int raised)
{
    const QColor white(Qt::white), grey(128, 128, 128);
    const QColor first = raised & 1 ? white : grey;
    const QColor second = raised >= 2 ? first : raised & 1 ? grey : white;
    for (int i = 0; i < width; ++i) {
        --y2;
        line(p, first, x1, y2, x1, y1);
        line(p, first, x1, y1, x2, y1);
        ++x1;
        --x2;
        ++y1;
    }
    for (int i = 0; i < width; ++i) {
        ++y2;
        --x1;
        ++x2;
        line(p, second, x1 + 1, y2, x2, y2);
        --y1;
        line(p, second, x2, y2, x2, y1);
    }
}

} // namespace

class MineField : public QWidget
{
public:
    MineField(const w4w::PeResources &pe, QWidget *parent);

    void newGame();
    void setLevel(int level, int width, int height, int mines);
    void setColour(bool on);
    void setMarks(bool on) { m_marks = on; }
    void setSound(bool on) { m_sound = on; }
    int level() const { return m_level; }
    int columns() const { return m_width; }
    int rows() const { return m_height; }
    int mines() const { return m_mines; }
    bool colour() const { return m_colour; }
    bool marks() const { return m_marks; }
    bool sound() const { return m_sound; }

    std::function<void()> resized;
    std::function<void(int)> won;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    uchar &at(int x, int y) { return m_grid[y * 32 + x]; }
    uchar at(int x, int y) const { return m_grid[y * 32 + x]; }
    bool inside(int x, int y) const { return x >= 1 && y >= 1 && x <= m_width && y <= m_height; }
    QPoint squareAt(const QPoint &point) const { return {(point.x() + 4) >> 4, (point.y() - 39) >> 4}; }
    QRect faceRect() const { return {(width() - FACE) / 2, PANEL_Y, FACE, FACE}; }
    int around(int x, int y, uchar what) const;
    void hover(const QPoint &square);
    void press(int x, int y, bool down);
    void step(int x, int y);
    void chord(int x, int y);
    void show(int x, int y);
    void flood(int x, int y);
    void mark(int x, int y);
    void finish(bool win);
    void play(int id);
    void tick();

    const w4w::PeResources &m_pe;
    QImage m_tiles, m_digits, m_faces;
    uchar m_grid[27 * 32] = {};
    int m_level = Beginner, m_width = 9, m_height = 9, m_mines = 10;
    bool m_colour = true, m_marks = true, m_sound = false;
    int m_left = 0, m_shown = 0, m_goal = 0, m_seconds = 0, m_timer = 0;
    int m_face = Smile;
    bool m_playing = false, m_tracking = false, m_chording = false, m_onFace = false;
    QPoint m_hover{-1, -1};
};

MineField::MineField(const w4w::PeResources &pe, QWidget *parent)
    : QWidget(parent), m_pe(pe)
{
    setColour(true);
}

void MineField::setColour(bool on)
{
    m_colour = on;
    // The monochrome bitmaps follow the colour ones
    m_tiles = xpfiles::bitmap(m_pe, IDB_TILES + (on ? 0 : 1));
    m_digits = xpfiles::bitmap(m_pe, IDB_DIGITS + (on ? 0 : 1));
    m_faces = xpfiles::bitmap(m_pe, IDB_FACES + (on ? 0 : 1));
    update();
}

void MineField::setLevel(int level, int width, int height, int mines)
{
    m_level = level;
    m_width = width;
    m_height = height;
    m_mines = mines;
    newGame();
}

void MineField::newGame()
{
    if (m_timer)
        killTimer(m_timer);
    m_timer = 0;
    std::fill(std::begin(m_grid), std::end(m_grid), uchar(BLANK));
    for (int x = 0; x <= m_width + 1; ++x)
        at(x, 0) = at(x, m_height + 1) = EDGE;
    for (int y = 0; y <= m_height + 1; ++y)
        at(0, y) = at(m_width + 1, y) = EDGE;
    for (int placed = 0; placed < m_mines;) {
        const int x = QRandomGenerator::global()->bounded(m_width) + 1;
        const int y = QRandomGenerator::global()->bounded(m_height) + 1;
        if (at(x, y) & MINE)
            continue;
        at(x, y) |= MINE;
        ++placed;
    }
    m_face = Smile;
    m_seconds = 0;
    m_shown = 0;
    m_left = m_mines;
    m_goal = m_width * m_height - m_mines;
    m_playing = true;
    m_tracking = m_chording = m_onFace = false;
    m_hover = QPoint(-1, -1);
    const QSize size(m_width * TILE + LEFT + RIGHT, m_height * TILE + TOP + BOTTOM);
    if (size != this->size()) {
        setFixedSize(size);
        if (resized)
            resized();
    }
    update();
}

int MineField::around(int x, int y, uchar what) const
{
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const uchar square = at(x + dx, y + dy);
            if (what == MINE ? (square & MINE) != 0 : (square & PICTURE) == what)
                ++count;
        }
    }
    return count;
}

void MineField::play(int id)
{
    if (!m_sound)
        return;
    // The player takes files: the WAVE resource, written out once
    const QString path = QDir::tempPath() + QStringLiteral("/win4wine-winmine-%1.wav").arg(id);
    if (!QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(m_pe.find(QStringLiteral("WAVE"), QString::number(id)));
    }
    sounds::playFile(path);
}

void MineField::tick()
{
    if (m_seconds < 999) {
        ++m_seconds;
        play(IDW_TICK);
        update();
    }
}

void MineField::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timer)
        tick();
}

// A covered square shown held down, or let go again
void MineField::press(int x, int y, bool down)
{
    if (!inside(x, y) || (at(x, y) & SHOWN))
        return;
    uchar picture = at(x, y) & PICTURE;
    if (down)
        picture = picture == BLANK ? uchar(EMPTY) : picture == QUESTION ? uchar(QUESTION_DOWN) : picture;
    else
        picture = picture == EMPTY ? uchar(BLANK) : picture == QUESTION_DOWN ? uchar(QUESTION) : picture;
    at(x, y) = (at(x, y) & ~PICTURE) | picture;
}

void MineField::hover(const QPoint &square)
{
    if (square == m_hover)
        return;
    const int reach = m_chording ? 1 : 0;
    for (const bool down : {false, true}) {
        const QPoint centre = down ? square : m_hover;
        if (!inside(centre.x(), centre.y()))
            continue;
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx)
                press(centre.x() + dx, centre.y() + dy, down);
        }
    }
    m_hover = square;
    update();
}

void MineField::show(int x, int y)
{
    at(x, y) = SHOWN | uchar(around(x, y, MINE));
    ++m_shown;
}

// A square with no mine: itself, and from an empty one everything around it
void MineField::flood(int x, int y)
{
    QList<QPoint> queue{QPoint(x, y)};
    show(x, y);
    while (!queue.isEmpty()) {
        const QPoint from = queue.takeFirst();
        if (at(from.x(), from.y()) & PICTURE)
            continue;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = from.x() + dx, ny = from.y() + dy;
                const uchar square = at(nx, ny);
                if ((square & SHOWN) || (square & PICTURE) == EDGE || (square & PICTURE) == FLAG)
                    continue;
                show(nx, ny);
                queue.append(QPoint(nx, ny));
            }
        }
    }
}

void MineField::step(int x, int y)
{
    if (at(x, y) & MINE) {
        if (m_shown != 0) {
            at(x, y) = SHOWN | EXPLODED;
            finish(false);
            return;
        }
        // The first square is never a mine: it goes to the first free square from the top left
        for (int row = 1; row < m_height; ++row) {
            for (int column = 1; column < m_width; ++column) {
                if (!(at(column, row) & MINE)) {
                    at(x, y) = BLANK;
                    at(column, row) |= MINE;
                    flood(x, y);
                    if (m_shown == m_goal)
                        finish(true);
                    return;
                }
            }
        }
        return;
    }
    flood(x, y);
    if (m_shown == m_goal)
        finish(true);
}

// Both buttons on a number that has its flags: the other squares around it
void MineField::chord(int x, int y)
{
    const uchar centre = at(x, y);
    if (!(centre & SHOWN) || (centre & PICTURE) != around(x, y, FLAG))
        return;
    bool lost = false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            const uchar square = at(nx, ny);
            if ((square & SHOWN) || (square & PICTURE) == EDGE || (square & PICTURE) == FLAG)
                continue;
            if (square & MINE) {
                at(nx, ny) = SHOWN | EXPLODED;
                lost = true;
            } else {
                flood(nx, ny);
            }
        }
    }
    if (lost)
        finish(false);
    else if (m_shown == m_goal)
        finish(true);
}

// The right button: a flag, then the question mark if Marks (?) is on, then nothing
void MineField::mark(int x, int y)
{
    if (!inside(x, y) || (at(x, y) & SHOWN))
        return;
    uchar picture = at(x, y) & PICTURE;
    if (picture == FLAG) {
        picture = m_marks ? QUESTION : BLANK;
        ++m_left;
    } else if (picture == QUESTION) {
        picture = BLANK;
    } else {
        picture = FLAG;
        --m_left;
    }
    at(x, y) = (at(x, y) & ~PICTURE) | picture;
    update();
}

void MineField::finish(bool win)
{
    if (m_timer)
        killTimer(m_timer);
    m_timer = 0;
    m_playing = false;
    m_face = win ? Cool : Dead;
    for (int y = 1; y <= m_height; ++y) {
        for (int x = 1; x <= m_width; ++x) {
            uchar &square = at(x, y);
            if (square & SHOWN)
                continue;
            const uchar picture = square & PICTURE;
            if (square & MINE) {
                if (picture != FLAG)
                    square = (square & ~PICTURE) | (win ? FLAG : MINE_SHOWN);
            } else if (picture == FLAG) {
                square = (square & ~PICTURE) | WRONG;
            }
        }
    }
    if (win)
        m_left = 0;
    play(win ? IDW_WIN : IDW_LOSE);
    update();
    if (win && won)
        won(m_seconds);
}

void MineField::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && faceRect().contains(event->pos())) {
        m_onFace = true;
        m_face = SmileDown;
        update();
        return;
    }
    if (!m_playing || m_onFace)
        return;
    const QPoint square = squareAt(event->pos());
    const bool both = (event->buttons() & Qt::LeftButton) && (event->buttons() & Qt::RightButton);
    if (event->button() == Qt::RightButton && !both) {
        if (!m_tracking)
            mark(square.x(), square.y());
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton && event->button() != Qt::RightButton)
        return;
    // Both buttons, the middle one or Shift clear around a number
    hover(QPoint(-1, -1));
    m_chording = both || event->button() == Qt::MiddleButton || (event->modifiers() & Qt::ShiftModifier);
    m_tracking = true;
    m_face = Ooh;
    hover(square);
    update();
}

void MineField::mouseMoveEvent(QMouseEvent *event)
{
    if (m_onFace) {
        const int face = faceRect().contains(event->pos()) ? SmileDown : m_playing ? Smile : m_face == SmileDown ? Smile : m_face;
        if (faceRect().contains(event->pos()) != (m_face == SmileDown)) {
            m_face = face;
            update();
        }
        return;
    }
    if (m_tracking)
        hover(squareAt(event->pos()));
}

void MineField::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_onFace) {
        if (event->button() != Qt::LeftButton)
            return;
        m_onFace = false;
        if (faceRect().contains(event->pos()))
            newGame();
        update();
        return;
    }
    if (!m_tracking)
        return;
    const QPoint square = squareAt(event->pos());
    const bool chording = m_chording;
    hover(QPoint(-1, -1));
    m_tracking = m_chording = false;
    m_face = Smile;
    if (m_playing && inside(square.x(), square.y())) {
        // The clock starts with the first square
        if (m_shown == 0 && m_seconds == 0) {
            play(IDW_TICK);
            m_seconds = 1;
            m_timer = startTimer(1000);
        }
        if (chording)
            chord(square.x(), square.y());
        else if (!(at(square.x(), square.y()) & SHOWN) && (at(square.x(), square.y()) & PICTURE) != FLAG)
            step(square.x(), square.y());
    }
    update();
}

void MineField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int dx = width(), dy = height();
    p.fillRect(rect(), QColor(192, 192, 192));
    border(p, 0, 0, dx - 1, dy - 1, 3, 1);
    border(p, 9, 52, dx - 10, dy - 10, 3, 0);
    border(p, 9, 9, dx - 10, 45, 2, 0);
    border(p, 16, 15, 56, 39, 1, 0);
    border(p, dx - 57, 15, dx - 17, 39, 1, 0);
    const QRect face = faceRect();
    border(p, face.x() - 1, 15, face.x() + 24, 40, 1, 2);
    // Each strip's pictures count from the bottom
    const auto picture = [&p](const QImage &strip, int index, int x, int y, int w, int h) {
        p.drawImage(QPoint(x, y), strip, QRect(0, strip.height() - (index + 1) * h, w, h));
    };
    const auto number = [&](int x, int value) {
        int hundreds = value / 100;
        if (value < 0) {
            hundreds = 11;
            value = -value % 100;
        }
        picture(m_digits, hundreds, x, PANEL_Y, DIGIT_W, DIGIT_H);
        picture(m_digits, value % 100 / 10, x + DIGIT_W, PANEL_Y, DIGIT_W, DIGIT_H);
        picture(m_digits, value % 10, x + 2 * DIGIT_W, PANEL_Y, DIGIT_W, DIGIT_H);
    };
    number(17, m_left);
    number(dx - 56, m_seconds);
    picture(m_faces, m_face, face.x(), face.y(), FACE, FACE);
    for (int y = 1; y <= m_height; ++y) {
        for (int x = 1; x <= m_width; ++x)
            picture(m_tiles, at(x, y) & PICTURE, x * TILE - 4, y * TILE + 39, TILE, TILE);
    }
}

// The window

Minesweeper::Minesweeper()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("winmine.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources)
        return;
    setWindowTitle(xpfiles::string(*m_resources, IDS_NAME));
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, IDI_MAIN, 32))));
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("501"));
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, IDM_MENU);
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { command(id); });
    }
    // The help is winmine.chm, which nothing here can show
    for (int id : {IDM_HELP, IDM_HELP_SEARCH, IDM_HELP_USING}) {
        if (QAction *help = m_actions.value(id))
            help->setEnabled(false);
    }
    for (int id : {IDM_BEGINNER, IDM_INTERMEDIATE, IDM_EXPERT, IDM_CUSTOM, IDM_MARKS, IDM_COLOR, IDM_SOUND}) {
        if (QAction *action = m_actions.value(id))
            action->setCheckable(true);
    }

    m_field = new MineField(*m_resources, this);
    setCentralWidget(m_field);
    m_field->resized = [this] { setFixedSize(m_field->width(), m_field->height() + menuBar()->sizeHint().height()); };
    m_field->won = [this](int seconds) { won(seconds); };
    // What XP keeps under HKCU\Software\Microsoft\winmine
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_field->setMarks(settings.value(QStringLiteral("Mark"), 1).toInt() != 0);
    m_field->setSound(settings.value(QStringLiteral("Sound"), 0).toInt() != 0);
    m_field->setColour(settings.value(QStringLiteral("Color"), 1).toInt() != 0);
    const int height = qBound(9, settings.value(QStringLiteral("Height"), 9).toInt(), 24);
    const int width = qBound(9, settings.value(QStringLiteral("Width"), 9).toInt(), 30);
    const int mines = qBound(10, settings.value(QStringLiteral("Mines"), 10).toInt(), qMin(999, (height - 1) * (width - 1)));
    m_field->setLevel(qBound(0, settings.value(QStringLiteral("Difficulty"), 0).toInt(), 3), width, height, mines);
    refresh();
}

void Minesweeper::refresh()
{
    const auto check = [this](int id, bool on) {
        if (QAction *action = m_actions.value(id))
            action->setChecked(on);
    };
    check(IDM_BEGINNER, m_field->level() == Beginner);
    check(IDM_INTERMEDIATE, m_field->level() == Intermediate);
    check(IDM_EXPERT, m_field->level() == Expert);
    check(IDM_CUSTOM, m_field->level() == Custom);
    check(IDM_MARKS, m_field->marks());
    check(IDM_COLOR, m_field->colour());
    check(IDM_SOUND, m_field->sound());
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Difficulty"), m_field->level());
    settings.setValue(QStringLiteral("Height"), m_field->rows());
    settings.setValue(QStringLiteral("Width"), m_field->columns());
    settings.setValue(QStringLiteral("Mines"), m_field->mines());
    settings.setValue(QStringLiteral("Mark"), int(m_field->marks()));
    settings.setValue(QStringLiteral("Color"), int(m_field->colour()));
    settings.setValue(QStringLiteral("Sound"), m_field->sound() ? 3 : 0);
}

void Minesweeper::command(int id)
{
    switch (id) {
    case IDM_NEW:
        m_field->newGame();
        break;
    case IDM_BEGINNER:
        m_field->setLevel(Beginner, 9, 9, 10);
        break;
    case IDM_INTERMEDIATE:
        m_field->setLevel(Intermediate, 16, 16, 40);
        break;
    case IDM_EXPERT:
        m_field->setLevel(Expert, 30, 16, 99);
        break;
    case IDM_CUSTOM:
        customField();
        break;
    case IDM_MARKS:
        m_field->setMarks(!m_field->marks());
        break;
    case IDM_COLOR:
        m_field->setColour(!m_field->colour());
        break;
    case IDM_SOUND:
        m_field->setSound(!m_field->sound());
        break;
    case IDM_BEST:
        bestTimes();
        break;
    case IDM_EXIT:
        close();
        return;
    case IDM_ABOUT:
        applets::about(xpfiles::string(*m_resources, IDS_ABOUT), xpfiles::icon(*m_resources, IDI_MAIN, 32),
                       xpfiles::string(*m_resources, IDS_AUTHORS));
        return;
    default:
        return;
    }
    refresh();
}

void Minesweeper::customField()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_CUSTOM, &dialog);
    dialog.setWindowTitle(page.caption);
    const auto edit = [&page](int id) { return qobject_cast<QLineEdit *>(page.controls.value(id)); };
    const auto fill = [&](int id, int value) {
        if (QLineEdit *field = edit(id))
            field->setText(QString::number(value));
    };
    fill(IDC_HEIGHT, m_field->rows());
    fill(IDC_WIDTH, m_field->columns());
    fill(IDC_MINES, m_field->mines());
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        const auto value = [&](int id) { return edit(id) ? edit(id)->text().toInt() : 0; };
        const int height = qBound(9, value(IDC_HEIGHT), 24);
        const int width = qBound(9, value(IDC_WIDTH), 30);
        const int mines = qBound(10, value(IDC_MINES), qMin(999, (height - 1) * (width - 1)));
        m_field->setLevel(Custom, width, height, mines);
    } else {
        // winmine leaves Custom checked and starts again all the same
        m_field->setLevel(Custom, m_field->columns(), m_field->rows(), m_field->mines());
    }
}

void Minesweeper::bestTimes()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_BEST, &dialog);
    dialog.setWindowTitle(page.caption);
    const auto show = [&] {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        for (int level = 0; level < 3; ++level) {
            if (auto *time = qobject_cast<QLabel *>(page.controls.value(IDC_TIME1 + 2 * level))) {
                time->setText(xpfiles::string(*m_resources, IDS_SECONDS)
                                  .replace(QLatin1String("%d"), QString::number(settings.value(QStringLiteral("Time%1").arg(level + 1), 999).toInt())));
            }
            if (auto *name = qobject_cast<QLabel *>(page.controls.value(IDC_NAME1 + 2 * level)))
                name->setText(settings.value(QStringLiteral("Name%1").arg(level + 1), xpfiles::string(*m_resources, IDS_ANONYMOUS)).toString());
        }
    };
    show();
    if (auto *reset = qobject_cast<QPushButton *>(page.controls.value(IDC_RESET))) {
        connect(reset, &QPushButton::clicked, &dialog, [&] {
            QSettings settings(settingsPath(), QSettings::IniFormat);
            for (int level = 1; level <= 3; ++level) {
                settings.setValue(QStringLiteral("Time%1").arg(level), 999);
                settings.setValue(QStringLiteral("Name%1").arg(level), xpfiles::string(*m_resources, IDS_ANONYMOUS));
            }
            settings.sync();
            show();
        });
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    dialog.exec();
}

// A level's fastest time: the name is asked for, then the times are shown
void Minesweeper::won(int seconds)
{
    const int level = m_field->level();
    if (level == Custom)
        return;
    const QString timeKey = QStringLiteral("Time%1").arg(level + 1), nameKey = QStringLiteral("Name%1").arg(level + 1);
    {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        if (seconds >= settings.value(timeKey, 999).toInt())
            return;
    }
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_NAME, &dialog);
    if (auto *text = qobject_cast<QLabel *>(page.controls.value(IDC_NAME_TEXT)))
        text->setText(xpfiles::string(*m_resources, IDS_FASTEST + level).replace(QLatin1Char('\r'), QLatin1Char('\n')));
    auto *name = qobject_cast<QLineEdit *>(page.controls.value(IDC_NAME));
    QString old;
    {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        old = settings.value(nameKey, xpfiles::string(*m_resources, IDS_ANONYMOUS)).toString();
    }
    if (name) {
        name->setMaxLength(32);
        name->setText(old);
        name->selectAll();
        name->setFocus();
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(IDC_NAME_OK))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    dialog.exec();
    {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        settings.setValue(timeKey, seconds);
        settings.setValue(nameKey, name ? name->text() : old);
    }
    bestTimes();
}

void Minesweeper::keyPressEvent(QKeyEvent *event)
{
    if (const int id = xpprogram::command(m_keys, event)) {
        if (QAction *action = m_actions.value(id); !action || action->isEnabled())
            command(id);
        return;
    }
    QMainWindow::keyPressEvent(event);
}
