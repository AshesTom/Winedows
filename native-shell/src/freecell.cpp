// SPDX-License-Identifier: LGPL-2.1-or-later
#include "freecell.h"

#include "applets.h"
#include "cards.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBasicTimer>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <cmath>
#include <functional>

namespace {

// freecell.exe's resources
enum {
    IDM_ABOUT = 101, IDM_NEW = 102, IDM_SELECT = 103, IDM_STATISTICS = 105, IDM_HELP = 106, IDM_RESTART = 107,
    IDM_EXIT = 108, IDM_OPTIONS = 109, IDM_HELP_SEARCH = 110, IDM_HELP_USING = 111, IDM_CHEAT = 114, IDM_UNDO = 115,
    IDS_NAME = 301, IDS_AUTHOR = 302, IDS_GAME = 303, IDS_RESIGN = 305, IDS_ILLEGAL = 306, IDS_TOO_MANY = 307,
    IDS_LEFT = 308, IDS_CLEAR = 309, IDS_ONE_WIN = 310, IDS_ONE_LOSS = 311, IDS_WINS = 312, IDS_LOSSES = 313,
    IDS_SESSION = 319, IDS_TOTAL = 320, IDS_STREAKS = 321,
    IDD_OPTIONS = 505,
    IDC_MOVE_COLUMN = 201, IDC_MOVE_CARD = 202, IDC_NUMBER = 203, IDC_SAME_GAME = 205, IDC_CLEAR = 207,
    IDC_SELECT_GAME = 208, IDC_MESSAGES = 209, IDC_QUICK = 210, IDC_DOUBLE_CLICK = 211,
    IDC_SESSION = 212, IDC_TOTAL = 213, IDC_STREAKS = 214,
    IDI_MAIN = 601,
};

// Pile 0 is the top row (four free cells, then the four home cells), piles 1 to 8 the columns
enum { Top = 0, Piles = 9, Places = 21, Cells = 4, LastGame = 1000000 };
const QColor TABLE(0, 127, 0);
const QColor BRIGHT(0, 255, 0);

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/freecell.conf");
}

// wsprintf, for the few forms freecell.exe's words use: %u, %d and %s, and a lone % that goes away
QString format(const QString &form, const QVariantList &values)
{
    QString out;
    int next = 0;
    for (int i = 0; i < form.size(); ++i) {
        if (form[i] != QLatin1Char('%') || i + 1 >= form.size()) {
            out += form[i];
            continue;
        }
        ++i;
        while (i < form.size() - 1 && (form[i] == QLatin1Char(' ') || form[i] == QLatin1Char('-') || form[i] == QLatin1Char('#')
                                       || form[i].isDigit()))
            ++i;
        const QChar type = form[i];
        if (type == QLatin1Char('u') || type == QLatin1Char('d') || type == QLatin1Char('s'))
            out += values.value(next++).toString();
        else
            out += type;
    }
    return out;
}

// The C library's rand() of freecell.exe: what makes game 617 the same game everywhere
struct Random
{
    quint32 seed = 0;
    int next()
    {
        seed = seed * 214013 + 2531011;
        return (seed >> 16) & 0x7fff;
    }
};

int randomGame()
{
    Random random{quint32(QDateTime::currentSecsSinceEpoch())};
    random.next();
    random.next();
    int game = 0;
    while (game == 0 || game > LastGame)
        game = random.next();
    return game;
}

struct Move
{
    int fromPile = 0, fromPos = 0, toPile = 0, toPos = 0;
};

struct State
{
    int card[Piles][Places];
    // The rank each suit's home has reached and the home cell the suit took, -1 for none
    int homeRank[4];
    int homeSlot[4];
    int left = 0;

    State()
    {
        for (auto &pile : card)
            std::fill(std::begin(pile), std::end(pile), -1);
        std::fill(std::begin(homeRank), std::end(homeRank), -1);
        std::fill(std::begin(homeSlot), std::end(homeSlot), -1);
    }

    // The place of a column's last card, -1 for an empty column
    int bottom(int pile) const
    {
        int pos = Places - 1;
        while (pos >= 0 && card[pile][pos] == -1)
            --pos;
        return pos;
    }
    int freeCells() const
    {
        int count = 0;
        for (int i = 0; i < Cells; ++i)
            count += card[Top][i] == -1;
        return count;
    }
    int emptyColumns() const
    {
        int count = 0;
        for (int pile = 1; pile < Piles; ++pile)
            count += card[pile][0] == -1;
        return count;
    }
};

// Whether CARD goes on ONTO in a column: a rank under it, of the other colour
bool fits(int card, int onto)
{
    return cards::rank(onto) - cards::rank(card) == 1 && cards::red(card) != cards::red(onto);
}

// How many of column FROM's last cards have to move for them to go on column TO (0: none does);
// onto an empty column, how many of them are in order
int needed(const State &s, int from, int to)
{
    if (from == to)
        return 1;
    int pos = s.bottom(from);
    int count = 1;
    if (s.card[to][0] == -1) {
        for (; pos > 0 && fits(s.card[from][pos], s.card[from][pos - 1]); --pos)
            ++count;
        return count;
    }
    const int onto = s.card[to][s.bottom(to)];
    while (!fits(s.card[from][pos], onto)) {
        if (pos == 0 || !fits(s.card[from][pos], s.card[from][pos - 1]))
            return 0;
        --pos;
        ++count;
    }
    return count;
}

