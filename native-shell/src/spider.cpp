// SPDX-License-Identifier: LGPL-2.1-or-later
#include "spider.h"

#include "cards.h"
#include "sounds.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QAction>
#include <QBasicTimer>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDataStream>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>

#include <cmath>
#include <functional>
#include <random>

namespace {

// spider.exe's resources
enum {
    IDM_MENU = 101, IDM_ABOUT = 40002, IDM_HELP = 40003, IDM_EXIT = 40004, IDM_NEW = 40005, IDM_RESTART = 40006,
    IDM_DEAL = 40007, IDM_UNDO = 40010, IDM_SAVE = 40011, IDM_OPEN = 40012, IDM_HINT = 40013, IDM_STATISTICS = 40014,
    IDM_OPTIONS = 40015, IDM_DEAL_BAR = 40016, IDM_DIFFICULTY = 40017,
    IDS_NAME = 1, IDS_NEW = 3, IDS_RESTART = 4, IDS_EMPTY_SLOTS = 5, IDS_COPYRIGHT = 15, IDS_REPLACE = 16, IDS_DISCARD = 17,
    IDS_NO_SAVE = 18, IDS_NO_LOAD = 19, IDS_HIGH = 33, IDS_WINS = 34, IDS_LOSSES = 35, IDS_MOST_WINS = 36,
    IDS_MOST_LOSSES = 37, IDS_CURRENT_WINS = 38, IDS_CURRENT_LOSSES = 39, IDS_RATE = 41, IDS_WON = 45, IDS_SAVE_FIRST = 50,
    IDS_RESET = 52, IDS_SCORE = 53, IDS_MOVES = 54,
    IDD_ABOUT = 107, IDD_OPTIONS = 117, IDD_STATISTICS = 118, IDD_DIFFICULTY = 119, IDD_WON = 130,
    IDB_ABOUT = 106, IDB_EMPTY = 108,
    IDW_CARD = 124, IDW_DROP = 125, IDW_HINT = 126, IDW_NO_HINT = 127, IDW_PICK = 128, IDW_WIN = 129,
    IDI_MAIN = 103,
};

enum { Columns = 10, Deals = 5, CardW = 71, CardH = 96, Top = 10, Hidden = 7, Spread = 28, Shelf = 106, MostUndo = 150 };

QString configPath(const char *name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/") + QLatin1String(name);
}

QString format(QString form, int value)
{
    form.replace(QStringLiteral("%%"), QStringLiteral("%"));
    return form.replace(QStringLiteral("%d"), QString::number(value));
}

const char *levelName(int suits)
{
    return suits == 1 ? "Easy" : suits == 2 ? "Medium" : "Difficult";
}

// A card is its suit times thirteen plus its rank, as spider.exe's CARD pictures are numbered
struct SCard
{
    int id = 0;
    bool up = false;
};
int rankOf(int id) { return id % 13; }
int suitOf(int id) { return id / 13; }

struct UndoMove
{
    bool turned = false;
    int from = 0, to = 0, index = 0;
};

struct Hint
{
    int from = 0, index = 0, to = 0, onto = 0, worth = 0;
};

struct Spark
{
    QPointF at, speed;
    QColor colour;
    int life = 0;
};

}

// The table

