// SPDX-License-Identifier: LGPL-2.1-or-later
#include "hearts.h"

#include "applets.h"
#include "cards.h"
#include "sounds.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QAction>
#include <QBasicTimer>
#include <QButtonGroup>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <array>
#include <cmath>
#include <functional>

namespace {

// mshearts.exe's resources
enum {
    IDM_ABOUT = 101, IDM_EXIT = 103, IDM_HELP = 106, IDM_SOUND = 124, IDM_SCORE = 125, IDM_QUOTE = 127, IDM_OPTIONS = 128,
    IDS_FIRST_NAME = 303, IDS_WELCOME = 306, IDS_SELECT_PASS = 307, IDS_WAITING = 310, IDS_SELECT_CARD = 311,
    IDS_FOLLOW_SUIT = 312, IDS_NOT_BROKEN = 313, IDS_TWO_OF_CLUBS = 314, IDS_ACCEPT = 315, IDS_OK = 316,
    IDS_SCORE_SHEET = 318, IDS_GAME_OVER = 333, IDS_YOU_WIN = 334, IDS_AGAIN = 337, IDS_NO_POINTS = 338, IDS_SUIT = 401,
    IDS_PASS = 405, IDS_NAME = 413, IDS_TITLE = 57344,
    IDD_QUOTE = 501, IDD_SCORE = 502, IDD_NAME = 503, IDD_OPTIONS = 505,
    IDC_NAME = 201, IDC_SLOW = 215, IDC_FIRST_PLAYER = 218,
    IDW_HEARTS_BROKEN = 401, IDW_QUEEN = 402,
    IDI_MAIN = 31234,
};

enum { Seats = 4, HandSize = 13, CardW = 71, CardH = 96, Step = 15, Raise = 20, TwoOfClubs = 4, Queen = 47 };
enum State { Held, Chosen, Gone };
enum Phase { Idle, Passing, Accepting, Playing, Over };
const QColor TABLE(0, 127, 0);

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/mshearts.conf");
}

// The ace is the highest card
int value(int card) { return cards::rank(card) == 0 ? 13 : cards::rank(card); }
int points(int card) { return card == Queen ? 13 : cards::suit(card) == cards::Hearts ? 1 : 0; }

struct Slot
{
    int card = -1;
    int state = Gone;
};

struct Flight
{
    QImage picture;
    QPoint from, to;
};

}

// The table

class HeartsBoard : public QWidget
{
public:
    HeartsBoard(const w4w::PeResources &pe, QWidget *parent)
        : QWidget(parent), m_pe(pe)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_button = new QPushButton(this);
        m_button->hide();
        connect(m_button, &QPushButton::clicked, this, [this] { pressed(); });
        m_status = xpfiles::string(pe, IDS_WELCOME);
        std::fill(std::begin(m_trick), std::end(m_trick), -1);
    }

    std::function<void()> handDone;

    QString names[Seats];
    bool sound = false;
    // Pixels a card moves at each step: 5 slow, 15 normal, 60 fast
    int speed = 15;

    const QList<std::array<int, Seats>> &sheet() const { return m_sheet; }
    bool gameOver() const
    {
        for (int total : m_total) {
            if (total >= 100)
                return true;
        }
        return false;
    }
    // The player with the fewest points, -1 while the game goes on
    int winner() const
    {
        if (!gameOver())
            return -1;
        int best = 0;
        for (int seat = 1; seat < Seats; ++seat) {
            if (m_total[seat] < m_total[best])
                best = seat;
        }
        return best;
    }

    void newGame()
    {
        m_sheet.clear();
        std::fill(std::begin(m_total), std::end(m_total), 0);
        m_handNumber = 0;
        deal();
    }

    void deal()
    {
        QList<int> deck;
        for (int card = 0; card < 52; ++card)
            deck.append(card);
        for (int i = 51; i > 0; --i)
            deck.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
        for (int seat = 0; seat < Seats; ++seat) {
            for (int i = 0; i < HandSize; ++i)
                m_hand[seat][i] = {deck.takeLast(), Held};
            sortHand(seat);
        }
        std::fill(std::begin(m_trick), std::end(m_trick), -1);
        std::fill(std::begin(m_points), std::end(m_points), 0);
        m_played = 0;
        m_tricks = 0;
        m_broken = false;
        // Left, right, across, then a hand with no pass
        m_pass = m_handNumber % 4;
        if (m_pass == 3) {
            startPlay();
            return;
        }
        m_phase = Passing;
        m_button->setText(xpfiles::string(m_pe, IDS_PASS + m_pass));
        m_button->setEnabled(false);
        placeButton();
        m_button->show();
        say(IDS_SELECT_PASS, names[target(0)]);
    }