// Where a move really starts and ends in S; false for one that moves nothing
bool resolve(const State &s, Move &m)
{
    if (m.fromPile == Top) {
        if (m.fromPos >= Cells)
            return false;
    } else {
        m.fromPos = s.bottom(m.fromPile);
        if (m.fromPos < 0 || m.fromPile == m.toPile)
            return false;
    }
    if (s.card[m.fromPile][m.fromPos] == -1)
        return false;
    if (m.toPile != Top)
        m.toPos = s.bottom(m.toPile) + 1;
    return m.toPos < Places;
}

void commit(State &s, const Move &m)
{
    const int card = s.card[m.fromPile][m.fromPos];
    s.card[m.fromPile][m.fromPos] = -1;
    s.card[m.toPile][m.toPos] = card;
    if (m.toPile == Top && m.toPos >= Cells) {
        s.homeRank[cards::suit(card)] = cards::rank(card);
        if (s.homeSlot[cards::suit(card)] == -1)
            s.homeSlot[cards::suit(card)] = m.toPos;
        --s.left;
    }
}

}

// The table

class FreeBoard : public QWidget
{
public:
    FreeBoard(const w4w::PeResources &pe, QWidget *parent)
        : QWidget(parent), m_dx(cards::size().width()), m_dy(cards::size().height())
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMouseTracking(true);
        bool partial = false;
        m_king = w4w::decodeDib(pe.find(QStringLiteral("2"), QStringLiteral("KINGBITMAP")), &partial);
        m_kingLeft = w4w::decodeDib(pe.find(QStringLiteral("2"), QStringLiteral("KINGLEFT")), &partial);
        m_kingSmile = w4w::decodeDib(pe.find(QStringLiteral("2"), QStringLiteral("KINGSMILE")), &partial);
    }

    std::function<void()> changed;
    std::function<void()> won;
    std::function<void()> lost;
    std::function<int()> moveColumn;
    std::function<void(int, int)> illegal;

    bool messages = true;
    bool quick = false;
    bool doubleClick = true;
    // What Ctrl+Shift+F10 asks for: 1 the game is lost at the next move, 2 every card goes home
    int cheat = 0;

    int game() const { return m_game; }
    bool inProgress() const { return m_inProgress; }
    int left() const { return m_now.left; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool busy() const { return !m_queue.isEmpty() || m_flying; }

    // The game is over for the statistics, whatever the table shows
    void stop()
    {
        m_inProgress = false;
    }
    void forget()
    {
        m_game = 0;
        m_inProgress = false;
    }

    void deal(int game)
    {
        m_flight.stop();
        m_flying = false;
        m_queue.clear();
        m_undo.clear();
        m_selected = false;
        m_smile = false;
        m_looksLeft = false;
        m_peek = false;
        cheat = 0;
        m_game = game;
        m_inProgress = true;
        m_now = State();
        m_now.left = 52;
        if (game == -1) {
            // The two games nobody wins
            for (int row = 0; row < 7; ++row) {
                for (int i = 0; i < 4; ++i)
                    m_now.card[1 + i][row] = row * 8 + i;
            }
            for (int row = 0; row < 6; ++row) {
                for (int i = 0; i < 4; ++i)
                    m_now.card[5 + i][row] = 44 - row * 8 + i;
            }
        } else if (game == -2) {
            for (int i = 0; i < 4; ++i)
                m_now.card[1 + i][0] = 3 - i;
            int card = 51;
            for (int row = 0; row < 6; ++row) {
                for (int i = 0; i < 4; ++i)
                    m_now.card[1 + i][1 + row] = card--;
            }
            for (int row = 0; row < 6; ++row) {
                for (int i = 0; i < 4; ++i)
                    m_now.card[5 + i][row] = card--;
            }
        } else {
            Random random{quint32(game)};
            int deck[52];
            for (int i = 0; i < 52; ++i)
                deck[i] = i;
            int count = 52;
            for (int i = 0; i < 52; ++i) {
                const int pick = random.next() % count;
                m_now.card[1 + i % 8][i / 8] = deck[pick];
                deck[pick] = deck[--count];
            }
        }
        notify();
    }

    void undo()
    {
        if (m_undo.isEmpty() || busy())
            return;
        m_selected = false;
        m_undoing = true;
        m_queue = m_undo;
        m_undo.clear();
        notify();
        play();
    }

    // The keys: 1 to 8 are the columns, 0 the free cells and 9 the home cells
    void digit(int digit)
    {
        if (busy() || m_game == 0)
            return;
        int pile = Top, pos = 0;
        if (digit == 0) {
            if (!m_selected) {
                while (pos < Cells && m_now.card[Top][pos] == -1)
                    ++pos;
            } else if (m_selPile != Top) {
                while (pos < Cells && m_now.card[Top][pos] != -1)
                    ++pos;
                if (pos == Cells)
                    pos = 0;
            } else {
                // From a free cell's card to the next one
                pos = m_selPos + 1;
                click(place(Top, m_selPos));
                while (pos < Cells && m_now.card[Top][pos] == -1)
                    ++pos;
            }
            if (pos == Cells)
                return;
        } else if (digit == 9) {
            if (!m_selected)
                return;
            pos = m_now.homeSlot[cards::suit(m_now.card[m_selPile][m_selPos])];
            if (pos == -1)
                pos = Cells;
        } else {
            pile = digit;
        }
        click(place(pile, pos));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const State &s = m_flying ? m_mid : m_now;
        p.fillRect(rect(), TABLE);
        for (int pos = 0; pos < 2 * Cells; ++pos) {
            const QPoint at = place(Top, pos);
            const int card = s.card[Top][pos];
            if (card != -1) {
                p.drawImage(at, picture(Top, pos, card));
                continue;
            }
            // An empty cell is sunk into the table
            p.fillRect(at.x(), at.y(), 1, m_dy - 1, Qt::black);
            p.fillRect(at.x(), at.y(), m_dx - 1, 1, Qt::black);
            p.fillRect(at.x() + m_dx - 1, at.y() + 1, 1, m_dy - 1, BRIGHT);
            p.fillRect(at.x() + 1, at.y() + m_dy - 1, m_dx - 1, 1, BRIGHT);
        }
        if (!m_smile) {
            // The king between the cells looks at the side the last card went to
            const int x = (width() - 32) / 2, y = (m_dy - 32) / 3;
            p.fillRect(x - 3, y - 3, 37, 1, BRIGHT);
            p.fillRect(x - 3, y - 3, 1, 37, BRIGHT);
            p.fillRect(x - 2, y + 34, 37, 1, Qt::black);
            p.fillRect(x + 34, y - 2, 1, 37, Qt::black);
            p.drawImage(x, y, m_looksLeft ? m_kingLeft : m_king);
        }
        for (int pile = 1; pile < Piles; ++pile) {
            for (int pos = 0; pos < Places && s.card[pile][pos] != -1; ++pos)
                p.drawImage(place(pile, pos), picture(pile, pos, s.card[pile][pos]));
        }
        if (m_peek && !m_flying && m_now.card[m_peekPile][m_peekPos] != -1)
            p.drawImage(place(m_peekPile, m_peekPos), cards::face(m_now.card[m_peekPile][m_peekPos]));
        if (m_smile) {
            p.drawImage(QRect(10, m_dy + 10, 320, 320), m_kingSmile);
        }
        if (m_flying)
            p.drawImage(m_from + (m_to - m_from) * m_step / m_steps, cards::face(m_card));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        if (event->button() == Qt::RightButton) {
            // The right button shows the whole of a card the others cover
            int pile = -1, pos = 0;
            if (!busy() && m_game != 0 && hit(at, &pile, &pos) && pile != Top && pos < Places - 1
                && m_now.card[pile][pos + 1] != -1) {
                m_peek = true;
                m_peekPile = pile;
                m_peekPos = pos;
                update();
            }
            return;
        }
        if (event->button() == Qt::LeftButton)
            click(at);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton && m_peek) {
            m_peek = false;
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            mousePressEvent(event);
            return;
        }
        const QPoint at = event->position().toPoint();
        if (!busy() && m_game != 0 && doubleClick && m_selected && m_selPile != Top) {
            int pile = -1, pos = 0;
            hit(at, &pile, &pos);
            if (pile == m_selPile && toFreeCell())
                return;
        }
        click(at);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        int pile = -1, pos = 0;
        if (hit(event->position().toPoint(), &pile, &pos) && pile == Top && !m_smile && m_looksLeft != (pos < Cells)) {
            m_looksLeft = pos < Cells;
            update();
        }
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() != m_flight.timerId())
            return;
        if (++m_step >= m_steps) {
            m_flight.stop();
            land();
            play();
        } else {
            update();
        }
    }