class SpiderBoard : public QWidget
{
public:
    SpiderBoard(const w4w::PeResources &pe, QWidget *parent)
        : QWidget(parent), m_pe(pe)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        bool partial = false;
        m_back = w4w::decodeDib(pe.find(QStringLiteral("2"), QStringLiteral("CARDBACK")), &partial);
        m_empty = xpfiles::bitmap(pe, IDB_EMPTY);
        m_felt = QPixmap::fromImage(w4w::decodeDib(pe.find(QStringLiteral("2"), QStringLiteral("FELT")), &partial).copy(0, 0, 63, 64));
        std::fill(std::begin(m_spread), std::end(m_spread), int(Spread));
        setMinimumSize(600, 400);
    }

    std::function<void()> changed;
    std::function<void()> won;
    std::function<void(int)> message;

    bool animate = true;
    bool sound = true;

    int suits() const { return m_suits; }
    quint32 seed() const { return m_seed; }
    int score() const { return m_score; }
    int moves() const { return m_moves; }
    int deals() const { return m_deals; }
    bool over() const { return m_over; }
    bool started() const { return m_moves > 0 || m_deals > 0; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool busy() const { return m_flying || !m_steps.isEmpty(); }
    int cardsLeft() const
    {
        int count = 0;
        for (const auto &column : m_columns)
            count += column.size();
        return count;
    }

    void deal(int suits, quint32 seed)
    {
        stopAll();
        m_suits = suits;
        m_seed = seed;
        // Two decks of the suits played: spades, spades and hearts, or all four
        QList<int> deck;
        for (int copy = 0; copy < 8; ++copy) {
            const int suit = suits == 1 ? cards::Spades : suits == 2 ? (copy % 2 ? cards::Hearts : cards::Spades) : copy % 4;
            for (int rank = 0; rank < 13; ++rank)
                deck.append(suit * 13 + rank);
        }
        std::mt19937 random(seed);
        for (int i = int(deck.size()) - 1; i > 0; --i)
            deck.swapItemsAt(i, int(random() % quint32(i + 1)));
        for (auto &column : m_columns)
            column.clear();
        m_done.clear();
        m_undo.clear();
        m_hintsKnown = false;
        m_deals = 0;
        m_moves = 0;
        m_score = 500;
        m_over = false;
        // 44 cards face down, a fifth row on the first four columns only
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < Columns; ++column) {
                if (row < 4 || column < 4)
                    m_columns[column].append({deck.takeLast(), false});
            }
        }
        m_stock = deck;
        fit();
        notify();
        dealRow(true);
    }

    // A row from the stock, a card on every column
    void dealRow(bool first = false)
    {
        if (busy() || m_over || (!first && m_deals >= Deals))
            return;
        if (!first) {
            for (const auto &column : m_columns) {
                if (column.isEmpty()) {
                    if (message)
                        message(IDS_EMPTY_SLOTS);
                    return;
                }
            }
        }
        for (int column = 0; column < Columns; ++column) {
            m_steps.append([this, column] {
                const QPoint from = stockPlace(stockPiles() - 1);
                const int id = m_stock.takeLast();
                const auto land = [this, column, id] {
                    m_columns[column].append({id, true});
                    play(IDW_CARD);
                };
                if (animate)
                    fly(face(id), from, place(column, int(m_columns[column].size()), true), land);
                else
                    land();
            });
        }
        m_steps.append([this, first] {
            if (!first)
                ++m_deals;
            m_undo.clear();
            m_hintsKnown = false;
            fit();
            notify();
            for (int column = 0; column < Columns; ++column) {
                if (complete(column))
                    takeAway(column);
            }
        });
        run();
    }

    void undo()
    {
        if (busy() || m_undo.isEmpty() || m_over)
            return;
        const UndoMove last = m_undo.takeLast();
        move(last.to, last.index, last.from, false, last.turned);
    }

    // Show An Available Move: the cards that can go, then where; the next one the next time
    void hint()
    {
        if (busy() || m_over || m_flash.isValid())
            return;
        if (!m_hintsKnown)
            findHints();
        if (m_hints.isEmpty()) {
            play(IDW_NO_HINT);
            return;
        }
        play(IDW_HINT);
        const Hint hint = m_hints[m_nextHint];
        m_nextHint = (m_nextHint + 1) % int(m_hints.size());
        m_flash = runRect(hint.from, hint.index);
        update();
        QTimer::singleShot(250, this, [this, hint] {
            m_flash = runRect(hint.to, hint.onto);
            update();
            QTimer::singleShot(250, this, [this] {
                m_flash = QRect();
                update();
            });
        });
    }

    void write(QDataStream &out) const
    {
        out << qint32(m_suits) << m_seed << qint32(m_deals) << qint32(m_moves) << qint32(m_score) << m_done << m_stock;
        for (const auto &column : m_columns) {
            out << qint32(column.size());
            for (const SCard &card : column)
                out << qint32(card.id) << card.up;
        }
    }

    bool read(QDataStream &in)
    {
        qint32 suits = 0, deals = 0, moves = 0, score = 0;
        quint32 seed = 0;
        QList<int> done, stock;
        QList<SCard> columns[Columns];
        in >> suits >> seed >> deals >> moves >> score >> done >> stock;
        for (auto &column : columns) {
            qint32 count = 0;
            in >> count;
            for (int i = 0; i < count && i < 104; ++i) {
                qint32 id = 0;
                bool up = false;
                in >> id >> up;
                column.append({id, up});
            }
        }
        if (in.status() != QDataStream::Ok || (suits != 1 && suits != 2 && suits != 4))
            return false;
        stopAll();
        m_suits = suits;
        m_seed = seed;
        m_deals = deals;
        m_moves = moves;
        m_score = score;
        m_done = done;
        m_stock = stock;
        for (int i = 0; i < Columns; ++i)
            m_columns[i] = columns[i];
        m_undo.clear();
        m_hintsKnown = false;
        m_over = false;
        fit();
        notify();
        return true;
    }

    void celebrate(bool on)
    {
        m_fireworks = on;
        m_sparks.clear();
        if (on)
            m_show.start(20, this);
        else
            m_show.stop();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        fit();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawTiledPixmap(rect(), m_felt);
        for (int column = 0; column < Columns; ++column) {
            const auto &cardsIn = m_columns[column];
            const int count = m_dragging && m_dragColumn == column ? m_dragIndex : int(cardsIn.size());
            if (cardsIn.isEmpty() || count == 0) {
                drawCard(p, place(column, 0), m_empty);
                continue;
            }
            for (int i = 0; i < count; ++i)
                drawCard(p, place(column, i), cardsIn[i].up ? face(cardsIn[i].id) : m_back);
        }
        // The score, between what is left to deal and the suits taken away
        const QRect box = scoreRect();
        p.fillRect(box, QColor(0, 127, 0));
        p.setPen(Qt::black);
        p.drawRect(box.adjusted(0, 0, -1, -1));
        p.setPen(Qt::white);
        QFont bold = font();
        bold.setBold(true);
        // (XP writes them in the System font)
        bold.setPixelSize(15);
        p.setFont(bold);
        p.drawText(QRect(box.left() + 10, box.top() + 30, 90, 20), Qt::AlignRight | Qt::AlignTop | Qt::TextSingleLine,
                   xpfiles::string(m_pe, IDS_SCORE));
        p.drawText(QRect(box.left() + 10, box.top() + 50, 90, 30), Qt::AlignRight | Qt::AlignTop | Qt::TextSingleLine,
                   xpfiles::string(m_pe, IDS_MOVES));
        p.drawText(QRect(box.left() + 110, box.top() + 30, 90, 20), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine,
                   QString::number(m_score));
        p.drawText(QRect(box.left() + 110, box.top() + 50, 90, 30), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine,
                   QString::number(m_moves));
        for (int pile = 0; pile < stockPiles(); ++pile)
            drawCard(p, stockPlace(pile), m_back);
        for (int i = 0; i < m_done.size(); ++i)
            drawCard(p, donePlace(i), face(m_done[i] * 13 + 12));
        if (m_peeking && m_peekIndex < m_columns[m_peekColumn].size())
            drawCard(p, place(m_peekColumn, m_peekIndex), face(m_columns[m_peekColumn][m_peekIndex].id));
        if (m_dragging) {
            const auto &cardsIn = m_columns[m_dragColumn];
            for (int i = m_dragIndex; i < cardsIn.size(); ++i)
                drawCard(p, m_dragAt + QPoint(0, (i - m_dragIndex) * m_spread[m_dragColumn]), face(cardsIn[i].id));
        }
        if (m_flying)
            drawCard(p, m_from + (m_to - m_from) * int(qMin(m_clock.elapsed(), qint64(100))) / 100, m_picture);
        if (m_flash.isValid()) {
            p.setCompositionMode(QPainter::RasterOp_NotDestination);
            p.fillRect(m_flash, Qt::white);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        if (m_fireworks) {
            for (const Spark &spark : std::as_const(m_sparks))
                p.fillRect(QRectF(spark.at, QSizeF(3, 3)), spark.colour);
            QFont big(QStringLiteral("Arial"));
            big.setPixelSize(64);
            big.setBold(true);
            p.setFont(big);
            p.setPen(Qt::yellow);
            p.drawText(rect().adjusted(0, 0, 0, -Shelf), Qt::AlignCenter, xpfiles::string(m_pe, IDS_WON));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        int index = -2;
        const int column = hit(at, &index);
        if (busy() || m_dragging)
            return;
        if (event->button() == Qt::RightButton) {
            // The right button shows the whole of a card the others cover
            if (column >= 0 && index >= 0 && m_columns[column][index].up) {
                m_peeking = true;
                m_peekColumn = column;
                m_peekIndex = index;
                update();
            }
            return;
        }
        if (event->button() != Qt::LeftButton || m_over || m_peeking || column < 0 || index < 0 || !movable(column, index))
            return;
        play(IDW_PICK);
        m_dragging = true;
        m_dragColumn = column;
        m_dragIndex = index;
        m_dragAt = place(column, index);
        m_grab = at - m_dragAt;
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
            return;
        m_dragAt = event->position().toPoint() - m_grab;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        if (event->button() == Qt::RightButton) {
            m_peeking = false;
            update();
            return;
        }
        if (event->button() != Qt::LeftButton || busy())
            return;
        if (!m_dragging) {
            if (m_over)
                return;
            // A click on the cards left deals them, one on the score shows a move
            if (m_deals < Deals && stockRect().contains(at))
                dealRow();
            else if (scoreRect().contains(at))
                hint();
            return;
        }
        m_dragging = false;
        play(IDW_DROP);
        const int from = m_dragColumn, index = m_dragIndex;
        const int height = (int(m_columns[from].size()) - 1 - index) * m_spread[from] + CardH;
        const QRect dropped(m_dragAt, QSize(CardW, height));
        int first = columnAt(dropped.left()), last = columnAt(dropped.left() + CardW);
        if (last < 0)
            last = qMin(first + 1, Columns - 1);
        int target = -1;
        for (int column = first; column <= last && target < 0; ++column) {
            if (column < 0 || column >= Columns)
                continue;
            const QRect onto(place(column, qMax(0, int(m_columns[column].size()) - 1)), QSize(CardW, CardH));
            if (onto.intersects(dropped) && accepts(from, index, column))
                target = column;
        }
        update();
        if (target < 0)
            return;
        move(from, index, target, true, false);
        if (complete(target))
            takeAway(target);
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_flight.timerId()) {
            if (m_clock.elapsed() >= 100) {
                m_flight.stop();
                m_flying = false;
                const auto land = m_land;
                m_land = nullptr;
                if (land)
                    land();
                update();
                run();
            } else {
                update();
            }
            return;
        }
        if (event->timerId() == m_show.timerId())
            sparkle();
    }

private:
    // Where things are
    int gap() const { return (width() - Columns * CardW) / (Columns + 1); }
    int columnX(int column) const { return (gap() + CardW) * column + qMax(0, gap()); }
    int shelf() const { return height() - Shelf; }
    int hidden(int column) const
    {
        int count = 0;
        while (count < m_columns[column].size() && !m_columns[column][count].up)
            ++count;
        return count;
    }
    // INDEX may be the place after the column's last card; NEXT_UP says that place takes a card face up
    QPoint place(int column, int index, bool nextUp = false) const
    {
        const int down = hidden(column);
        Q_UNUSED(nextUp);
        const int y = index < down ? Top + index * Hidden : Top + down * Hidden + (index - down) * m_spread[column];
        return QPoint(columnX(column), y);
    }
    int stockPiles() const { return (int(m_stock.size()) + 9) / 10; }
    QPoint stockPlace(int pile) const { return QPoint(columnX(Columns - 1) - 12 * pile, shelf()); }
    QRect stockRect() const
    {
        if (stockPiles() == 0)
            return {};
        return QRect(stockPlace(stockPiles() - 1), QPoint(columnX(Columns - 1) + CardW - 1, shelf() + CardH - 1));
    }
    QPoint donePlace(int index) const { return QPoint(qMax(0, gap()) + 12 * index, shelf()); }
    QRect scoreRect() const { return QRect((width() - 200) / 2, shelf(), 200, CardH); }
    QRect runRect(int column, int index) const
    {
        const int last = qMax(0, int(m_columns[column].size()) - 1);
        return QRect(place(column, qMax(0, index)), QSize(CardW, place(column, last).y() - place(column, qMax(0, index)).y() + CardH));
    }

    int columnAt(int x) const
    {
        const int left = qMax(0, gap());
        if (x < left)
            return -2;
        const int pitch = gap() + CardW;
        const int column = (x - left) / pitch;
        return (x - left) % pitch < CardW + 1 && column < Columns ? column : -2;
    }

    // The column under a point, -1 for none; INDEX is the card, -1 on an empty column, -2 for none
    int hit(const QPoint &at, int *index) const
    {
        *index = -2;
        const int column = columnAt(at.x());
        if (column < 0 || at.y() < Top)
            return -1;
        const int down = at.y() - Top;
        const int count = int(m_columns[column].size()), under = hidden(column), spread = qMax(1, m_spread[column]);
        if (count == 0) {
            if (down < CardH)
                *index = -1;
            return column;
        }
        const int lastTop = (count - under - 1) * spread + under * Hidden;
        if (down < under * Hidden)
            *index = down / Hidden;
        else if (down < lastTop)
            *index = (down - under * Hidden) / spread + under;
        else if (down < lastTop + CardH)
            *index = count - 1;
        return column;
    }

    // The columns' cards close up when a column would reach the shelf at the bottom
    void fit()
    {
        for (int column = 0; column < Columns; ++column) {
            if (m_columns[column].isEmpty())
                continue;
            const int last = int(m_columns[column].size()) - 1;
            m_spread[column] = Spread;
            for (;;) {
                const int top = place(column, last).y();
                if (top + CardH < shelf() || (m_spread[column] < 16 && top < shelf() - 16) || m_spread[column] < 1)
                    break;
                --m_spread[column];
            }
        }
        update();
    }

    QImage face(int id) const { return cards::face(cards::card(rankOf(id), suitOf(id))); }

    // A card, with the three pixels of each corner in the felt's green
    void drawCard(QPainter &p, const QPoint &at, const QImage &image) const
    {
        p.drawImage(at, image);
        const QColor green(0, 128, 0);
        const int x = at.x(), y = at.y(), r = CardW - 1, b = CardH - 1;
        for (const QPoint &point : {QPoint(0, 0), QPoint(1, 0), QPoint(0, 1), QPoint(r, 0), QPoint(r - 1, 0), QPoint(r, 1),
                                    QPoint(0, b), QPoint(1, b), QPoint(0, b - 1), QPoint(r, b), QPoint(r - 1, b), QPoint(r, b - 1)})
            p.fillRect(x + point.x(), y + point.y(), 1, 1, green);
    }

    void play(int id) const
    {
        if (!sound)
            return;
        // The player takes files: the WAVE resource, written out once
        const QString path = QDir::tempPath() + QStringLiteral("/win4wine-spider-%1.wav").arg(id);
        if (!QFile::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return;
            file.write(m_pe.find(QStringLiteral("WAVE"), QString::number(id)));
        }
        sounds::playFile(path);
    }

    void notify()
    {
        update();
        if (changed)
            changed();
    }

    void stopAll()
    {
        m_flight.stop();
        m_flying = false;
        m_land = nullptr;
        m_steps.clear();
        m_dragging = false;
        m_peeking = false;
        m_flash = QRect();
        celebrate(false);
    }

    // What is queued is done in turn; a step may send a card flying, and the rest waits for it
    void run()
    {
        while (!m_flying && !m_steps.isEmpty()) {
            const auto step = m_steps.takeFirst();
            step();
        }
    }

    void fly(const QImage &picture, const QPoint &from, const QPoint &to, const std::function<void()> &land)
    {
        m_picture = picture;
        m_from = from;
        m_to = to;
        m_land = land;
        m_flying = true;
        m_clock.start();
        m_flight.start(5, Qt::PreciseTimer, this);
    }

    // A card that is face up with nothing but its own suit in order over it
    bool movable(int column, int index) const
    {
        const auto &cardsIn = m_columns[column];
        if (!cardsIn[index].up)
            return false;
        for (int i = index + 1; i < cardsIn.size(); ++i) {
            if (suitOf(cardsIn[i].id) != suitOf(cardsIn[i - 1].id) || rankOf(cardsIn[i].id) != rankOf(cardsIn[i - 1].id) - 1)
                return false;
        }
        return true;
    }

    // An empty column takes anything, another the rank under its last card, of any suit
    bool accepts(int from, int index, int to) const
    {
        if (from == to || m_columns[from].isEmpty())
            return false;
        if (m_columns[to].isEmpty())
            return true;
        const SCard &onto = m_columns[to].last();
        return onto.up && m_columns[from][index].up && rankOf(onto.id) == rankOf(m_columns[from][index].id) + 1;
    }

    // The cards of FROM from INDEX go to TO. A move costs a point, and so does taking it back
    void move(int from, int index, int to, bool remember, bool turnBack)
    {
        const int landed = int(m_columns[to].size());
        if (!remember && turnBack && landed > 0)
            m_columns[to][landed - 1].up = false;
        m_columns[to].append(m_columns[from].mid(index));
        m_columns[from].remove(index, m_columns[from].size() - index);
        if (remember) {
            UndoMove undo{false, from, to, landed};
            if (!m_columns[from].isEmpty() && !m_columns[from].last().up) {
                m_columns[from].last().up = true;
                undo.turned = true;
            }
            if (m_undo.size() == MostUndo)
                m_undo.removeFirst();
            m_undo.append(undo);
        }
        ++m_moves;
        m_score = qMax(0, m_score - 1);
        m_hintsKnown = false;
        fit();
        notify();
    }

    // King to ace of a suit at the end of a column
    bool complete(int column) const
    {
        const auto &cardsIn = m_columns[column];
        const int count = int(cardsIn.size());
        if (count < 13)
            return false;
        for (int i = 0; i < 13; ++i) {
            const SCard &card = cardsIn[count - 1 - i];
            if (!card.up || rankOf(card.id) != i || suitOf(card.id) != suitOf(cardsIn[count - 1].id))
                return false;
        }
        return true;
    }

    // The suit leaves the table for the shelf, a hundred points; nothing before it can be undone
    void takeAway(int column)
    {
        const int suit = suitOf(m_columns[column].last().id);
        for (int i = 0; i < 13; ++i) {
            m_steps.append([this, column] {
                const int index = int(m_columns[column].size()) - 1;
                const QPoint from = place(column, index);
                const int id = m_columns[column].takeLast().id;
                if (animate)
                    fly(face(id), from, donePlace(int(m_done.size())), [this] { play(IDW_CARD); });
                else if (rankOf(id) == 12)
                    play(IDW_CARD);
            });
        }
        m_steps.append([this, column, suit] {
            m_done.append(suit);
            m_undo.clear();
            m_hintsKnown = false;
            m_score += 100;
            if (!m_columns[column].isEmpty())
                m_columns[column].last().up = true;
            fit();
            notify();
            if (cardsLeft() == 0 && m_stock.isEmpty()) {
                m_over = true;
                notify();
                play(IDW_WIN);
                if (won)
                    QTimer::singleShot(0, this, [this] { won(); });
            }
        });
        run();
    }

    // Every move there is: the longest run of each column onto each other column, the same suit
    // first, an empty column last
    void findHints()
    {
        m_hints.clear();
        m_nextHint = 0;
        m_hintsKnown = true;
        for (int from = 0; from < Columns; ++from) {
            const auto &cardsIn = m_columns[from];
            if (cardsIn.isEmpty())
                continue;
            int index = int(cardsIn.size()) - 1;
            while (index > 0 && cardsIn[index - 1].up && suitOf(cardsIn[index - 1].id) == suitOf(cardsIn[index].id)
                   && rankOf(cardsIn[index - 1].id) == rankOf(cardsIn[index].id) + 1)
                --index;
            for (int to = 0; to < Columns; ++to) {
                if (!accepts(from, index, to))
                    continue;
                const bool empty = m_columns[to].isEmpty();
                const int worth = empty ? 1 : suitOf(m_columns[to].last().id) == suitOf(cardsIn[index].id) ? 3 : 2;
                m_hints.append({from, index, to, int(m_columns[to].size()) - 1, worth});
            }
        }
        std::stable_sort(m_hints.begin(), m_hints.end(), [](const Hint &a, const Hint &b) { return a.worth > b.worth; });
    }

    void sparkle()
    {
        auto *random = QRandomGenerator::global();
        if (random->bounded(12) == 0) {
            const QPointF centre(random->bounded(qMax(1, width())), random->bounded(qMax(1, height() * 2 / 3)));
            const QColor colour = QColor::fromHsv(random->bounded(360), 255, 255);
            for (int i = 0; i < 60; ++i) {
                const double angle = random->bounded(628) / 100.0, speed = 1 + random->bounded(500) / 100.0;
                m_sparks.append({centre, QPointF(std::cos(angle) * speed, std::sin(angle) * speed), colour, 40 + random->bounded(20)});
            }
        }
        for (int i = int(m_sparks.size()) - 1; i >= 0; --i) {
            Spark &spark = m_sparks[i];
            spark.at += spark.speed;
            spark.speed.ry() += 0.08;
            if (--spark.life <= 0)
                m_sparks.removeAt(i);
        }
        update();
    }

    const w4w::PeResources &m_pe;
    QImage m_back, m_empty;
    QPixmap m_felt;
    QList<SCard> m_columns[Columns];
    QList<int> m_stock, m_done;
    int m_spread[Columns];
    int m_suits = 1;
    quint32 m_seed = 0;
    int m_deals = 0, m_moves = 0, m_score = 500;
    bool m_over = true;
    QList<UndoMove> m_undo;
    QList<Hint> m_hints;
    bool m_hintsKnown = false;
    int m_nextHint = 0;
    QRect m_flash;
    bool m_dragging = false;
    int m_dragColumn = 0, m_dragIndex = 0;
    QPoint m_dragAt, m_grab;
    bool m_peeking = false;
    int m_peekColumn = 0, m_peekIndex = 0;
    // The card in the air and what waits for it to land
    QList<std::function<void()>> m_steps;
    bool m_flying = false;
    QBasicTimer m_flight;
    QElapsedTimer m_clock;
    QImage m_picture;
    QPoint m_from, m_to;
    std::function<void()> m_land;
    bool m_fireworks = false;
    QBasicTimer m_show;
    QList<Spark> m_sparks;
};

