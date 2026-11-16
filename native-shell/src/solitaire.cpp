// SPDX-License-Identifier: LGPL-2.1-or-later
#include "solitaire.h"

#include "applets.h"
#include "cards.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QAction>
#include <QBasicTimer>
#include <QButtonGroup>
#include <QDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {

// sol.exe's resources
enum {
    IDM_MENU = 1,
    IDM_DEAL = 1000, IDM_UNDO = 1001, IDM_DECK = 1002, IDM_OPTIONS = 1003, IDM_EXIT = 1004,
    IDM_HELP = 65506, IDM_HELP_SEARCH = 65507, IDM_HELP_USING = 65508, IDM_ABOUT = 2000,
    IDS_NAME = 100, IDS_SCORE = 101, IDS_TIME = 102, IDS_DEAL_AGAIN = 104, IDS_STOP = 106, IDS_BONUS = 107,
    IDS_AUTHOR = 108,
    IDD_BACKS = 101, IDD_OPTIONS = 103,
    IDC_DRAW_ONE = 300, IDC_DRAW_THREE = 301, IDC_STANDARD = 302, IDC_VEGAS = 303, IDC_NO_SCORE = 304,
    IDC_TIMED = 305, IDC_STATUS = 306, IDC_OUTLINE = 307, IDC_CUMULATIVE = 308, IDC_SCORE_LABEL = 310,
    IDI_MAIN = 500,
};

enum Scoring { Standard, Vegas, NoScore };
// What is scored: the ten seconds of a timed game, the deck turned over, a card sent up, a card
// from the deck put on a column, a card turned, a card taken back down, the deal, the game won
enum Event { Tick, Recycle, ToFoundation, DeckToColumn, Turned, FromFoundation, Dealt, Won };
const int STANDARD_SCORES[] = {-2, -20, 10, 5, 5, -15, 0, 0};
const int VEGAS_SCORES[] = {0, 0, 5, 0, 0, -5, -52, 0};

enum { Deck, Waste, Foundation, Column = Foundation + 4, Piles = Column + 7 };
const QColor TABLE(0, 128, 0);

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/sol.conf");
}

struct Card
{
    int id = 0;
    bool up = false;
    QPoint pos;
};

struct Pile
{
    QPoint origin;
    QList<Card> cards;
};

}

// The table