private:
    // Where a place of the table is
    int columnX(int pile) const
    {
        const int step = (width() - 8 * m_dx) / 9;
        return step + (pile - 1) * (width() - step) / 8;
    }
    QPoint place(int pile, int pos) const
    {
        if (pile == Top)
            return QPoint(pos < Cells ? pos * m_dx : width() - (2 * Cells - pos) * m_dx, 0);
        return QPoint(columnX(pile), m_dy + 10 + pos * overlap());
    }
    int overlap() const { return m_dy * 9 / 46; }

    // The place under a point. False when no card or cell is there, though PILE is still the
    // column the point is in: that is enough to put a card down
    bool hit(const QPoint &at, int *pile, int *pos) const
    {
        *pile = -1;
        if (at.x() < 0 || at.y() < 0)
            return false;
        if (at.y() < m_dy) {
            const int home = width() - Cells * m_dx;
            if (at.x() < Cells * m_dx)
                *pos = at.x() / m_dx;
            else if (at.x() >= home && at.x() - home < Cells * m_dx)
                *pos = (at.x() - home) / m_dx + Cells;
            else
                return false;
            *pile = Top;
            return true;
        }
        const int top = m_dy + 10;
        if (at.y() < top || at.x() < columnX(1))
            return false;
        const int column = (at.x() - columnX(1)) / (columnX(2) - columnX(1)) + 1;
        if (column >= Piles)
            return false;
        *pile = column;
        if (at.x() > columnX(column) + m_dx || m_now.card[column][0] == -1)
            return false;
        const int down = at.y() - top;
        *pos = qMin(down / overlap(), Places - 1);
        if (*pos < Places - 1 && m_now.card[column][*pos + 1] != -1)
            return true;
        *pos = qMin(*pos, m_now.bottom(column));
        return down <= *pos * overlap() + m_dy;
    }

    QImage picture(int pile, int pos, int card) const
    {
        return m_selected && !m_flying && pile == m_selPile && pos == m_selPos ? cards::inverted(card) : cards::face(card);
    }

    void notify()
    {
        update();
        if (changed)
            changed();
    }

    void click(const QPoint &at)
    {
        if (busy() || m_game == 0)
            return;
        if (m_selected)
            putDown(at);
        else
            pick(at);
    }

    // The first click: a free cell's card or a column's last one is picked
    void pick(const QPoint &at)
    {
        m_undo.clear();
        int pile = -1, pos = 0;
        if (hit(at, &pile, &pos)) {
            if (pile == Top) {
                if (pos >= Cells || m_now.card[Top][pos] == -1)
                    pile = -1;
            } else {
                pos = m_now.bottom(pile);
            }
            if (pile != -1) {
                m_selected = true;
                m_selPile = pile;
                m_selPos = pos;
                if (pile == Top)
                    m_looksLeft = true;
            }
        }
        notify();
    }

    // The second click: where the picked card, or the cards over it that go with it, should go
    void putDown(const QPoint &at)
    {
        int pile = -1, pos = 0;
        hit(at, &pile, &pos);
        if (pile == -1 || (pile == Top && pos >= 2 * Cells)) {
            pile = m_selPile;
            pos = m_selPos;
        } else if (pile != Top) {
            pos = qMax(0, m_now.bottom(pile));
        }
        m_plan = m_now;
        m_queue.clear();
        if (m_selPile == Top || pile == Top || m_now.card[pile][0] == -1) {
            int choice = 0;
            if (allowed(pile, pos, &choice)) {
                if (choice == 1)
                    moveRun(m_selPile, pile);
                else
                    record(m_selPile, m_selPos, pile, pos);
            } else if (choice == -1) {
                record(m_selPile, m_selPos, m_selPile, m_selPos);
            } else if (!messages) {
                return;
            } else {
                refuse(0, 0);
            }
        } else {
            const int count = needed(m_now, m_selPile, pile);
            const int most = (m_now.freeCells() + 1) * (m_now.emptyColumns() + 1);
            if (count != 0 && count <= most) {
                moveColumnTo(m_selPile, pile);
            } else {
                if (!messages)
                    return;
                refuse(count, most);
            }
        }
        sendHome();
        m_selected = false;
        m_undoing = false;
        m_all = m_queue;
        play();
    }

    void refuse(int count, int most)
    {
        if (illegal)
            illegal(count, most);
        record(m_selPile, m_selPos, m_selPile, m_selPos);
    }

    // Whether the picked card may go to a cell, or to a column when one of the two is a cell or
    // the column is empty. CHOICE is what Move to Empty Column answered: 1 the column, -1 Cancel
    bool allowed(int pile, int pos, int *choice)
    {
        if (m_selPile == Top && pile == Top && m_selPos == pos)
            return true;
        const int card = m_now.card[m_selPile][m_selPos];
        const int onto = m_now.card[pile][pos];
        if (pile == Top) {
            if (pos < Cells)
                return onto == -1;
            if (cards::rank(card) == 0 && onto == -1)
                return true;
            return onto != -1 && cards::suit(card) == cards::suit(onto) && cards::rank(card) == cards::rank(onto) + 1;
        }
        if (m_now.card[pile][0] != -1)
            return fits(card, onto);
        if (m_selPile == Top)
            return true;
        int count = needed(m_now, m_selPile, pile);
        if (m_now.freeCells() == 0 && count > 1)
            count = 1;
        if (count > 1) {
            *choice = moveColumn ? moveColumn() : 1;
            return *choice != -1;
        }
        return count != 0;
    }

    // A double click on a column's picked card: it goes to the first empty free cell
    bool toFreeCell()
    {
        int cell = 0;
        while (cell < Cells && m_now.card[Top][cell] != -1)
            ++cell;
        if (cell == Cells)
            return false;
        m_plan = m_now;
        m_queue.clear();
        record(m_selPile, m_now.bottom(m_selPile), Top, cell);
        sendHome();
        m_selected = false;
        m_undoing = false;
        m_all = m_queue;
        play();
        return true;
    }

    // A move, written down and made on the plan
    void record(int fromPile, int fromPos, int toPile, int toPos)
    {
        Move move{fromPile, fromPos, toPile, toPos};
        m_queue.append(move);
        if (resolve(m_plan, move))
            commit(m_plan, move);
    }

    // The last cards of FROM to TO through the free cells, as many as the cells let pass
    void moveRun(int from, int to)
    {
        QList<int> cells;
        for (int i = 0; i < Cells; ++i) {
            if (m_plan.card[Top][i] == -1)
                cells.append(i);
        }
        const int count = qMin(needed(m_plan, from, to), int(cells.size()) + 1);
        for (int i = 0; i < count - 1; ++i)
            record(from, 0, Top, cells[i]);
        record(from, 0, to, 0);
        for (int i = count - 2; i >= 0; --i)
            record(Top, cells[i], to, 0);
    }

    // The same with more cards than that: the empty columns each take a run on the way
    void moveColumnTo(int from, int to)
    {
        const int cells = m_plan.freeCells();
        int count = needed(m_plan, from, to);
        if (count <= cells + 1) {
            moveRun(from, to);
            return;
        }
        QList<int> columns;
        for (int pile = 1; pile < Piles; ++pile) {
            if (m_plan.card[pile][0] == -1)
                columns.append(pile);
        }
        int used = 0;
        do {
            moveRun(from, columns.value(used, to));
            count -= cells + 1;
            ++used;
        } while (cells + 1 < count);
        for (;;) {
            moveRun(from, to);
            if (--used < 0)
                break;
            from = columns.value(used, to);
        }
    }

    // Whether a card may go home by itself: an ace, a two on its ace, another when both suits of
    // the other colour have reached the rank under it
    bool goesHome(int card) const
    {
        if (card == -1)
            return false;
        if (cheat == 2)
            return true;
        const int rank = cards::rank(card), suit = cards::suit(card);
        if (rank == 0)
            return true;
        if (rank == 1)
            return m_plan.homeRank[suit] == 0;
        if (m_plan.homeRank[suit] != rank - 1)
            return false;
        const int first = cards::red(card) ? m_plan.homeRank[cards::Clubs] : m_plan.homeRank[cards::Diamonds];
        const int second = cards::red(card) ? m_plan.homeRank[cards::Spades] : m_plan.homeRank[cards::Hearts];
        return first != -1 && second != -1 && rank - 1 <= first && rank - 1 <= second;
    }

    int homeSlot(int card) const
    {
        int slot = m_plan.homeSlot[cards::suit(card)];
        if (slot == -1) {
            slot = Cells;
            while (slot < 2 * Cells - 1 && m_plan.card[Top][slot] != -1)
                ++slot;
        }
        return slot;
    }

    void sendHome()
    {
        bool moved = true;
        while (moved) {
            moved = false;
            for (int cell = 0; cell < Cells; ++cell) {
                const int card = m_plan.card[Top][cell];
                if (goesHome(card)) {
                    moved = true;
                    record(Top, cell, Top, homeSlot(card));
                }
            }
            for (int pile = 1; pile < Piles; ++pile) {
                const int pos = m_plan.bottom(pile);
                if (pos >= 0 && goesHome(m_plan.card[pile][pos])) {
                    moved = true;
                    record(pile, pos, Top, homeSlot(m_plan.card[pile][pos]));
                }
            }
        }
    }

    // The moves written down are made one after the other, each card sliding to its place
    void play()
    {
        if (m_flying)
            return;
        while (!m_queue.isEmpty()) {
            Move move = m_undoing ? m_queue.takeLast() : m_queue.takeFirst();
            m_mid = m_now;
            m_after = m_now;
            if (m_undoing) {
                // Back from where it went to where it was
                if (move.toPile != Top) {
                    if (move.toPile == move.fromPile)
                        break;
                    move.toPos = m_now.bottom(move.toPile);
                }
                if (move.fromPile != Top)
                    move.fromPos = m_now.bottom(move.fromPile) + 1;
                if (move.toPos < 0 || m_now.card[move.toPile][move.toPos] == -1)
                    continue;
                m_card = m_now.card[move.toPile][move.toPos];
                int under = -1;
                if (move.toPile == Top && move.toPos >= Cells) {
                    const int suit = cards::suit(m_card);
                    ++m_after.left;
                    --m_after.homeRank[suit];
                    if (cards::rank(m_card) == 0)
                        m_after.homeSlot[suit] = -1;
                    else
                        under = m_card - 4;
                }
                m_after.card[move.toPile][move.toPos] = under;
                m_mid = m_after;
                m_after.card[move.fromPile][move.fromPos] = m_card;
                m_from = place(move.toPile, move.toPos);
                m_to = place(move.fromPile, move.fromPos);
            } else {
                if (!resolve(m_now, move))
                    continue;
                m_card = m_now.card[move.fromPile][move.fromPos];
                m_mid.card[move.fromPile][move.fromPos] = -1;
                commit(m_after, move);
                m_from = place(move.fromPile, move.fromPos);
                m_to = place(move.toPile, move.toPos);
                if (move.toPile == Top)
                    m_looksLeft = move.toPos < Cells;
            }
            const QPoint by = m_to - m_from;
            m_steps = quick ? 1 : int(std::sqrt(double(by.x()) * by.x() + double(by.y()) * by.y())) / 37;
            if (m_steps > 1) {
                m_step = 0;
                m_flying = true;
                m_flight.start(12, Qt::PreciseTimer, this);
                update();
                return;
            }
            m_now = m_after;
            if (changed)
                changed();
        }
        m_queue.clear();
        finished();
    }

    void land()
    {
        m_flying = false;
        m_now = m_after;
        notify();
    }

    void finished()
    {
        if (m_undoing) {
            m_undoing = false;
            notify();
            return;
        }
        // A card put back where it was is nothing to undo
        if (m_all.size() < 2 && (m_all.isEmpty() || m_all.first().fromPile == m_all.first().toPile))
            m_undo.clear();
        else
            m_undo = m_all;
        m_all.clear();
        notify();
        if (m_now.left == 0) {
            m_undo.clear();
            m_smile = true;
            notify();
            if (won)
                won();
        } else {
            checkLost();
        }
    }

    // With every cell and column taken, the moves left are counted: none and the game is lost,
    // one and the window flashes
    void checkLost()
    {
        if (cheat != 1) {
            if (m_now.freeCells() != 0 || m_now.emptyColumns() != 0)
                return;
            int last[Piles];
            int moves = 0;
            for (int pile = 1; pile < Piles; ++pile) {
                last[pile] = m_now.card[pile][m_now.bottom(pile)];
                moves += cards::rank(last[pile]) == 0;
                moves += m_now.homeRank[cards::suit(last[pile])] == cards::rank(last[pile]) - 1;
            }
            for (int cell = 0; cell < Cells; ++cell) {
                const int card = m_now.card[Top][cell];
                moves += m_now.homeRank[cards::suit(card)] == cards::rank(card) - 1;
                for (int pile = 1; pile < Piles; ++pile)
                    moves += fits(card, last[pile]);
            }
            for (int from = 1; from < Piles; ++from) {
                for (int to = 1; to < Piles; ++to)
                    moves += from != to && fits(last[from], last[to]);
            }
            if (moves != 0) {
                if (moves == 1)
                    QApplication::alert(window(), 1600);
                return;
            }
        }
        m_undo.clear();
        cheat = 0;
        notify();
        if (lost)
            lost();
    }

    const int m_dx, m_dy;
    QImage m_king, m_kingLeft, m_kingSmile;
    State m_now, m_plan;
    int m_game = 0;
    bool m_inProgress = false;
    bool m_selected = false;
    int m_selPile = 0, m_selPos = 0;
    bool m_looksLeft = false;
    bool m_smile = false;
    bool m_peek = false;
    int m_peekPile = 0, m_peekPos = 0;
    // The moves to make, the ones of the turn being made and the ones Undo takes back
    QList<Move> m_queue, m_all, m_undo;
    bool m_undoing = false;
    // The card on its way: the table without it, the table once it is there
    bool m_flying = false;
    QBasicTimer m_flight;
    State m_mid, m_after;
    int m_card = 0;
    QPoint m_from, m_to;
    int m_step = 0, m_steps = 1;
};

