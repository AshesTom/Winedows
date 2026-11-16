// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Solitaire as a Linux program: sol.exe's own menu, dialogs and words and cards.dll's cards,
// laid out and played by its rules (Draw One or Three, Standard, Vegas or no scoring, the timed
// game, one Undo, the double click and the right click that send cards up, the twelve backs and the
// cards that bounce away when the game is won). Its keyboard play is not here.
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QAction;
class SolBoard;
class SolStatus;

class Solitaire : public QMainWindow
{
public:
    Solitaire();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void command(int id);
    void refresh();
    void save();
    void options();
    void chooseBack();
    void won();

    std::optional<w4w::PeResources> m_resources;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    SolBoard *m_board = nullptr;
    SolStatus *m_status = nullptr;
    QString m_hint;
};