// The window

Spider::Spider()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("spider.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources || !cards::load())
        return;
    setWindowTitle(xpfiles::string(*m_resources, IDS_NAME));
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, IDI_MAIN, 32))));
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("102"));
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, IDM_MENU);
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { command(id); });
    }
    // The help is spider.chm, which nothing here can show
    if (QAction *help = m_actions.value(IDM_HELP))
        help->setEnabled(false);

    m_board = new SpiderBoard(*m_resources, this);
    setCentralWidget(m_board);
    m_board->changed = [this] { refresh(); };
    m_board->won = [this] { won(); };
    m_board->message = [this](int text) {
        QMessageBox::information(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, text));
    };
    loadOptions();
    resize(800, 600);
    refresh();
    // The saved game or the choice of a difficulty first, once the window is there
    QTimer::singleShot(0, this, [this] {
        if (m_loadAtStart && QFile::exists(configPath("spider.sav")))
            open();
        if (m_board->over())
            difficulty();
        if (m_board->over() && m_board->cardsLeft() == 0)
            newGame(false);
    });
}

void Spider::loadOptions()
{
    // What XP keeps under HKCU\Software\Microsoft\Spider
    const QSettings settings(configPath("spider.conf"), QSettings::IniFormat);
    m_board->animate = settings.value(QStringLiteral("AnimDeal"), 1).toInt();
    m_saveOnExit = settings.value(QStringLiteral("SaveOnExit"), 0).toInt();
    m_loadAtStart = settings.value(QStringLiteral("LoadAtStart"), 0).toInt();
    m_promptSave = settings.value(QStringLiteral("PromptSave"), 1).toInt();
    m_promptLoad = settings.value(QStringLiteral("PromptLoad"), 1).toInt();
    m_board->sound = settings.value(QStringLiteral("Sound"), 1).toInt();
    m_suits = settings.value(QStringLiteral("NumSuits"), 1).toInt();
    if (m_suits != 2 && m_suits != 4)
        m_suits = 1;
}