// The window

FreeCell::FreeCell()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("freecell.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources || !cards::load())
        return;
    setWindowTitle(xpfiles::string(*m_resources, IDS_NAME));
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, IDI_MAIN, 32))));
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("FREEMENU"));
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, QStringLiteral("FREEMENU"));
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { command(id); });
    }
    // The help is freecell.chm, which nothing here can show
    for (int id : {IDM_HELP, IDM_HELP_SEARCH, IDM_HELP_USING}) {
        if (QAction *help = m_actions.value(id))
            help->setEnabled(false);
    }
    // The cards left are counted at the right of the menu bar
    m_left = new QLabel(menuBar());
    m_left->setContentsMargins(0, 0, 4, 0);
    m_left->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_left->setMinimumWidth(m_left->fontMetrics().horizontalAdvance(format(xpfiles::string(*m_resources, IDS_LEFT), {88})) + 12);
    menuBar()->setCornerWidget(m_left);

    m_board = new FreeBoard(*m_resources, this);
    setCentralWidget(m_board);
    m_board->changed = [this] { refresh(); };
    m_board->won = [this] { QMetaObject::invokeMethod(this, [this] { won(); }, Qt::QueuedConnection); };
    m_board->lost = [this] { QMetaObject::invokeMethod(this, [this] { lost(); }, Qt::QueuedConnection); };
    m_board->moveColumn = [this] { return moveColumn(); };
    m_board->illegal = [this](int needed, int most) { illegal(needed, most); };

    // What XP keeps under HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_board->messages = settings.value(QStringLiteral("messages"), 1).toInt();
    m_board->quick = settings.value(QStringLiteral("quick"), 0).toInt();
    m_board->doubleClick = settings.value(QStringLiteral("dblclick"), 1).toInt();
    // A window of 640 by 480, no wider than that
    const int width = 632;
    resize(width, 480 - 34 - 20 + menuBar()->sizeHint().height());
    setMaximumWidth(width);
    refresh();
}

