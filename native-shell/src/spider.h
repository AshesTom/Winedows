// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Spider Solitaire as a Linux program: spider.exe's own menu, dialogs, words, felt, card back
// and sounds, laid out and played by its rules (one, two or four suits, the runs dragged from a
// column to another, the five deals, the suits taken away king to ace, the score that starts at
// 500, Undo, Show An Available Move, the saved game, the statistics and the options). The
// fireworks of a won game are this program's own, and a shuffle is not XP's for the same number.
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QAction;
class SpiderBoard;

class Spider : public QMainWindow
{
public:
    Spider();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void command(int id);
    void refresh();
    bool ask(int text, bool cancel = false, int *answer = nullptr);
    void newGame(bool sameCards);
    void count(bool won);
    void difficulty();
    void statistics();
    void options();
    void about();
    bool save();
    void open();
    void won();
    void loadOptions();
    void saveOptions();

    std::optional<w4w::PeResources> m_resources;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    SpiderBoard *m_board = nullptr;
    int m_suits = 1;
    bool m_saveOnExit = false, m_loadAtStart = false, m_promptSave = true, m_promptLoad = true;
};