void Spider::saveOptions()
{
    QSettings settings(configPath("spider.conf"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("AnimDeal"), int(m_board->animate));
    settings.setValue(QStringLiteral("SaveOnExit"), int(m_saveOnExit));
    settings.setValue(QStringLiteral("LoadAtStart"), int(m_loadAtStart));
    settings.setValue(QStringLiteral("PromptSave"), int(m_promptSave));
    settings.setValue(QStringLiteral("PromptLoad"), int(m_promptLoad));
    settings.setValue(QStringLiteral("Sound"), int(m_board->sound));
    settings.setValue(QStringLiteral("NumSuits"), m_suits);
}

void Spider::refresh()
{
    const bool playing = !m_board->over();
    const auto enable = [this](int id, bool on) {
        if (QAction *action = m_actions.value(id))
            action->setEnabled(on);
    };
    enable(IDM_UNDO, playing && m_board->canUndo());
    enable(IDM_RESTART, playing);
    enable(IDM_HINT, playing);
    enable(IDM_SAVE, playing);
    enable(IDM_DEAL, playing && m_board->deals() < Deals);
    enable(IDM_DEAL_BAR, playing && m_board->deals() < Deals);
    enable(IDM_OPEN, QFile::exists(configPath("spider.sav")));
}

bool Spider::ask(int text, bool cancel, int *answer)
{
    const auto buttons = cancel ? QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel : QMessageBox::Yes | QMessageBox::No;
    const int chosen = QMessageBox::question(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, text),
                                             buttons, cancel ? QMessageBox::Cancel : QMessageBox::No);
    if (answer)
        *answer = chosen;
    return chosen == QMessageBox::Yes;
}