void FreeCell::refresh()
{
    if (QAction *undo = m_actions.value(IDM_UNDO))
        undo->setEnabled(m_board->canUndo());
    m_left->setText(format(xpfiles::string(*m_resources, IDS_LEFT), {m_board->left()}));
}

void FreeCell::command(int id)
{
    if (m_board->busy())
        return;
    switch (id) {
    case IDM_NEW:
    case IDM_SELECT:
    case IDM_RESTART:
        start(id);
        break;
    case IDM_STATISTICS:
        statistics();
        break;
    case IDM_OPTIONS:
        options();
        break;
    case IDM_UNDO:
        m_board->undo();
        break;
    case IDM_EXIT:
        close();
        break;
    case IDM_CHEAT: {
        QMessageBox box(QMessageBox::Question, QStringLiteral("User-Friendly User Interface"),
                        QStringLiteral("Choose Abort to Win,\nRetry to Lose,\nor Ignore to Cancel."),
                        QMessageBox::Abort | QMessageBox::Retry | QMessageBox::Ignore, this);
        const int answer = box.exec();
        m_board->cheat = answer == QMessageBox::Abort ? 2 : answer == QMessageBox::Retry ? 1 : 0;
        break;
    }
    case IDM_ABOUT:
        applets::about(xpfiles::string(*m_resources, IDS_NAME), xpfiles::icon(*m_resources, IDI_MAIN, 32),
                       xpfiles::string(*m_resources, IDS_AUTHOR));
        break;
    default:
        break;
    }
}