protected:
    void resizeEvent(QResizeEvent *) override { placeButton(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), TABLE);
        const QFontMetrics metrics = p.fontMetrics();
        for (int seat = 0; seat < Seats; ++seat) {
            for (int i = 0; i < HandSize; ++i) {
                const Slot &slot = m_hand[seat][i];
                if (slot.state != Gone)
                    p.drawImage(place(seat, i), seat == 0 ? cards::face(slot.card) : cards::back(cards::FirstBack));
            }
            if (!names[seat].isEmpty()) {
                const QPoint at = namePlace(seat, metrics.horizontalAdvance(names[seat]), metrics.height());
                p.setPen(Qt::black);
                p.drawText(at.x(), at.y() + metrics.ascent(), names[seat]);
            }
        }
        for (int i = 0; i < Seats; ++i) {
            const int seat = (m_leader + i) % Seats;
            if (m_trick[seat] != -1 && !m_collecting)
                p.drawImage(playPlace(seat), cards::face(m_trick[seat]));
        }
        for (const Flight &flight : std::as_const(m_flights))
            p.drawImage(flight.from + (flight.to - flight.from) * m_step / m_steps, flight.picture);
        // The line of words at the bottom
        const QRect bar(0, tableHeight(), width(), height() - tableHeight());
        p.fillRect(bar, palette().color(QPalette::Button));
        p.fillRect(bar.left(), bar.top(), bar.width(), 1, palette().color(QPalette::Dark));
        p.setPen(palette().color(QPalette::ButtonText));
        p.drawText(bar.adjusted(8, 1, -8, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_status);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_flights.isEmpty())
            return;
        const QPoint at = event->position().toPoint();
        int index = -1;
        for (int i = HandSize - 1; i >= 0 && index < 0; --i) {
            if (m_hand[0][i].state != Gone && QRect(place(0, i), QSize(CardW, CardH)).contains(at))
                index = i;
        }
        if (index < 0)
            return;
        if (m_phase == Passing) {
            Slot &slot = m_hand[0][index];
            if (slot.state == Chosen)
                slot.state = Held;
            else if (chosen(0) < 3)
                slot.state = Chosen;
            m_button->setEnabled(chosen(0) == 3);
            update();
        } else if (m_phase == Playing && m_turn == 0 && m_played < Seats && !m_waiting) {
            const int reason = refusal(0, index);
            if (reason == 0)
                play(0, index);
            else if (reason == IDS_FOLLOW_SUIT)
                say(reason, xpfiles::string(m_pe, IDS_SUIT + cards::suit(m_trick[m_leader])));
            else
                say(reason);
        }
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() != m_timer.timerId())
            return;
        if (++m_step < m_steps) {
            update();
            return;
        }
        m_timer.stop();
        m_flights.clear();
        const auto land = m_land;
        m_land = nullptr;
        update();
        if (land)
            land();
    }