void Spider::command(int id)
{
    if (m_board->busy())
        return;
    switch (id) {
    case IDM_NEW:
        if (!m_board->started() || m_board->over() || ask(IDS_NEW))
            newGame(false);
        break;
    case IDM_RESTART:
        if (m_board->started() && ask(IDS_RESTART))
            newGame(true);
        break;
    case IDM_DEAL:
    case IDM_DEAL_BAR:
        m_board->dealRow();
        break;
    case IDM_UNDO:
        m_board->undo();
        break;
    case IDM_HINT:
        m_board->hint();
        break;
    case IDM_SAVE:
        save();
        break;
    case IDM_OPEN:
        open();
        break;
    case IDM_DIFFICULTY:
        difficulty();
        break;
    case IDM_STATISTICS:
        statistics();
        break;
    case IDM_OPTIONS:
        options();
        break;
    case IDM_EXIT:
        close();
        break;
    case IDM_ABOUT:
        about();
        break;
    default:
        break;
    }
}

// A game ends for the statistics of its difficulty: the score, the count and the streaks
void Spider::count(bool won)
{
    QSettings settings(configPath("spider.conf"), QSettings::IniFormat);
    settings.beginGroup(QLatin1String(levelName(m_board->suits())));
    const auto number = [&settings](const char *key) { return settings.value(QLatin1String(key), 0).toInt(); };
    if (m_board->score() > number("HighScore"))
        settings.setValue(QStringLiteral("HighScore"), m_board->score());
    const bool winning = number("FWinStreak") != 0;
    const int streak = winning == won ? number("StreakCurrent") + 1 : 1;
    settings.setValue(QStringLiteral("FWinStreak"), int(won));
    settings.setValue(QStringLiteral("StreakCurrent"), streak);
    const char *most = won ? "StreakWins" : "StreakLosses";
    if (number(most) < streak)
        settings.setValue(QLatin1String(most), streak);
    if (won)
        settings.setValue(QStringLiteral("Wins"), number("Wins") + 1);
    else
        settings.setValue(QStringLiteral("Losses"), number("Losses") + 1);
}

