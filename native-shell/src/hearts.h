// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Hearts as a Linux program: mshearts.exe's own menu, dialogs, words and sounds and
// cards.dll's cards, laid out as it lays them out (the four hands around the table, the trick in
// the middle, the names, the Pass button and the line of words at the bottom) and played by its
// rules against three computer players. They pass the cards mshearts.exe's players pass; how they
// play a trick is this program's own. The game over a network (a dealer's computer) is not here.
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QAction;
class HeartsBoard;

class Hearts : public QMainWindow
{
public:
    Hearts();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void command(int id);
    bool askName();
    void options();
    void scoreSheet(bool handOver);
    void quote();
    void loadNames();

    std::optional<w4w::PeResources> m_resources;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    HeartsBoard *m_board = nullptr;
};