bool FreeCell::resign()
{
    return QMessageBox::question(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_RESIGN))
           == QMessageBox::Yes;
}

// New Game, Select Game or Restart Game
void FreeCell::start(int id)
{
    const bool playing = m_board->inProgress();
    if (playing) {
        if (!resign())
            return;
        countLoss();
    }
    int game = 0;
    if (id == IDM_RESTART) {
        game = playing ? m_board->game() : m_counted;
    } else {
        m_select = id == IDM_SELECT;
        if (id == IDM_NEW)
            game = randomGame();
    }
    if (game == 0) {
        game = chooseGame(randomGame());
        if (game == 0) {
            m_board->stop();
            return;
        }
    }
    setWindowTitle(format(xpfiles::string(*m_resources, IDS_GAME), {game}));
    m_board->deal(game);
    if (QAction *restart = m_actions.value(IDM_RESTART))
        restart->setEnabled(true);
}

// Game Number: 0 when it is closed without a number
int FreeCell::chooseGame(int suggested)
{
    for (;;) {
        QDialog dialog(this);
        const xpdialog::Page page = xpdialog::build(*m_resources, QStringLiteral("GAMENUM"), &dialog);
        dialog.setWindowTitle(page.caption);
        auto *edit = qobject_cast<QLineEdit *>(page.controls.value(IDC_NUMBER));
        if (edit) {
            edit->setText(QString::number(suggested));
            edit->selectAll();
            edit->setFocus();
        }
        if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
            ok->setDefault(true);
            connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        }
        if (dialog.exec() != QDialog::Accepted || !edit)
            return 0;
        const int game = edit->text().trimmed().toInt();
        if (game != 0 && game >= -2 && game <= LastGame)
            return game;
    }
}