void Spider::newGame(bool sameCards)
{
    // A game left after its first move is a game lost
    if (!m_board->over() && m_board->started())
        count(false);
    m_board->deal(sameCards ? m_board->suits() : m_suits,
                  sameCards ? m_board->seed() : quint32(QDateTime::currentSecsSinceEpoch()));
}

void Spider::difficulty()
{
    if (!m_board->over() && m_board->started()) {
        int answer = 0;
        if (ask(IDS_SAVE_FIRST, true, &answer))
            save();
        else if (answer == QMessageBox::Cancel)
            return;
    }
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_DIFFICULTY, &dialog);
    dialog.setWindowTitle(page.caption);
    auto *group = new QButtonGroup(&dialog);
    for (int i = 0; i < 3; ++i) {
        if (auto *radio = qobject_cast<QAbstractButton *>(page.controls.value(1008 + i))) {
            group->addButton(radio, 1 << i);
            radio->setChecked(m_suits == 1 << i);
        }
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || group->checkedId() < 1)
        return;
    m_suits = group->checkedId();
    saveOptions();
    newGame(false);
}

void Spider::statistics()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_STATISTICS, &dialog);
    dialog.setWindowTitle(page.caption);
    // The tab control's three tabs are named by three hidden labels
    auto *tabs = new QTabWidget(&dialog);
    for (int id : {1012, 1013, 1014}) {
        auto *label = qobject_cast<QLabel *>(page.controls.value(id));
        tabs->addTab(new QWidget, label ? label->text() : QString());
        if (label)
            label->hide();
    }
    if (QWidget *place = page.controls.value(1011)) {
        tabs->setGeometry(place->geometry());
        place->hide();
    }
    tabs->lower();
    const auto fill = [&] {
        QSettings settings(configPath("spider.conf"), QSettings::IniFormat);
        settings.beginGroup(QLatin1String(levelName(1 << tabs->currentIndex())));
        const auto number = [&settings](const char *key) { return settings.value(QLatin1String(key), 0).toInt(); };
        const auto set = [&](int id, int text, int value) {
            if (auto *label = qobject_cast<QLabel *>(page.controls.value(id)))
                label->setText(format(xpfiles::string(*m_resources, text), value));
        };
        const int wins = number("Wins"), losses = number("Losses");
        set(1006, IDS_HIGH, number("HighScore"));
        set(1003, IDS_WINS, wins);
        set(1004, IDS_LOSSES, losses);
        set(1005, IDS_RATE, wins + losses ? wins * 100 / (wins + losses) : 0);
        set(1007, IDS_MOST_WINS, number("StreakWins"));
        set(1008, IDS_MOST_LOSSES, number("StreakLosses"));
        set(1009, number("FWinStreak") ? IDS_CURRENT_WINS : IDS_CURRENT_LOSSES, number("StreakCurrent"));
    };
    tabs->setCurrentIndex(m_suits == 1 ? 0 : m_suits == 2 ? 1 : 2);
    connect(tabs, &QTabWidget::currentChanged, &dialog, fill);
    fill();
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *reset = qobject_cast<QPushButton *>(page.controls.value(1002))) {
        connect(reset, &QPushButton::clicked, &dialog, [&] {
            if (QMessageBox::question(&dialog, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_RESET))
                != QMessageBox::Yes)
                return;
            {
                QSettings settings(configPath("spider.conf"), QSettings::IniFormat);
                for (int suits : {1, 2, 4})
                    settings.remove(QLatin1String(levelName(suits)));
            }
            fill();
        });
    }
    dialog.exec();
}