private:
    // Where things are
    int tableHeight() const { return height() - (fontMetrics().height() + 11); }
    QPoint place(int seat, int index) const
    {
        const int w = width(), h = tableHeight();
        const int across = (w - CardW - 12 * Step) / 2, down = (h - CardH - 12 * Step) / 2;
        switch (seat) {
        case 0: return QPoint(across + index * Step, h - CardH - 3 - (m_hand[0][index].state == Chosen ? Raise : 0));
        case 1: return QPoint(9, down + index * Step);
        case 2: return QPoint(across + 12 * Step - index * Step, 3);
        default: return QPoint(w - CardW - 9, down + 12 * Step - index * Step);
        }
    }
    QPoint playPlace(int seat) const
    {
        const QPoint centre(width() / 2 - CardW / 2, tableHeight() / 2 - CardH / 2);
        static const QPoint offsets[] = {{-5, 30}, {-30, -5}, {5, -30}, {30, 5}};
        return centre + offsets[seat];
    }
    // Where a trick goes when SEAT takes it: off the table, on its side
    QPoint awayPlace(int seat) const
    {
        const QPoint at = playPlace(seat);
        switch (seat) {
        case 0: return QPoint(at.x(), tableHeight() + CardH);
        case 1: return QPoint(-CardW, at.y());
        case 2: return QPoint(at.x(), -CardH);
        default: return QPoint(width(), at.y());
        }
    }
    QPoint namePlace(int seat, int textWidth, int textHeight) const
    {
        const int w = width(), h = tableHeight();
        const int across = (w - CardW - 12 * Step) / 2, down = (h - CardH - 12 * Step) / 2;
        switch (seat) {
        case 0: return QPoint(across - textWidth - 3, h - textHeight - 3);
        case 1: return QPoint(11, down - textHeight);
        case 2: return QPoint(across + 12 * Step + 3 + CardW, 3);
        default: return QPoint(w - textWidth - 11, down + 12 * Step + CardH);
        }
    }
    void placeButton()
    {
        const QFontMetrics metrics = m_button->fontMetrics();
        const int w = metrics.averageCharWidth() * 15, h = metrics.height() * 14 / 8;
        m_button->setGeometry(width() / 2 - w / 2, tableHeight() - CardH - h - 40, w, h);
    }

    void say(int text, const QString &name = QString())
    {
        m_status = xpfiles::string(m_pe, text);
        m_status.replace(QStringLiteral("%s"), name);
        update();
    }

    void noise(int id) const
    {
        if (!sound)
            return;
        // The player takes files: the WAVE resource, written out once
        const QString path = QDir::tempPath() + QStringLiteral("/win4wine-mshearts-%1.wav").arg(id);
        if (!QFile::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return;
            file.write(m_pe.find(QStringLiteral("WAVE"), QString::number(id)));
        }
        sounds::playFile(path);
    }

    // A hand in order: clubs, diamonds, spades, hearts, each from the two up to the ace
    void sortHand(int seat)
    {
        static const int order[] = {0, 1, 3, 2};
        std::sort(std::begin(m_hand[seat]), std::end(m_hand[seat]), [](const Slot &a, const Slot &b) {
            if (cards::suit(a.card) != cards::suit(b.card))
                return order[cards::suit(a.card)] < order[cards::suit(b.card)];
            return value(a.card) < value(b.card);
        });
    }

    int chosen(int seat) const
    {
        int count = 0;
        for (const Slot &slot : m_hand[seat])
            count += slot.state == Chosen;
        return count;
    }
    int target(int seat) const { return (seat + (m_pass == 0 ? 1 : m_pass == 1 ? 3 : 2)) % Seats; }

    // The three cards a computer player passes, as mshearts.exe's do: the high spades, the high
    // hearts, the high cards of a suit short of low ones, a suit that can be emptied, then anything
    void choosePass(int seat)
    {
        static const int ranks[] = {0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
        static const int suits[] = {cards::Hearts, cards::Spades, cards::Diamonds, cards::Clubs};
        int count = 0;
        const auto holds = [&](int suit) {
            quint32 mask = 0;
            for (const Slot &slot : m_hand[seat]) {
                if (slot.state == Held && cards::suit(slot.card) == suit)
                    mask |= 1u << cards::rank(slot.card);
            }
            return mask;
        };
        const auto take = [&](quint32 mask, int suit) {
            for (int rank : ranks) {
                if (count >= 3 || !(mask & (1u << rank)))
                    continue;
                for (Slot &slot : m_hand[seat]) {
                    if (slot.state == Held && slot.card == cards::card(rank, suit)) {
                        slot.state = Chosen;
                        ++count;
                    }
                }
            }
        };
        take(0x1801, cards::Spades);
        take(0x1c01, cards::Hearts);
        for (int suit : suits) {
            if (suit != cards::Spades && qPopulationCount(holds(suit) & 0x7e) < 2)
                take(0x1f81, suit);
        }
        for (int suit : suits) {
            if (int(qPopulationCount(holds(suit))) <= 3 - count)
                take(0x1fff, suit);
        }
        for (int suit : suits)
            take(0x1fff, suit);
    }

    // The button: the three cards go, then the three that came are taken
    void pressed()
    {
        if (m_phase == Passing && chosen(0) == 3) {
            for (int seat = 1; seat < Seats; ++seat)
                choosePass(seat);
            QList<int> passed[Seats];
            for (int seat = 0; seat < Seats; ++seat) {
                for (const Slot &slot : m_hand[seat]) {
                    if (slot.state == Chosen)
                        passed[seat].append(slot.card);
                }
            }
            for (int seat = 0; seat < Seats; ++seat) {
                QList<int> &coming = passed[(seat + Seats - (target(0) - 0)) % Seats];
                QList<int> received = coming;
                for (Slot &slot : m_hand[seat]) {
                    if (slot.state == Chosen)
                        slot = {coming.takeFirst(), Held};
                }
                sortHand(seat);
                if (seat == 0) {
                    for (Slot &slot : m_hand[0]) {
                        if (received.contains(slot.card))
                            slot.state = Chosen;
                    }
                }
            }
            m_phase = Accepting;
            m_button->setText(xpfiles::string(m_pe, IDS_OK));
            say(IDS_ACCEPT);
        } else if (m_phase == Accepting) {
            for (Slot &slot : m_hand[0]) {
                if (slot.state == Chosen)
                    slot.state = Held;
            }
            startPlay();
        }
    }

    void startPlay()
    {
        m_button->hide();
        m_phase = Playing;
        for (int seat = 0; seat < Seats; ++seat) {
            for (const Slot &slot : m_hand[seat]) {
                if (slot.card == TwoOfClubs)
                    m_leader = seat;
            }
        }
        m_turn = m_leader;
        update();
        next();
    }

    bool holdsSuit(int seat, int suit) const
    {
        for (const Slot &slot : m_hand[seat]) {
            if (slot.state != Gone && cards::suit(slot.card) == suit)
                return true;
        }
        return false;
    }
    bool onlyPoints(int seat, bool heartsOnly) const
    {
        for (const Slot &slot : m_hand[seat]) {
            if (slot.state != Gone && (heartsOnly ? cards::suit(slot.card) != cards::Hearts : points(slot.card) == 0))
                return false;
        }
        return true;
    }

    // Why a card cannot be played now, as the words that say so; 0 when it can
    int refusal(int seat, int index) const
    {
        const int card = m_hand[seat][index].card;
        if (m_played == 0) {
            if (m_tricks == 0)
                return card == TwoOfClubs ? 0 : IDS_TWO_OF_CLUBS;
            if (cards::suit(card) == cards::Hearts && !m_broken && !onlyPoints(seat, true))
                return IDS_NOT_BROKEN;
            return 0;
        }
        const int led = cards::suit(m_trick[m_leader]);
        if (cards::suit(card) != led && holdsSuit(seat, led))
            return IDS_FOLLOW_SUIT;
        if (m_tricks == 0 && points(card) != 0 && !onlyPoints(seat, false))
            return IDS_NO_POINTS;
        return 0;
    }

    // What a computer player plays: under the trick when it can, its worst cards when it has none
    // of the suit, low when it leads
    int choose(int seat) const
    {
        QList<int> legal;
        for (int i = 0; i < HandSize; ++i) {
            if (m_hand[seat][i].state != Gone && refusal(seat, i) == 0)
                legal.append(i);
        }
        const auto card = [&](int index) { return m_hand[seat][index].card; };
        const auto best = [&](const std::function<int(int)> &worth) {
            int found = legal.first();
            for (int index : std::as_const(legal)) {
                if (worth(card(index)) > worth(card(found)))
                    found = index;
            }
            return found;
        };
        if (m_played == 0) {
            // Low, and not the high spades while the queen is about
            return best([&](int c) {
                const bool risky = cards::suit(c) == cards::Spades && value(c) >= 11 && !m_queenOut;
                return -value(c) - (risky ? 20 : 0);
            });
        }
        const int led = cards::suit(m_trick[m_leader]);
        if (cards::suit(card(legal.first())) != led) {
            return best([&](int c) {
                if (c == Queen)
                    return 100;
                if (cards::suit(c) == cards::Spades && value(c) > 11 && !m_queenOut)
                    return 80 + value(c);
                return (cards::suit(c) == cards::Hearts ? 40 : 0) + value(c);
            });
        }
        int top = 0, at = 0;
        for (int card : m_trick) {
            if (card != -1) {
                at += points(card);
                if (cards::suit(card) == led)
                    top = qMax(top, value(card));
            }
        }
        const bool last = m_played == Seats - 1;
        bool under = false;
        for (int index : std::as_const(legal))
            under = under || value(card(index)) < top;
        return best([&](int c) {
            if (c == Queen)
                return value(c) < top ? 100 : -100;
            if (value(c) < top)
                return 50 + value(c);
            if (under)
                return -value(c);
            // The trick is this player's: high when nothing can come of it, low otherwise
            return (last && at == 0) || m_tricks == 0 ? value(c) : -value(c);
        });
    }

    void next()
    {
        if (m_played == Seats) {
            m_waiting = true;
            QTimer::singleShot(700, this, [this] { collect(); });
            return;
        }
        if (m_turn == 0) {
            say(IDS_SELECT_CARD);
            return;
        }
        say(IDS_WAITING, names[m_turn]);
        m_waiting = true;
        QTimer::singleShot(250, this, [this] {
            m_waiting = false;
            play(m_turn, choose(m_turn));
        });
    }

    void fly(const QList<Flight> &flights, const std::function<void()> &land)
    {
        const QPoint by = flights.first().to - flights.first().from;
        m_steps = int(std::sqrt(double(by.x()) * by.x() + double(by.y()) * by.y())) / qMax(1, speed);
        m_steps += m_steps % 2;
        m_steps = qMax(1, m_steps);
        m_step = 0;
        m_flights = flights;
        m_land = land;
        m_timer.start(10, Qt::PreciseTimer, this);
        update();
    }

    void play(int seat, int index)
    {
        Slot &slot = m_hand[seat][index];
        const int card = slot.card;
        const QPoint from = place(seat, index);
        slot.state = Gone;
        if (cards::suit(card) == cards::Hearts && !m_broken) {
            m_broken = true;
            noise(IDW_HEARTS_BROKEN);
        }
        if (card == Queen) {
            m_queenOut = true;
            noise(IDW_QUEEN);
        }
        fly({{cards::face(card), from, playPlace(seat)}}, [this, seat, card] {
            m_trick[seat] = card;
            ++m_played;
            m_turn = (m_turn + 1) % Seats;
            update();
            next();
        });
    }

    // The trick goes to the highest card of the suit led, and that player leads the next
    void collect()
    {
        m_waiting = false;
        const int led = cards::suit(m_trick[m_leader]);
        int taker = m_leader;
        for (int seat = 0; seat < Seats; ++seat) {
            if (cards::suit(m_trick[seat]) == led && value(m_trick[seat]) > value(m_trick[taker]))
                taker = seat;
        }
        QList<Flight> flights;
        for (int i = 0; i < Seats; ++i) {
            const int seat = (m_leader + i) % Seats;
            m_points[taker] += points(m_trick[seat]);
            flights.append({cards::face(m_trick[seat]), playPlace(seat), awayPlace(taker) + playPlace(seat) - playPlace(taker)});
        }
        // The taker's own card sets the way: the others go along
        flights.swapItemsAt(0, (taker - m_leader + Seats) % Seats);
        m_collecting = true;
        fly(flights, [this, taker] {
            m_collecting = false;
            std::fill(std::begin(m_trick), std::end(m_trick), -1);
            m_played = 0;
            m_leader = m_turn = taker;
            if (++m_tricks < HandSize) {
                next();
                return;
            }
            // All 26 points to one player are 26 to each of the others instead
            for (int seat = 0; seat < Seats; ++seat) {
                if (m_points[seat] == 26) {
                    for (int other = 0; other < Seats; ++other)
                        m_points[other] = other == seat ? 0 : 26;
                    break;
                }
            }
            for (int seat = 0; seat < Seats; ++seat)
                m_total[seat] += m_points[seat];
            m_sheet.append({m_total[0], m_total[1], m_total[2], m_total[3]});
            ++m_handNumber;
            m_queenOut = false;
            m_phase = Over;
            update();
            if (handDone)
                QTimer::singleShot(0, this, [this] { handDone(); });
        });
    }

    const w4w::PeResources &m_pe;
    QPushButton *m_button = nullptr;
    QString m_status;
    Slot m_hand[Seats][HandSize];
    int m_trick[Seats];
    int m_phase = Idle;
    int m_pass = 0;
    int m_leader = 0, m_turn = 0, m_played = 0, m_tricks = 0;
    bool m_broken = false, m_queenOut = false, m_waiting = false, m_collecting = false;
    int m_points[Seats] = {}, m_total[Seats] = {};
    int m_handNumber = 0;
    QList<std::array<int, Seats>> m_sheet;
    // The cards on their way
    QList<Flight> m_flights;
    QBasicTimer m_timer;
    int m_step = 0, m_steps = 1;
    std::function<void()> m_land;
};