class SolBoard : public QWidget
{
public:
    explicit SolBoard(QWidget *parent)
        : QWidget(parent), m_card(cards::size())
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_back = cards::FirstBack + QRandomGenerator::global()->bounded(12);
        m_clock.start(250, this);
    }

    std::function<void()> changed;
    std::function<void()> won;

    QSize sizeHint() const override { return QSize(m_card.width() * 7 + (m_card.width() / 8 + 3) * 8, m_card.height() * 4); }

    int drawCount() const { return m_draw; }
    int scoring() const { return m_scoring; }
    bool timed() const { return m_timed; }
    bool outline() const { return m_outline; }
    bool cumulative() const { return m_cumulative; }
    int back() const { return m_back; }
    int score() const { return m_score; }
    int seconds() const { return m_ticks >> 2; }
    int bonus() const { return m_bonus; }
    bool canUndo() const { return m_undo.has_value(); }
    bool flying() const { return m_flying; }

    void setOptions(int draw, int scoring, bool timed, bool cumulative)
    {
        m_draw = draw;
        m_scoring = scoring;
        m_timed = timed;
        m_cumulative = cumulative;
    }
    void setOutline(bool on) { m_outline = on; }
    void setBack(int back)
    {
        m_back = back;
        update();
    }

    void deal()
    {
        stopFlying(false);
        QList<int> deck;
        for (int card = 0; card < 52; ++card)
            deck.append(card);
        for (int pass = 0; pass < 5; ++pass) {
            for (int i = 0; i < 52; ++i)
                deck.swapItemsAt(i, QRandomGenerator::global()->bounded(52));
        }
        for (Pile &pile : m_piles)
            pile.cards.clear();
        for (int id : std::as_const(deck))
            m_piles[Deck].cards.append({id, false, {}});
        for (int row = 0; row < 7; ++row) {
            for (int column = row; column < 7; ++column) {
                Card card = m_piles[Deck].cards.takeLast();
                card.up = column == row;
                m_piles[Column + column].cards.append(card);
            }
        }
        m_undo.reset();
        m_drag = {};
        m_passes = 0;
        m_ticks = 0;
        m_bonus = 0;
        m_started = false;
        m_over = false;
        if (!(m_scoring == Vegas && m_cumulative))
            m_score = 0;
        score(Dealt);
        place();
        notify();
    }

    void undo()
    {
        if (!m_undo || m_over)
            return;
        for (int i = 0; i < Piles; ++i)
            m_piles[i].cards = m_undo->cards[i];
        m_score = m_undo->score;
        m_passes = m_undo->passes;
        m_undo.reset();
        notify();
    }

    // The cards stop bouncing: a key or a button, or the last of them gone
    void stopFlying(bool tell = true)
    {
        if (!m_flying)
            return;
        m_flying = false;
        m_flight.stop();
        m_canvas = QPixmap();
        update();
        if (tell && won)
            QTimer::singleShot(0, this, [this] { won(); });
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const QList<QPoint> before = origins();
        place(false);
        for (int i = 0; i < Piles; ++i) {
            for (Card &card : m_piles[i].cards)
                card.pos += m_piles[i].origin - before[i];
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (m_flying) {
            p.fillRect(rect(), TABLE);
            p.drawPixmap(0, 0, m_canvas);
            return;
        }
        paintTable(p);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_flying) {
            stopFlying();
            return;
        }
        if (m_over)
            return;
        if (event->button() == Qt::RightButton) {
            sendAllUp();
            return;
        }
        if (event->button() != Qt::LeftButton)
            return;
        const QPoint at = event->position().toPoint();
        int index = -1;
        const int pile = cardAt(at, &index);
        if (pile < 0) {
            if (QRect(m_piles[Deck].origin, m_card).contains(at))
                turnDeck();
            return;
        }
        Pile &from = m_piles[pile];
        if (pile == Deck) {
            const bool one = event->modifiers().testFlags(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
            drawCards(one ? 1 : m_draw);
            return;
        }
        if (!from.cards[index].up) {
            if (pile >= Column && index == from.cards.size() - 1) {
                from.cards[index].up = true;
                m_undo.reset();
                m_started = true;
                score(Turned);
                notify();
            }
            return;
        }
        if (pile < Column && index != from.cards.size() - 1)
            return;
        m_drag = {pile, index, at, at};
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_drag.pile < 0)
            return;
        m_drag.now = event->position().toPoint();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_drag.pile < 0 || event->button() != Qt::LeftButton)
            return;
        const Drag drag = m_drag;
        m_drag = {};
        const QRect dropped(m_piles[drag.pile].cards[drag.index].pos + drag.now - drag.from, m_card);
        for (int to = Foundation; to < Piles; ++to) {
            if (to == drag.pile)
                continue;
            const Pile &pile = m_piles[to];
            const QRect place(pile.cards.isEmpty() ? pile.origin : pile.cards.last().pos, m_card);
            if (place.intersects(dropped) && accepts(to, drag.pile, drag.index)) {
                move(drag.pile, drag.index, to);
                return;
            }
        }
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_flying && !m_over) {
            int index = -1;
            const int pile = cardAt(event->position().toPoint(), &index);
            if ((pile == Waste || pile >= Column) && index == m_piles[pile].cards.size() - 1 && m_piles[pile].cards[index].up) {
                sendUp(pile);
                return;
            }
        }
        mousePressEvent(event);
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_flight.timerId()) {
            fly();
            return;
        }
        if (event->timerId() != m_clock.timerId() || !m_timed || !m_started || m_over || m_ticks >= 0x7ffe)
            return;
        ++m_ticks;
        if (m_ticks % 40 == 0)
            score(Tick);
        if (m_ticks % 4 == 0 && changed)
            changed();
    }