// A game given up or lost, once for a game
void FreeCell::countLoss()
{
    const int game = m_board->game();
    if (game > 0 && game != m_counted) {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        ++m_sessionLost;
        settings.setValue(QStringLiteral("lost"), settings.value(QStringLiteral("lost"), 0).toInt() + 1);
        int streak = 1;
        if (settings.value(QStringLiteral("stype"), 1).toInt() == 1)
            settings.setValue(QStringLiteral("stype"), 0);
        else
            streak = settings.value(QStringLiteral("streak"), 0).toInt() + 1;
        settings.setValue(QStringLiteral("streak"), streak);
        if (settings.value(QStringLiteral("losses"), 0).toInt() < streak)
            settings.setValue(QStringLiteral("losses"), streak);
    }
    m_counted = game;
}

void FreeCell::countWin()
{
    if (m_board->game() == m_counted)
        return;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    ++m_sessionWon;
    settings.setValue(QStringLiteral("won"), settings.value(QStringLiteral("won"), 0).toInt() + 1);
    int streak = 1;
    if (settings.value(QStringLiteral("stype"), 0).toInt() == 0)
        settings.setValue(QStringLiteral("stype"), 1);
    else
        streak = settings.value(QStringLiteral("streak"), 0).toInt() + 1;
    settings.setValue(QStringLiteral("streak"), streak);
    if (settings.value(QStringLiteral("wins"), 0).toInt() < streak)
        settings.setValue(QStringLiteral("wins"), streak);
}

namespace {

// The buttons of one of freecell.exe's small dialogs, each closing it with its own answer
void answers(QDialog &dialog, const xpdialog::Page &page, const QList<int> &ids)
{
    for (int id : ids) {
        if (auto *button = qobject_cast<QPushButton *>(page.controls.value(id))) {
            button->setAutoDefault(false);
            QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, id] { dialog.done(id); });
        }
    }
    if (auto *first = qobject_cast<QPushButton *>(page.controls.value(ids.value(0)))) {
        first->setDefault(true);
        first->setFocus();
    }
}

}