// The window

Hearts::Hearts()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("mshearts.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources || !cards::load())
        return;
    setWindowTitle(xpfiles::string(*m_resources, IDS_TITLE));
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, IDI_MAIN, 32))));
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("HEARTSACCEL"));
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, QStringLiteral("HEARTSMENU"));
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { command(id); });
    }
    // The help is mshearts.chm, which nothing here can show
    if (QAction *help = m_actions.value(IDM_HELP))
        help->setEnabled(false);

    m_board = new HeartsBoard(*m_resources, this);
    setCentralWidget(m_board);
    m_board->handDone = [this] { scoreSheet(true); };
    // What XP keeps under HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\Hearts
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_board->sound = settings.value(QStringLiteral("sound"), 0).toInt();
    const int speed = settings.value(QStringLiteral("speed"), IDC_SLOW + 1).toInt();
    m_board->speed = speed == IDC_SLOW ? 5 : speed == IDC_SLOW + 2 ? 60 : 15;
    if (QAction *soundItem = m_actions.value(IDM_SOUND)) {
        soundItem->setCheckable(true);
        soundItem->setChecked(m_board->sound);
    }
    loadNames();
    // A window of 540 by 480
    setFixedSize(532, 446);
    QTimer::singleShot(0, this, [this] {
        if (askName())
            m_board->newGame();
        else
            close();
    });
}