private:
    struct Drag
    {
        int pile = -1;
        int index = 0;
        QPoint from, now;
    };
    struct Undo
    {
        QList<Card> cards[Piles];
        int score = 0;
        int passes = 0;
    };

    QList<QPoint> origins() const
    {
        QList<QPoint> list;
        for (const Pile &pile : m_piles)
            list.append(pile.origin);
        return list;
    }

    // Where the piles are: the deck and the cards turned from it, the four suits above the last
    // four columns, the seven columns under them. LAY puts their cards back in their places too.
    void place(bool lay = true)
    {
        const int dx = m_card.width(), dy = m_card.height();
        const int margin = qMax(dx / 8 + 3, (width() - 7 * dx) / 8);
        // (MulDiv rounds: 5 pixels for a card of 96)
        const int top = (dy * 5 + 50) / 100;
        m_piles[Deck].origin = QPoint(margin, top);
        m_piles[Waste].origin = QPoint(margin * 2 + dx, top);
        for (int i = 0; i < 4; ++i)
            m_piles[Foundation + i].origin = QPoint(dx * 3 + margin * 4 + i * (dx + margin), top);
        for (int i = 0; i < 7; ++i)
            m_piles[Column + i].origin = QPoint(margin + i * (dx + margin), dy + top + top + 1);
        if (lay) {
            for (int i = 0; i < Piles; ++i)
                layPile(i);
        }
    }

    // A pile's cards from its first: the deck and the turned cards thicken by (2, 1) every ten
    // cards, a suit's every four; a column's hidden cards show 3 pixels each, the others 15
    void layPile(int index)
    {
        Pile &pile = m_piles[index];
        const int dy = m_card.height();
        QPoint pos = pile.origin;
        for (int i = 0; i < pile.cards.size(); ++i) {
            Card &card = pile.cards[i];
            card.pos = pos;
            if (index >= Column)
                pos += QPoint(0, card.up ? dy * 4 / 25 : dy / 25);
            else if (index >= Foundation ? i % 4 == 3 : i % 10 == 9)
                pos += QPoint(2, 1);
        }
    }

    int cardAt(const QPoint &at, int *index) const
    {
        for (int i = 0; i < Piles; ++i) {
            const Pile &pile = m_piles[i];
            for (int card = int(pile.cards.size()) - 1; card >= 0; --card) {
                if (QRect(pile.cards[card].pos, m_card).contains(at)) {
                    *index = card;
                    return i;
                }
            }
        }
        return -1;
    }

    void remember()
    {
        m_undo.emplace();
        for (int i = 0; i < Piles; ++i)
            m_undo->cards[i] = m_piles[i].cards;
        m_undo->score = m_score;
        m_undo->passes = m_passes;
    }

    void notify()
    {
        update();
        if (changed)
            changed();
    }

    void score(int event)
    {
        if (m_scoring == NoScore)
            return;
        if (m_scoring == Vegas) {
            m_score += VEGAS_SCORES[event];
            return;
        }
        int points = STANDARD_SCORES[event];
        if (event == Recycle) {
            // Draw One pays a hundred for every time through the deck after the first, Draw Three
            // twenty after the fourth
            if (m_draw == 1)
                points = m_passes >= 1 ? -100 : 0;
            else if (m_passes < 4)
                points = 0;
        } else if (event == Won) {
            points = m_timed && m_ticks >= 120 ? (20000 / (m_ticks >> 2)) * 35 : 0;
            m_bonus = points;
        }
        m_score = qMax(0, m_score + points);
    }

    // The deck's top cards, turned onto the pile beside it: the ones there close up, the new ones fan out
    void drawCards(int count)
    {
        Pile &deck = m_piles[Deck], &waste = m_piles[Waste];
        remember();
        m_started = true;
        layPile(Waste);
        QPoint pos = waste.cards.isEmpty() ? waste.origin : waste.cards.last().pos;
        for (int i = 0; i < count && !deck.cards.isEmpty(); ++i) {
            Card card = deck.cards.takeLast();
            card.up = true;
            card.pos = pos;
            pos += QPoint(m_card.width() / 5, 1);
            waste.cards.append(card);
        }
        notify();
    }

    bool lastPass() const { return m_scoring == Vegas && m_passes == m_draw - 1; }

    // The empty deck clicked: the turned cards go back under it, as often as the scoring lets them
    void turnDeck()
    {
        Pile &deck = m_piles[Deck], &waste = m_piles[Waste];
        if (waste.cards.isEmpty() || lastPass())
            return;
        remember();
        ++m_passes;
        while (!waste.cards.isEmpty()) {
            Card card = waste.cards.takeLast();
            card.up = false;
            deck.cards.append(card);
        }
        layPile(Deck);
        score(Recycle);
        notify();
    }

    // Whether pile TO takes pile FROM's cards from INDEX up
    bool accepts(int to, int from, int index) const
    {
        const Pile &pile = m_piles[to];
        const QList<Card> &moving = m_piles[from].cards;
        const int first = moving[index].id;
        if (to < Column) {
            if (index != moving.size() - 1)
                return false;
            if (pile.cards.isEmpty())
                return cards::rank(first) == 0;
            const int top = pile.cards.last().id;
            return cards::suit(first) == cards::suit(top) && cards::rank(first) == cards::rank(top) + 1;
        }
        if (pile.cards.isEmpty())
            return cards::rank(first) == 12;
        const Card &top = pile.cards.last();
        return top.up && cards::red(top.id) != cards::red(first) && cards::rank(first) == cards::rank(top.id) - 1;
    }

    void move(int from, int index, int to)
    {
        remember();
        m_started = true;
        m_piles[to].cards.append(m_piles[from].cards.mid(index));
        m_piles[from].cards.remove(index, m_piles[from].cards.size() - index);
        layPile(to);
        if (to < Column && from != to && (from == Waste || from >= Column))
            score(ToFoundation);
        else if (to >= Column && from == Waste)
            score(DeckToColumn);
        else if (to >= Column && from < Column)
            score(FromFoundation);
        notify();
        if (to < Column)
            checkWon();
    }

    // A pile's top card to the first suit that takes it
    bool sendUp(int from)
    {
        const Pile &pile = m_piles[from];
        if (pile.cards.isEmpty() || !pile.cards.last().up)
            return false;
        for (int to = Foundation; to < Column; ++to) {
            if (accepts(to, from, int(pile.cards.size()) - 1)) {
                move(from, int(pile.cards.size()) - 1, to);
                return true;
            }
        }
        return false;
    }

    // The right button: every card that can go up goes up
    void sendAllUp()
    {
        bool moved = true;
        while (moved && !m_over) {
            moved = sendUp(Waste);
            for (int from = Column; from < Piles && !m_over; ++from)
                moved = sendUp(from) || moved;
        }
    }

    void checkWon()
    {
        for (int i = Foundation; i < Column; ++i) {
            if (m_piles[i].cards.size() < 13)
                return;
        }
        m_over = true;
        m_undo.reset();
        score(Won);
        // The cards leave over what the table shows now
        m_canvas = QPixmap(size());
        {
            QPainter p(&m_canvas);
            paintTable(p);
        }
        m_flying = true;
        m_rank = 12;
        m_suit = -1;
        nextFlight();
        m_flight.start(5, Qt::PreciseTimer, this);
        notify();
    }

    // The kings first, a suit after the other; each card leaves at a speed of its own
    void nextFlight()
    {
        if (++m_suit == 4) {
            m_suit = 0;
            if (--m_rank < 0) {
                stopFlying();
                return;
            }
        }
        m_at = m_piles[Foundation + m_suit].cards[m_rank].pos;
        m_speed.setX(QRandomGenerator::global()->bounded(110) - 65);
        if (qAbs(m_speed.x()) < 15)
            m_speed.setX(-20);
        m_speed.setY(QRandomGenerator::global()->bounded(110) - 75);
    }

    // A step of the card in the air: it is drawn where it is and nothing wipes it, it falls three
    // more every step and comes back up from the bottom with eight tenths of its speed
    void fly()
    {
        if (!m_flying)
            return;
        if (m_at.x() <= -m_card.width() || m_at.x() >= width()) {
            nextFlight();
            if (!m_flying)
                return;
        }
        {
            QPainter p(&m_canvas);
            p.drawImage(m_at, cards::face(m_piles[Foundation + m_suit].cards[m_rank].id));
        }
        update(QRect(m_at, m_card));
        m_at += QPoint(m_speed.x() / 10, m_speed.y() / 10);
        m_speed.ry() += 3;
        if (m_at.y() > height() - m_card.height() && m_speed.y() > 0)
            m_speed.setY(m_speed.y() * -8 / 10);
    }

    QImage picture(const Card &card) const { return card.up ? cards::face(card.id) : cards::back(m_back); }

    void paintTable(QPainter &p)
    {
        p.fillRect(rect(), TABLE);
        const bool lifted = m_drag.pile >= 0 && !m_outline;
        for (int i = 0; i < Piles; ++i) {
            const Pile &pile = m_piles[i];
            const int count = lifted && m_drag.pile == i ? m_drag.index : int(pile.cards.size());
            if (count == 0) {
                if (i == Deck)
                    p.drawImage(pile.origin, lastPass() ? cards::deckX() : cards::deckO());
                else if (i >= Foundation && i < Column)
                    p.drawImage(pile.origin, cards::ghost(TABLE));
                continue;
            }
            for (int card = 0; card < count; ++card)
                p.drawImage(pile.cards[card].pos, picture(pile.cards[card]));
        }
        if (m_drag.pile < 0)
            return;
        const Pile &pile = m_piles[m_drag.pile];
        const QPoint by = m_drag.now - m_drag.from;
        if (m_outline) {
            // Outline dragging: the cards stay, their edges follow the pointer
            p.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
            p.setPen(Qt::white);
            for (int card = m_drag.index; card < pile.cards.size(); ++card)
                p.drawRect(QRect(pile.cards[card].pos + by, m_card).adjusted(0, 0, -1, -1));
            return;
        }
        for (int card = m_drag.index; card < pile.cards.size(); ++card)
            p.drawImage(pile.cards[card].pos + by, picture(pile.cards[card]));
    }

    QSize m_card;
    Pile m_piles[Piles];
    std::optional<Undo> m_undo;
    Drag m_drag;
    int m_draw = 3;
    int m_scoring = Standard;
    bool m_timed = true;
    bool m_outline = false;
    bool m_cumulative = false;
    int m_back = cards::FirstBack;
    int m_score = 0;
    int m_passes = 0;
    int m_ticks = 0;
    int m_bonus = 0;
    bool m_started = false;
    bool m_over = false;
    QBasicTimer m_clock;
    // The cards bouncing away
    bool m_flying = false;
    QBasicTimer m_flight;
    QPixmap m_canvas;
    int m_rank = 0, m_suit = 0;
    QPoint m_at, m_speed;
};