void Spider::options()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_OPTIONS, &dialog);
    dialog.setWindowTitle(page.caption);
    bool *const values[] = {&m_board->animate, &m_saveOnExit, &m_loadAtStart, &m_promptSave, &m_promptLoad, &m_board->sound};
    for (int i = 0; i < 6; ++i) {
        if (auto *box = qobject_cast<QAbstractButton *>(page.controls.value(1001 + i)))
            box->setChecked(*values[i]);
    }
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < 6; ++i) {
        if (auto *box = qobject_cast<QAbstractButton *>(page.controls.value(1001 + i)))
            *values[i] = box->isChecked();
    }
    saveOptions();
}

// About Spider: the picture at the top, the rights under it
void Spider::about()
{
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_ABOUT, &dialog);
    dialog.setWindowTitle(page.caption);
    const QImage picture = xpfiles::bitmap(*m_resources, IDB_ABOUT);
    auto *image = new QLabel(&dialog);
    image->setPixmap(QPixmap::fromImage(picture));
    image->setGeometry(10, 10, picture.width(), picture.height());
    auto *words = new QLabel(xpfiles::string(*m_resources, IDS_COPYRIGHT), &dialog);
    words->setGeometry(10, picture.height() + 14, picture.width(), 30);
    words->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    dialog.exec();
}

