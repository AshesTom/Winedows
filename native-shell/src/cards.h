// SPDX-License-Identifier: LGPL-2.1-or-later
// cards.dll's pictures, as its cdtDraw hands them to XP's card games: the 52 faces, the twelve
// backs, the empty place's outline and the deck's X and O, each with the three pixels of every
// corner left out (the cards' round corners).
#pragma once

#include <QColor>
#include <QImage>
#include <QSize>

namespace cards {

enum Suit { Clubs, Diamonds, Hearts, Spades };
enum { FirstBack = 54, LastBack = 65 };

// A card as cards.dll numbers it: the rank (0 the ace, 12 the king) times four, plus the suit
inline int card(int rank, int suit) { return rank * 4 + suit; }
inline int rank(int card) { return card / 4; }
inline int suit(int card) { return card % 4; }
inline bool red(int card) { return suit(card) == Diamonds || suit(card) == Hearts; }

// False when the user's XP files have no cards.dll
bool load();
// cdtInit's: 71 by 96
QSize size();
QImage face(int card);
// The face with its colours inverted (cdtDraw's hilite)
QImage inverted(int card);
QImage back(int id);
// The outline of an empty place over BACKGROUND
QImage ghost(const QColor &background);
// An empty deck that can be turned over (the green circle) and one that cannot (the red cross)
QImage deckO();
QImage deckX();

}