// The status bar: a menu command's words on the left, the score and the time on the right

class SolStatus : public QWidget
{
public:
    explicit SolStatus(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(18);
    }

    void set(const QString &left, const QString &scoreName, const QString &score, bool owing, const QString &time)
    {
        m_left = left;
        m_scoreName = scoreName;
        m_score = score;
        m_owing = owing;
        m_time = time;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.fillRect(0, 0, width(), 1, Qt::black);
        const QRect inside = rect().adjusted(4, 1, -4, 0);
        p.setPen(Qt::black);
        p.drawText(inside, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_left);
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        int right = inside.right() + 1;
        const auto word = [&](const QString &text, const QColor &colour) {
            p.setPen(colour);
            p.drawText(QRect(inside.left(), inside.top(), right - inside.left(), inside.height()),
                       Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, text);
            right -= p.fontMetrics().horizontalAdvance(text);
        };
        word(m_time, Qt::black);
        if (!m_scoreName.isEmpty()) {
            // What is owed in Vegas is red
            word(m_score, m_owing ? QColor(255, 0, 0) : QColor(Qt::black));
            word(m_scoreName, Qt::black);
        }
    }

private:
    QString m_left, m_scoreName, m_score, m_time;
    bool m_owing = false;
};

namespace {

// A back in Select Card Back: the picture squeezed into the button, a frame around the chosen one
class BackButton : public QWidget
{
public:
    BackButton(int back, QWidget *parent)
        : QWidget(parent), m_back(back)
    {
    }

