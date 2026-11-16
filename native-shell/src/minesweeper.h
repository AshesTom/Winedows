// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Minesweeper as a Linux program: winmine.exe's own tiles, digits, faces, menu, dialogs, words
// and sounds, laid out and played by its rules (the first square never a mine, both buttons to
// clear around a number, the three levels and Custom, Marks (?), Color, Sound and Best Times).
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QAction;
class MineField;

class Minesweeper : public QMainWindow
{
public:
    Minesweeper();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void command(int id);
    void refresh();
    void customField();
    void bestTimes();
    void won(int seconds);

    std::optional<w4w::PeResources> m_resources;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    MineField *m_field = nullptr;
};