void FreeCell::won()
{
    countWin();
    m_board->stop();
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, QStringLiteral("YOUWIN"), &dialog);
    dialog.setWindowTitle(page.caption);
    auto *select = qobject_cast<QAbstractButton *>(page.controls.value(IDC_SELECT_GAME));
    if (select)
        select->setChecked(m_select);
    answers(dialog, page, {6, 7});
    const bool again = dialog.exec() == 6;
    if (again && select)
        m_select = select->isChecked();
    m_counted = m_board->game();
    m_board->forget();
    if (again)
        start(m_select ? IDM_SELECT : IDM_NEW);
}

void FreeCell::lost()
{
    m_board->stop();
    countLoss();
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, QStringLiteral("YOULOSE"), &dialog);
    dialog.setWindowTitle(page.caption);
    auto *same = qobject_cast<QAbstractButton *>(page.controls.value(IDC_SAME_GAME));
    if (same)
        same->setChecked(true);
    answers(dialog, page, {6, 7});
    const bool again = dialog.exec() == 6;
    m_board->forget();
    if (again)
        start(same && same->isChecked() ? IDM_RESTART : m_select ? IDM_SELECT : IDM_NEW);
}

// Move to Empty Column: 1 the column, 0 a single card, -1 Cancel
int FreeCell::moveColumn()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, QStringLiteral("MOVECOL"), &dialog);
    dialog.setWindowTitle(page.caption);
    answers(dialog, page, {IDC_MOVE_COLUMN, IDC_MOVE_CARD, 2});
    const int answer = dialog.exec();
    return answer == IDC_MOVE_COLUMN ? 1 : answer == IDC_MOVE_CARD ? 0 : -1;
}

void FreeCell::illegal(int needed, int most)
{
    QMessageBox::information(this, xpfiles::string(*m_resources, IDS_NAME),
                             needed == 0 ? xpfiles::string(*m_resources, IDS_ILLEGAL)
                                         : format(xpfiles::string(*m_resources, IDS_TOO_MANY), {needed, most}));
}

void FreeCell::statistics()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, QStringLiteral("STATS"), &dialog);
    dialog.setWindowTitle(page.caption);
    const auto percent = [](int won, int lost) {
        const int total = won + lost;
        if (total == 0)
            return 0;
        const int value = (won * 200 + total) / (total * 2);
        return value == 100 && lost != 0 ? 99 : value;
    };
    const auto fill = [&] {
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        const auto number = [&settings](const char *key) { return settings.value(QLatin1String(key), 0).toInt(); };
        const auto set = [&page](int id, const QString &text) {
            if (auto *label = qobject_cast<QLabel *>(page.controls.value(id)))
                label->setText(text);
        };
        set(IDC_SESSION, format(xpfiles::string(*m_resources, IDS_SESSION),
                                {percent(m_sessionWon, m_sessionLost), m_sessionWon, m_sessionLost}));
        set(IDC_TOTAL, format(xpfiles::string(*m_resources, IDS_TOTAL),
                              {percent(number("won"), number("lost")), number("won"), number("lost")}));
        const int streak = number("streak");
        const bool winning = number("stype") == 1;
        const QString current = streak == 0 ? QStringLiteral("0") : streak == 1 ? xpfiles::string(*m_resources, winning ? IDS_ONE_WIN : IDS_ONE_LOSS)
                                            : format(xpfiles::string(*m_resources, winning ? IDS_WINS : IDS_LOSSES), {streak});
        set(IDC_STREAKS, format(xpfiles::string(*m_resources, IDS_STREAKS), {number("wins"), number("losses"), current}));
    };
    fill();
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *clear = qobject_cast<QPushButton *>(page.controls.value(IDC_CLEAR))) {
        connect(clear, &QPushButton::clicked, &dialog, [&] {
            if (QMessageBox::question(&dialog, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_CLEAR))
                != QMessageBox::Yes)
                return;
            QSettings settings(settingsPath(), QSettings::IniFormat);
            for (const char *key : {"won", "lost", "wins", "losses", "streak", "stype"})
                settings.remove(QLatin1String(key));
            m_sessionWon = m_sessionLost = 0;
            settings.sync();
            fill();
        });
    }
    dialog.exec();
}

void FreeCell::options()
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
    check(IDC_MESSAGES, m_board->messages);
    check(IDC_QUICK, m_board->quick);
    check(IDC_DOUBLE_CLICK, m_board->doubleClick);
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_board->messages = checked(IDC_MESSAGES);
    m_board->quick = checked(IDC_QUICK);
    m_board->doubleClick = checked(IDC_DOUBLE_CLICK);
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("messages"), int(m_board->messages));
    settings.setValue(QStringLiteral("quick"), int(m_board->quick));
    settings.setValue(QStringLiteral("dblclick"), int(m_board->doubleClick));
}

void FreeCell::keyPressEvent(QKeyEvent *event)
{
    if (const int id = xpprogram::command(m_keys, event)) {
        if (QAction *action = m_actions.value(id); !action || action->isEnabled())
            command(id);
        return;
    }
    if (m_board && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9 && !(event->modifiers() & ~Qt::KeypadModifier)) {
        m_board->digit(event->key() - Qt::Key_0);
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void FreeCell::closeEvent(QCloseEvent *event)
{
    if (m_board && m_board->inProgress()) {
        if (!resign()) {
            event->ignore();
            return;
        }
        countLoss();
    }
    event->accept();
}