    std::function<void()> chosen;
    std::function<void()> taken;

    void setChosen(bool on)
    {
        m_on = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Button));
        if (m_on) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawRect(rect().adjusted(1, 1, -1, -1));
        }
        p.drawImage(rect().adjusted(3, 3, -3, -3), cards::back(m_back));
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (chosen)
            chosen();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        if (taken)
            taken();
    }

private:
    int m_back;
    bool m_on = false;
};

}

// The window

Solitaire::Solitaire()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("sol.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources || !cards::load())
        return;
    setWindowTitle(xpfiles::string(*m_resources, IDS_NAME));
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, IDI_MAIN, 32))));
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("HIDDENACCEL"));
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, IDM_MENU);
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { command(id); });
        // The status bar says what the command under the pointer does
        connect(it.value(), &QAction::hovered, this, [this, id] {
            m_hint = xpfiles::string(*m_resources, id);
            refresh();
        });
    }
    const auto menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *menu : menus) {
        connect(menu, &QMenu::aboutToHide, this, [this] {
            m_hint.clear();
            refresh();
        });
    }
    // The help is sol.chm, which nothing here can show
    for (int id : {IDM_HELP, IDM_HELP_SEARCH, IDM_HELP_USING}) {
        if (QAction *help = m_actions.value(id))
            help->setEnabled(false);
    }

    auto *table = new QWidget(this);
    auto *layout = new QVBoxLayout(table);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_board = new SolBoard(table);
    m_status = new SolStatus(table);
    layout->addWidget(m_board, 1);
    layout->addWidget(m_status);
    setCentralWidget(table);
    m_board->changed = [this] { refresh(); };
    m_board->won = [this] { won(); };

    // What XP keeps under HKCU\Software\Microsoft\Solitaire: the status bar, the timed game, outline
    // dragging and Draw Three are a bit each, then two bits of scoring and one for the cumulative score
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    const int options = settings.value(QStringLiteral("Options"), 0x0b).toInt();
    m_status->setVisible(options & 1);
    m_board->setOptions(options & 8 ? 3 : 1, qBound(0, (options >> 4) & 3, int(NoScore)), options & 2, options & 0x40);
    m_board->setOutline(options & 4);
    const int back = settings.value(QStringLiteral("Back"), 0).toInt();
    if (back >= cards::FirstBack && back <= cards::LastBack)
        m_board->setBack(back);
    resize(m_board->sizeHint().width(), m_board->sizeHint().height() + 2 + menuBar()->sizeHint().height());
    m_board->deal();
}