void Hearts::loadNames()
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_board->names[0] = settings.value(QStringLiteral("name")).toString();
    for (int i = 0; i < 3; ++i) {
        const QString name = settings.value(QStringLiteral("p%1name").arg(i + 1)).toString();
        m_board->names[i + 1] = name.isEmpty() ? xpfiles::string(*m_resources, IDS_FIRST_NAME + i) : name;
    }
    m_board->update();
}

// "What is your name?": false for Quit
bool Hearts::askName()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_NAME, &dialog);
    dialog.setWindowTitle(page.caption);
    auto *edit = qobject_cast<QLineEdit *>(page.controls.value(IDC_NAME));
    if (edit) {
        edit->setText(m_board->names[0].isEmpty() ? qEnvironmentVariable("USER") : m_board->names[0]);
        edit->setMaxLength(14);
        edit->selectAll();
        edit->setFocus();
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *quit = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(quit, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (edit && !edit->text().trimmed().isEmpty()) {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("name"), edit->text().trimmed());
    }
    loadNames();
    return true;
}

void Hearts::command(int id)
{
    switch (id) {
    case IDM_OPTIONS:
        options();
        break;
    case IDM_SOUND: {
        m_board->sound = !m_board->sound;
        if (QAction *soundItem = m_actions.value(IDM_SOUND))
            soundItem->setChecked(m_board->sound);
        QSettings settings(settingsPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("sound"), int(m_board->sound));
        break;
    }
    case IDM_SCORE:
        scoreSheet(false);
        break;
    case IDM_EXIT:
        close();
        break;
    case IDM_QUOTE:
        quote();
        break;
    case IDM_ABOUT:
        applets::about(xpfiles::string(*m_resources, IDS_NAME), xpfiles::icon(*m_resources, IDI_MAIN, 32));
        break;
    default:
        break;
    }
}

