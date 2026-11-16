// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's FreeCell as a Linux program: freecell.exe's own menu, dialogs, words and king and cards.dll's
// cards, dealt and played by its rules (the numbered games, the card picked then put down, the
// columns moved through the free cells and the empty columns, the cards that go home by
// themselves, one Undo, the keys 0 to 9, the statistics and the three options). The pointer does
// not turn into its down arrow over a place that takes the card.
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QAction;
class QLabel;
class FreeBoard;

class FreeCell : public QMainWindow
{
public:
    FreeCell();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void command(int id);
    void start(int id);
    int chooseGame(int suggested);
    bool resign();
    void countLoss();
    void countWin();
    void refresh();
    void statistics();
    void options();
    void won();
    void lost();
    int moveColumn();
    void illegal(int needed, int most);

    std::optional<w4w::PeResources> m_resources;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    FreeBoard *m_board = nullptr;
    QLabel *m_left = nullptr;
    // The game a win or a loss was last counted for, and what Yes means after a won game
    int m_counted = 0;
    bool m_select = false;
    int m_sessionWon = 0, m_sessionLost = 0;
};