void Solitaire::save()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Options"), int(m_status->isVisibleTo(this)) | int(m_board->timed()) << 1
                                                     | int(m_board->outline()) << 2 | (m_board->drawCount() == 3 ? 8 : 0)
                                                     | m_board->scoring() << 4 | int(m_board->cumulative()) << 6);
    settings.setValue(QStringLiteral("Back"), m_board->back());
}

void Solitaire::refresh()
{
    if (QAction *undo = m_actions.value(IDM_UNDO))
        undo->setEnabled(m_board->canUndo());
    QString left = m_hint;
    if (left.isEmpty() && m_board->flying()) {
        if (m_board->scoring() == Standard)
            left = xpfiles::string(*m_resources, IDS_BONUS) + QString::number(m_board->bonus()) + QStringLiteral("  ");
        left += xpfiles::string(*m_resources, IDS_STOP);
    }
    QString score;
    if (m_board->scoring() != NoScore) {
        if (m_board->score() < 0)
            score = QStringLiteral("-");
        if (m_board->scoring() == Vegas)
            score += QLatin1Char('$');
        score += QString::number(qAbs(m_board->score())) + QLatin1Char(' ');
    }
    m_status->set(left, m_board->scoring() != NoScore ? xpfiles::string(*m_resources, IDS_SCORE) : QString(), score,
                  m_board->score() < 0,
                  m_board->timed() ? xpfiles::string(*m_resources, IDS_TIME) + QString::number(m_board->seconds()) : QString());
}

void Solitaire::command(int id)
{
    switch (id) {
    case IDM_DEAL:
        m_board->deal();
        break;
    case IDM_UNDO:
        m_board->undo();
        break;
    case IDM_DECK:
        chooseBack();
        break;
    case IDM_OPTIONS:
        options();
        break;
    case IDM_EXIT:
        close();
        break;
    case IDM_ABOUT:
        applets::about(xpfiles::string(*m_resources, IDS_NAME), xpfiles::icon(*m_resources, IDI_MAIN, 32),
                       xpfiles::string(*m_resources, IDS_AUTHOR));
        break;
    default:
        break;
    }
}