void Hearts::options()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_OPTIONS, &dialog);
    dialog.setWindowTitle(page.caption);
    QSettings settings(settingsPath(), QSettings::IniFormat);
    auto *group = new QButtonGroup(&dialog);
    const int speed = settings.value(QStringLiteral("speed"), IDC_SLOW + 1).toInt();
    for (int i = 0; i < 3; ++i) {
        if (auto *radio = qobject_cast<QAbstractButton *>(page.controls.value(IDC_SLOW + i))) {
            group->addButton(radio, IDC_SLOW + i);
            radio->setChecked(speed == IDC_SLOW + i);
        }
        if (auto *edit = qobject_cast<QLineEdit *>(page.controls.value(IDC_FIRST_PLAYER + i))) {
            edit->setText(m_board->names[i + 1]);
            edit->setMaxLength(14);
        }
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int chosen = group->checkedId() < IDC_SLOW ? IDC_SLOW + 1 : group->checkedId();
    settings.setValue(QStringLiteral("speed"), chosen);
    m_board->speed = chosen == IDC_SLOW ? 5 : chosen == IDC_SLOW + 2 ? 60 : 15;
    for (int i = 0; i < 3; ++i) {
        if (auto *edit = qobject_cast<QLineEdit *>(page.controls.value(IDC_FIRST_PLAYER + i)))
            settings.setValue(QStringLiteral("p%1name").arg(i + 1), edit->text().trimmed());
    }
    settings.sync();
    loadNames();
}