bool Spider::save()
{
    const QString path = configPath("spider.sav");
    if (m_promptSave && QFile::exists(path) && !ask(IDS_REPLACE))
        return false;
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_NO_SAVE));
        return false;
    }
    QDataStream out(&file);
    m_board->write(out);
    file.close();
    refresh();
    return true;
}

void Spider::open()
{
    QFile file(configPath("spider.sav"));
    if (!file.exists())
        return;
    if (!m_board->over() && m_promptLoad && !ask(IDS_DISCARD))
        return;
    QDataStream in(&file);
    if (!file.open(QIODevice::ReadOnly) || !m_board->read(in)) {
        QMessageBox::warning(this, xpfiles::string(*m_resources, IDS_NAME), xpfiles::string(*m_resources, IDS_NO_LOAD));
        return;
    }
    m_suits = m_board->suits();
    refresh();
}

// Fireworks over the table, and Game Over in the window's corner
void Spider::won()
{
    count(true);
    refresh();
    m_board->celebrate(true);
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_WON, &dialog);
    dialog.setWindowTitle(page.caption);
    if (auto *yes = qobject_cast<QPushButton *>(page.controls.value(1))) {
        yes->setDefault(true);
        connect(yes, &QPushButton::clicked, &dialog, &QDialog::accept);
    }
    if (auto *no = qobject_cast<QPushButton *>(page.controls.value(2)))
        connect(no, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.adjustSize();
    const QRect frame = frameGeometry();
    dialog.move(frame.right() - dialog.width() - 20, frame.bottom() - dialog.height() - 40);
    const bool again = dialog.exec() == QDialog::Accepted;
    m_board->celebrate(false);
    if (again)
        newGame(false);
}

void Spider::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        showMinimized();
        return;
    }
    if (const int id = xpprogram::command(m_keys, event)) {
        if (QAction *action = m_actions.value(id); !action || action->isEnabled())
            command(id);
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void Spider::closeEvent(QCloseEvent *event)
{
    // (XP asks even before the first move)
    if (m_board && !m_board->over()) {
        bool saved = false;
        if (m_saveOnExit) {
            saved = save();
        } else {
            int answer = 0;
            if (ask(IDS_SAVE_FIRST, true, &answer)) {
                saved = save();
            } else if (answer == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
        }
        if (!saved && m_board->started())
            count(false);
    }
    event->accept();
}