void Solitaire::options()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_OPTIONS, &dialog);
    dialog.setWindowTitle(page.caption);
    const auto button = [&page](int id) { return qobject_cast<QAbstractButton *>(page.controls.value(id)); };
    const auto check = [&](int id, bool on) {
        if (QAbstractButton *box = button(id))
            box->setChecked(on);
    };
    const auto checked = [&](int id) { return button(id) && button(id)->isChecked(); };
    // Two sets of radio buttons in one dialog
    for (const auto &set : {QList<int>{IDC_DRAW_ONE, IDC_DRAW_THREE}, QList<int>{IDC_STANDARD, IDC_VEGAS, IDC_NO_SCORE}}) {
        auto *group = new QButtonGroup(&dialog);
        for (int id : set) {
            if (QAbstractButton *radio = button(id))
                group->addButton(radio);
        }
    }
    check(m_board->drawCount() == 1 ? IDC_DRAW_ONE : IDC_DRAW_THREE, true);
    check(IDC_STANDARD + m_board->scoring(), true);
    check(IDC_TIMED, m_board->timed());
    check(IDC_STATUS, m_status->isVisibleTo(this));
    check(IDC_OUTLINE, m_board->outline());
    check(IDC_CUMULATIVE, m_board->cumulative());
    // The cumulative score is Vegas's
    const auto vegas = [&] {
        for (int id : {IDC_CUMULATIVE, IDC_SCORE_LABEL}) {
            if (QWidget *control = page.controls.value(id))
                control->setEnabled(checked(IDC_VEGAS));
        }
    };
    vegas();
    for (int id : {IDC_STANDARD, IDC_VEGAS, IDC_NO_SCORE}) {
        if (QAbstractButton *radio = button(id))
            connect(radio, &QAbstractButton::toggled, &dialog, vegas);
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int draw = checked(IDC_DRAW_ONE) ? 1 : 3;
    const int scoring = checked(IDC_VEGAS) ? Vegas : checked(IDC_NO_SCORE) ? NoScore : Standard;
    const bool timed = checked(IDC_TIMED);
    // Another draw, another scoring or the clock set or stopped: the cards are dealt again
    const bool again = draw != m_board->drawCount() || scoring != m_board->scoring() || timed != m_board->timed();
    m_status->setVisible(checked(IDC_STATUS));
    m_board->setOutline(checked(IDC_OUTLINE));
    m_board->setOptions(draw, scoring, timed, checked(IDC_CUMULATIVE));
    save();
    if (again)
        m_board->deal();
    refresh();
}

void Solitaire::chooseBack()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_BACKS, &dialog);
    dialog.setWindowTitle(page.caption);
    int back = m_board->back();
    QHash<int, BackButton *> buttons;
    const auto choose = [&](int id) {
        back = id;
        for (auto it = buttons.cbegin(); it != buttons.cend(); ++it)
            it.value()->setChosen(it.key() == id);
    };
    for (int id = cards::FirstBack; id <= cards::LastBack; ++id) {
        QWidget *place = page.controls.value(id);
        if (!place)
            continue;
        auto *button = new BackButton(id, &dialog);
        button->setGeometry(place->geometry());
        place->hide();
        button->chosen = [&choose, id] { choose(id); };
        button->taken = [&dialog] { dialog.accept(); };
        buttons.insert(id, button);
    }
    choose(back);
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_board->setBack(back);
    save();
}

// The cards have stopped bouncing
void Solitaire::won()
{
    refresh();
    if (QMessageBox::question(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_DEAL_AGAIN))
        == QMessageBox::Yes)
        m_board->deal();
}

void Solitaire::keyPressEvent(QKeyEvent *event)
{
    if (m_board && m_board->flying()) {
        m_board->stopFlying();
        return;
    }
    if (const int id = xpprogram::command(m_keys, event)) {
        if (QAction *action = m_actions.value(id); !action || action->isEnabled())
            command(id);
        return;
    }
    QMainWindow::keyPressEvent(event);
}