// The score sheet: a column for each player, a line for each hand. After a hand it says who won
// when a player has reached a hundred, and the next hand is dealt when it closes
void Hearts::scoreSheet(bool handOver)
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_SCORE, &dialog);
    const int winner = m_board->winner();
    dialog.setWindowTitle(xpfiles::string(*m_resources, winner < 0 ? IDS_SCORE_SHEET : winner == 0 ? IDS_YOU_WIN : IDS_GAME_OVER));
    QString table = QStringLiteral("<table cellspacing='0' cellpadding='2' width='100%'><tr>");
    for (int seat = 0; seat < 4; ++seat) {
        table += QStringLiteral("<th align='center' width='25%'>%1%2%3</th>")
                     .arg(seat == winner ? QStringLiteral("<font color='#0000ff'>") : QString(),
                          m_board->names[seat].toHtmlEscaped(), seat == winner ? QStringLiteral("</font>") : QString());
    }
    table += QStringLiteral("</tr>");
    for (const auto &line : m_board->sheet()) {
        table += QStringLiteral("<tr>");
        for (int score : line)
            table += QStringLiteral("<td align='center'>%1</td>").arg(score);
        table += QStringLiteral("</tr>");
    }
    table += QStringLiteral("</table>");
    auto *sheet = new QLabel(table, &dialog);
    sheet->setTextFormat(Qt::RichText);
    sheet->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    QWidget *okButton = page.controls.value(1);
    sheet->setGeometry(8, 8, (okButton ? okButton->x() : dialog.width()) - 16, dialog.height() - 16);
    if (auto *ok = qobject_cast<QPushButton *>(okButton)) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    dialog.exec();
    if (!handOver)
        return;
    if (winner < 0) {
        m_board->deal();
        return;
    }
    if (QMessageBox::question(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_AGAIN))
        == QMessageBox::Yes)
        m_board->newGame();
    else
        close();
}

void Hearts::quote()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_QUOTE, &dialog);
    dialog.setWindowTitle(page.caption);
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    dialog.exec();
}

void Hearts::keyPressEvent(QKeyEvent *event)
{
    if (const int id = xpprogram::command(m_keys, event)) {
        if (QAction *action = m_actions.value(id); !action || action->isEnabled())
            command(id);
        return;
    }
    QMainWindow::keyPressEvent(event);
}
