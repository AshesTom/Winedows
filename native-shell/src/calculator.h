// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Calculator as a Linux program: Standard and Scientific are calc.exe's own dialogs (templates 102
// and 101) with its menus, keys, words and button colours; the arithmetic works to XP's 32 digits
// (with libquadmath; 18 without), with Scientific's operator precedence and parentheses, Hex, Octal and
// Binary in four word sizes, the angle units, the memory and the Statistics Box.
#pragma once

#include "msstyles.h"
#include "xpprogram.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>

#include <optional>

class QAbstractButton;
class QAction;
class QCheckBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;

#ifdef XP_QUAD
using CalcReal = __float128;
#else
using CalcReal = long double;
#endif

// The arithmetic, apart from the window
class CalcEngine
{
public:
    enum Radix { Hex = 16, Dec = 10, Oct = 8, Bin = 2 };
    enum Angle { Degrees, Radians, Grads };

    void setScientific(bool on) { m_scientific = on; }
    void setRadix(Radix radix);
    Radix radix() const { return m_radix; }
    void setWordBits(int bits);
    int wordBits() const { return m_bits; }
    void setAngle(Angle angle) { m_angle = angle; }
    Angle angle() const { return m_angle; }
    void setGrouping(bool on) { m_grouping = on; }
    bool grouping() const { return m_grouping; }

    // A button's command (calc.exe's IDs); INV and HYP are the check boxes' state.
    void command(int id, bool inv, bool hyp);
    QString display() const;
    bool memory() const { return m_memory != 0; }
    int parentheses() const { return m_frames.size() - 1; }
    bool error() const { return !m_error.isEmpty(); }
    void setError(const QString &text) { m_error = text; }
    // The Statistics Box's numbers, as the display writes them
    QList<CalcReal> &statistics() { return m_data; }
    QString format(CalcReal value) const;
    CalcReal value() const;
    void setValue(CalcReal value);

    // calc.exe's error messages, by their string ID
    QHash<int, QString> messages;

private:
    struct Pending
    {
        CalcReal operand;
        int op;
    };
    void commitEntry();
    void binary(int op);
    CalcReal apply(CalcReal left, int op, CalcReal right);
    CalcReal unary(int id, CalcReal x, bool inv, bool hyp);
    void resolve(QList<Pending> &frame, int precedence);
    int precedence(int op) const;
    CalcReal wrap(CalcReal value) const;
    CalcReal toRadians(CalcReal x) const;
    CalcReal fromRadians(CalcReal x) const;
    void fail(int message);

    bool m_scientific = false, m_grouping = false, m_fixedExponent = false;
    Radix m_radix = Dec;
    int m_bits = 64;
    Angle m_angle = Degrees;

    CalcReal m_value = 0, m_memory = 0;
    QString m_entry;            // the digits being typed, in the radix
    bool m_entering = false, m_exponent = false;
    int m_last = 0;             // the last command
    QList<QList<Pending>> m_frames = {{}};
    int m_repeatOp = 0;
    CalcReal m_repeatOperand = 0;
    QString m_error;
    QList<CalcReal> m_data;
};

class Calculator : public QMainWindow
{
public:
    Calculator();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void build();
    void press(int id, bool fromKey = false);
    void refresh();
    void updateEnabled();
    void showStatistics();
    void refreshStatistics();
    void paste();

    std::optional<w4w::PeResources> m_resources;
    CalcEngine m_engine;
    bool m_scientific = false;
    QHash<int, QWidget *> m_controls;
    QHash<int, QAction *> m_actions;
    QList<xpprogram::Accelerator> m_keys;
    QLineEdit *m_display = nullptr;
    QLabel *m_memoryBox = nullptr, *m_parenthesesBox = nullptr;
    QCheckBox *m_inv = nullptr, *m_hyp = nullptr;
    QPointer<QDialog> m_statistics;
    QListWidget *m_statisticsList = nullptr;
    QLabel *m_statisticsCount = nullptr;
};
