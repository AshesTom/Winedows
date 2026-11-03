// SPDX-License-Identifier: LGPL-2.1-or-later
#include "calculator.h"

#include "applets.h"
#include "theme.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef XP_QUAD
#include <quadmath.h>
#endif

namespace {

// calc.exe's commands: its buttons, then its menu
enum {
    IDC_SIGN = 80, IDC_CLEAR = 81, IDC_CENTR = 82, IDC_BACK = 83, IDC_STAT = 84, IDC_PNT = 85,
    IDC_AND = 86, IDC_OR = 87, IDC_XOR = 88, IDC_LSHF = 89, IDC_DIV = 90, IDC_MUL = 91, IDC_ADD = 92,
    IDC_SUB = 93, IDC_MOD = 94, IDC_PWR = 95, IDC_CHOP = 96, IDC_COM = 97, IDC_SIN = 98, IDC_COS = 99,
    IDC_TAN = 100, IDC_LN = 101, IDC_LOG = 102, IDC_SQRT = 103, IDC_SQR = 104, IDC_CUB = 105, IDC_FAC = 106,
    IDC_REC = 107, IDC_DMS = 108, IDC_PERCENT = 109, IDC_FE = 110, IDC_PI = 111, IDC_EQU = 112,
    IDC_MCLEAR = 113, IDC_RECALL = 114, IDC_STORE = 115, IDC_MPLUS = 116, IDC_EXP = 117, IDC_AVE = 118,
    IDC_SUM = 119, IDC_DEV = 120, IDC_DATA = 121, IDC_OPENP = 122, IDC_CLOSEP = 123, IDC_0 = 124, IDC_F = 139,
    IDC_INV = 140, IDC_HYP = 141,
    IDM_COPY = 300, IDM_PASTE = 301, IDM_ABOUT = 302, IDM_GROUPING = 303, IDM_SCIENTIFIC = 304,
    IDM_STANDARD = 305, IDM_HEX = 306, IDM_DEC = 307, IDM_OCT = 308, IDM_BIN = 309, IDM_QWORD = 310,
    IDM_DWORD = 311, IDM_WORD = 312, IDM_BYTE = 313, IDM_DEG = 314, IDM_RAD = 315, IDM_GRAD = 316,
    IDM_HELPTOPICS = 317,
    // Inv's operators, of this file's own
    OP_RSH = 1089, OP_ROOT = 1095,
    // The display, the memory and parentheses boxes, the control that marks the dialog's height
    IDC_DISPLAY = 403, IDC_MEMTEXT = 401, IDC_PARTEXT = 402, IDC_SIZE = 1000,
    // The Statistics Box
    IDD_STATISTICS = 103, IDC_STATLIST = 407, IDC_CD = 404, IDC_CAD = 405, IDC_NUMTEXT = 408,
    IDC_LOAD = 410, IDC_RET = 411,
};
// and strings
enum {
    IDS_DIVIDE_BY_ZERO = 71, IDS_INVALID_INPUT = 72, IDS_UNDEFINED = 73, IDS_POSITIVE_INFINITY = 74,
    IDS_NEGATIVE_INFINITY = 75, IDS_CALCULATOR = 83,
};

// A number written with "." as the C library reads it, whose point is LC_NUMERIC's
QByteArray cNumber(const QString &text)
{
    QByteArray out = text.toLatin1();
    out.replace('.', localeconv()->decimal_point);
    return out;
}

#ifdef XP_QUAD
const int PRECISION = 32;
CalcReal r_abs(CalcReal x) { return fabsq(x); }
CalcReal r_trunc(CalcReal x) { return truncq(x); }
CalcReal r_fmod(CalcReal x, CalcReal y) { return fmodq(x, y); }
CalcReal r_pow(CalcReal x, CalcReal y) { return powq(x, y); }
CalcReal r_sqrt(CalcReal x) { return sqrtq(x); }
CalcReal r_cbrt(CalcReal x) { return cbrtq(x); }
CalcReal r_exp(CalcReal x) { return expq(x); }
CalcReal r_log(CalcReal x) { return logq(x); }
CalcReal r_log10(CalcReal x) { return log10q(x); }
CalcReal r_sin(CalcReal x) { return sinq(x); }
CalcReal r_cos(CalcReal x) { return cosq(x); }
CalcReal r_tan(CalcReal x) { return tanq(x); }
CalcReal r_asin(CalcReal x) { return asinq(x); }
CalcReal r_acos(CalcReal x) { return acosq(x); }
CalcReal r_atan(CalcReal x) { return atanq(x); }
CalcReal r_sinh(CalcReal x) { return sinhq(x); }
CalcReal r_cosh(CalcReal x) { return coshq(x); }
CalcReal r_tanh(CalcReal x) { return tanhq(x); }
CalcReal r_asinh(CalcReal x) { return asinhq(x); }
CalcReal r_acosh(CalcReal x) { return acoshq(x); }
CalcReal r_atanh(CalcReal x) { return atanhq(x); }
CalcReal r_gamma(CalcReal x) { return tgammaq(x); }
bool r_finite(CalcReal x) { return finiteq(x); }
bool r_nan(CalcReal x) { return isnanq(x); }
const CalcReal PI = M_PIq;
CalcReal parse(const QString &text) { return strtoflt128(cNumber(text).constData(), nullptr); }
QString scientific(CalcReal value, int digits)
{
    char buffer[96];
    quadmath_snprintf(buffer, sizeof buffer, "%.*Qe", digits - 1, value);
    return QString::fromLatin1(buffer);
}
#else
const int PRECISION = 18;
CalcReal r_abs(CalcReal x) { return fabsl(x); }
CalcReal r_trunc(CalcReal x) { return truncl(x); }
CalcReal r_fmod(CalcReal x, CalcReal y) { return fmodl(x, y); }
CalcReal r_pow(CalcReal x, CalcReal y) { return powl(x, y); }
CalcReal r_sqrt(CalcReal x) { return sqrtl(x); }
CalcReal r_cbrt(CalcReal x) { return cbrtl(x); }
CalcReal r_exp(CalcReal x) { return expl(x); }
CalcReal r_log(CalcReal x) { return logl(x); }
CalcReal r_log10(CalcReal x) { return log10l(x); }
CalcReal r_sin(CalcReal x) { return sinl(x); }
CalcReal r_cos(CalcReal x) { return cosl(x); }
CalcReal r_tan(CalcReal x) { return tanl(x); }
CalcReal r_asin(CalcReal x) { return asinl(x); }
CalcReal r_acos(CalcReal x) { return acosl(x); }
CalcReal r_atan(CalcReal x) { return atanl(x); }
CalcReal r_sinh(CalcReal x) { return sinhl(x); }
CalcReal r_cosh(CalcReal x) { return coshl(x); }
CalcReal r_tanh(CalcReal x) { return tanhl(x); }
CalcReal r_asinh(CalcReal x) { return asinhl(x); }
CalcReal r_acosh(CalcReal x) { return acoshl(x); }
CalcReal r_atanh(CalcReal x) { return atanhl(x); }
CalcReal r_gamma(CalcReal x) { return tgammal(x); }
bool r_finite(CalcReal x) { return std::isfinite(x); }
bool r_nan(CalcReal x) { return std::isnan(x); }
const CalcReal PI = 3.14159265358979323846264338327950288L;
CalcReal parse(const QString &text) { return strtold(cNumber(text).constData(), nullptr); }
QString scientific(CalcReal value, int digits)
{
    char buffer[96];
    snprintf(buffer, sizeof buffer, "%.*Le", digits - 1, value);
    return QString::fromLatin1(buffer);
}
#endif

// A whole number's 64 bits, as two's complement wraps it
qint64 bits64(CalcReal x)
{
    const CalcReal span = CalcReal(18446744073709551616.0L);
    CalcReal whole = r_fmod(r_trunc(x), span);
    if (whole < 0)
        whole += span;
    return qint64(quint64(whole));
}

bool isDigit(int id) { return id >= IDC_0 && id <= IDC_F; }
bool isBinary(int id)
{
    return (id >= IDC_AND && id <= IDC_PWR) || id == OP_RSH || id == OP_ROOT;
}

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/calc.conf");
}

// Groups of digits from the right: the locale's separator by three in decimal, spaces by four in
// hexadecimal and binary, by three in octal
QString group(const QString &digits, int radix)
{
    const int size = radix == 10 || radix == 8 ? 3 : 4;
    const QString separator = radix == 10 ? QLocale::system().groupSeparator() : QStringLiteral(" ");
    QString out;
    for (int i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % size == 0)
            out += separator;
        out += digits[i];
    }
    return out;
}

// DrawEdge's EDGE_SUNKEN around a widget, which a static with WS_EX_CLIENTEDGE has even under a visual style
class SunkenEdge : public QWidget
{
public:
    explicit SunkenEdge(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        ShellTheme *theme = ShellTheme::instance();
        QPainter painter(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        const auto edge = [&](const QRect &box, const char *topLeft, const QColor &a, const char *bottomRight, const QColor &b) {
            painter.setPen(theme->system(QLatin1String(topLeft), a));
            painter.drawLine(box.topLeft(), box.topRight() - QPoint(1, 0));
            painter.drawLine(box.topLeft(), box.bottomLeft() - QPoint(0, 1));
            painter.setPen(theme->system(QLatin1String(bottomRight), b));
            painter.drawLine(box.bottomLeft(), box.bottomRight());
            painter.drawLine(box.topRight(), box.bottomRight());
        };
        edge(r, "btnshadow", QColor(172, 168, 153), "btnhighlight", Qt::white);
        edge(r.adjusted(1, 1, -1, -1), "dkshadow3d", QColor(113, 111, 100), "light3d", QColor(241, 239, 226));
    }
};

// A one-pixel outline over a widget, letting the mouse through
class EdgeRing : public QWidget
{
public:
    EdgeRing(const QColor &colour, QWidget *parent)
        : QWidget(parent)
        , m_colour(colour)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(m_colour);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QColor m_colour;
};

} // namespace

// The arithmetic

void CalcEngine::setRadix(Radix radix)
{
    commitEntry();
    m_radix = radix;
    m_value = wrap(m_value);
}

void CalcEngine::setWordBits(int bits)
{
    commitEntry();
    m_bits = bits;
    m_value = wrap(m_value);
}

CalcReal CalcEngine::wrap(CalcReal value) const
{
    // Hexadecimal, octal and binary hold whole numbers of the word size, in two's complement
    if (m_radix == Dec || !r_finite(value))
        return value;
    quint64 bits = quint64(bits64(value));
    const quint64 mask = m_bits == 64 ? ~quint64(0) : (quint64(1) << m_bits) - 1;
    bits &= mask;
    if (m_bits < 64 && (bits >> (m_bits - 1)) & 1)
        bits |= ~mask;
    return CalcReal(qint64(bits));
}

CalcReal CalcEngine::value() const
{
    if (!m_entering)
        return m_value;
    if (m_radix != Dec)
        return wrap(CalcReal(m_entry.toULongLong(nullptr, m_radix)));
    QString text = m_entry;
    if (text.endsWith(QLatin1Char('+')) || text.endsWith(QLatin1Char('-')))
        text += QLatin1Char('0');
    return parse(text);
}

void CalcEngine::setValue(CalcReal value)
{
    m_value = wrap(value);
    m_entering = false;
    m_error.clear();
}

void CalcEngine::commitEntry()
{
    if (!m_entering)
        return;
    m_value = value();
    m_entering = false;
    m_exponent = false;
}

void CalcEngine::fail(int message)
{
    m_error = messages.value(message);
    m_frames = {{}};
    m_entering = false;
    m_value = 0;
}

int CalcEngine::precedence(int op) const
{
    // Standard works each operator out as it comes; Scientific has XP's order
    if (!m_scientific)
        return 1;
    switch (op) {
    case IDC_OR: case IDC_XOR: return 1;
    case IDC_AND: return 2;
    case IDC_LSHF: case OP_RSH: return 3;
    case IDC_ADD: case IDC_SUB: return 4;
    case IDC_MUL: case IDC_DIV: case IDC_MOD: return 5;
    default: return 6;
    }
}

CalcReal CalcEngine::apply(CalcReal left, int op, CalcReal right)
{
    const bool whole = m_radix != Dec;
    const auto integer = bits64;
    CalcReal out = 0;
    switch (op) {
    case IDC_ADD: out = left + right; break;
    case IDC_SUB: out = left - right; break;
    case IDC_MUL: out = left * right; break;
    case IDC_DIV:
        if (right == 0) {
            fail(IDS_DIVIDE_BY_ZERO);
            return 0;
        }
        out = whole ? r_trunc(left / right) : left / right;
        break;
    case IDC_MOD:
        if (right == 0) {
            fail(IDS_DIVIDE_BY_ZERO);
            return 0;
        }
        out = r_fmod(whole ? r_trunc(left) : left, whole ? r_trunc(right) : right);
        break;
    case IDC_PWR:
        out = r_pow(left, right);
        break;
    case OP_ROOT:
        if (right == 0) {
            fail(IDS_INVALID_INPUT);
            return 0;
        }
        // An odd root of a negative number is negative
        if (left < 0 && r_fmod(right, 2) != 0 && r_trunc(right) == right)
            out = -r_pow(-left, 1 / right);
        else
            out = r_pow(left, 1 / right);
        break;
    case IDC_AND: out = CalcReal(integer(left) & integer(right)); break;
    case IDC_OR: out = CalcReal(integer(left) | integer(right)); break;
    case IDC_XOR: out = CalcReal(integer(left) ^ integer(right)); break;
    case IDC_LSHF:
    case OP_RSH: {
        const qint64 by = integer(right);
        if (by < 0 || by > 63)
            out = op == IDC_LSHF ? 0 : (integer(left) < 0 ? -1 : 0);
        else
            out = op == IDC_LSHF ? CalcReal(qint64(quint64(integer(left)) << by)) : CalcReal(integer(left) >> by);
        break;
    }
    }
    if (r_nan(out)) {
        fail(IDS_INVALID_INPUT);
        return 0;
    }
    if (!r_finite(out)) {
        fail(out > 0 ? IDS_POSITIVE_INFINITY : IDS_NEGATIVE_INFINITY);
        return 0;
    }
    return wrap(out);
}

void CalcEngine::resolve(QList<Pending> &frame, int above)
{
    while (!frame.isEmpty() && precedence(frame.last().op) >= above && m_error.isEmpty()) {
        // (A failure clears the frames, this one with them)
        const Pending pending = frame.takeLast();
        m_value = apply(pending.operand, pending.op, m_value);
    }
}

void CalcEngine::binary(int op)
{
    commitEntry();
    QList<Pending> &frame = m_frames.last();
    // A second operator in a row takes the first one's place
    if (isBinary(m_last) && !frame.isEmpty()) {
        frame.last().op = op;
        return;
    }
    resolve(frame, precedence(op));
    if (m_error.isEmpty())
        m_frames.last().append({m_value, op});
}

CalcReal CalcEngine::toRadians(CalcReal x) const
{
    return m_angle == Degrees ? x * PI / 180 : m_angle == Grads ? x * PI / 200 : x;
}

CalcReal CalcEngine::fromRadians(CalcReal x) const
{
    return m_angle == Degrees ? x * 180 / PI : m_angle == Grads ? x * 200 / PI : x;
}

CalcReal CalcEngine::unary(int id, CalcReal x, bool inv, bool hyp)
{
    CalcReal out = 0;
    switch (id) {
    case IDC_SIN:
    case IDC_COS:
    case IDC_TAN:
        if (hyp) {
            if (inv && id == IDC_COS && x < 1)
                return fail(IDS_INVALID_INPUT), 0;
            if (inv && id == IDC_TAN && r_abs(x) >= 1)
                return fail(IDS_INVALID_INPUT), 0;
            out = id == IDC_SIN ? (inv ? r_asinh(x) : r_sinh(x)) : id == IDC_COS ? (inv ? r_acosh(x) : r_cosh(x)) : (inv ? r_atanh(x) : r_tanh(x));
        } else if (inv) {
            if (id != IDC_TAN && r_abs(x) > 1)
                return fail(IDS_INVALID_INPUT), 0;
            out = fromRadians(id == IDC_SIN ? r_asin(x) : id == IDC_COS ? r_acos(x) : r_atan(x));
        } else {
            // Right angles exactly, as XP's rational arithmetic has them
            const CalcReal quarter = m_angle == Degrees ? 90 : m_angle == Grads ? 100 : 0;
            if (quarter != 0 && r_fmod(x, quarter) == 0) {
                const int k = int(r_fmod(r_fmod(x / quarter, 4) + 4, 4));
                static const int sines[] = {0, 1, 0, -1}, cosines[] = {1, 0, -1, 0};
                if (id == IDC_TAN && (k == 1 || k == 3))
                    return fail(IDS_INVALID_INPUT), 0;
                out = id == IDC_SIN ? sines[k] : id == IDC_COS ? cosines[k] : 0;
            } else {
                const CalcReal radians = toRadians(x);
                out = id == IDC_SIN ? r_sin(radians) : id == IDC_COS ? r_cos(radians) : r_tan(radians);
                // What is left of a multiple of pi is noise at this precision
                if (r_abs(out) < r_pow(10, -PRECISION) && r_abs(radians) > 1)
                    out = 0;
            }
        }
        break;
    case IDC_LN:
        if (!inv && x <= 0)
            return fail(IDS_INVALID_INPUT), 0;
        out = inv ? r_exp(x) : r_log(x);
        break;
    case IDC_LOG:
        if (!inv && x <= 0)
            return fail(IDS_INVALID_INPUT), 0;
        out = inv ? r_pow(10, x) : r_log10(x);
        break;
    case IDC_SQRT:
        if (x < 0)
            return fail(IDS_INVALID_INPUT), 0;
        out = r_sqrt(x);
        break;
    case IDC_SQR:
        if (inv && x < 0)
            return fail(IDS_INVALID_INPUT), 0;
        out = inv ? r_sqrt(x) : x * x;
        break;
    case IDC_CUB:
        out = inv ? r_cbrt(x) : x * x * x;
        break;
    case IDC_FAC:
        if (x < 0 && r_trunc(x) == x)
            return fail(IDS_INVALID_INPUT), 0;
        out = r_gamma((m_radix == Dec ? x : r_trunc(x)) + 1);
        if (r_trunc(x) == x)
            out = r_trunc(out + CalcReal(0.5));
        break;
    case IDC_REC:
        if (x == 0)
            return fail(IDS_DIVIDE_BY_ZERO), 0;
        out = 1 / x;
        break;
    case IDC_DMS: {
        const CalcReal degrees = r_trunc(x);
        if (inv) {
            // d.mmss to degrees
            const CalcReal minutes = r_trunc((x - degrees) * 100);
            const CalcReal seconds = ((x - degrees) * 100 - minutes) * 100;
            out = degrees + minutes / 60 + seconds / 3600;
        } else {
            const CalcReal minutes = (x - degrees) * 60;
            const CalcReal seconds = (minutes - r_trunc(minutes)) * 60;
            out = degrees + r_trunc(minutes) / 100 + seconds / 10000;
        }
        break;
    }
    case IDC_CHOP:
        out = inv ? x - r_trunc(x) : r_trunc(x);
        break;
    case IDC_COM:
        out = CalcReal(~bits64(x));
        break;
    }
    if (r_nan(out))
        return fail(IDS_INVALID_INPUT), 0;
    if (!r_finite(out))
        return fail(out > 0 ? IDS_POSITIVE_INFINITY : IDS_NEGATIVE_INFINITY), 0;
    return wrap(out);
}

void CalcEngine::command(int id, bool inv, bool hyp)
{
    if (!m_error.isEmpty()) {
        // An error stays until C, CE or a new number
        if (id != IDC_CLEAR && id != IDC_CENTR && !isDigit(id) && id != IDC_PNT)
            return;
        m_error.clear();
        m_frames = {{}};
        m_value = 0;
    }
    if (isDigit(id)) {
        const int digit = id - IDC_0;
        if (digit >= m_radix)
            return;
        if (!m_entering) {
            m_entry = QStringLiteral("0");
            m_entering = true;
            m_exponent = false;
        }
        const QChar character = QLatin1Char("0123456789ABCDEF"[digit]);
        if (m_exponent) {
            const int at = m_entry.indexOf(QLatin1Char('e'));
            if (m_entry.size() - at - 2 < 4)
                m_entry += character;
        } else {
            const bool negative = m_entry.startsWith(QLatin1Char('-'));
            QString next = m_entry.mid(negative ? 1 : 0);
            if (next == QLatin1String("0"))
                next.clear();
            next += character;
            int digits = 0;
            for (QChar c : std::as_const(next))
                digits += c != QLatin1Char('.');
            // Thirty-two digits in decimal; in the others, what the word holds
            const bool fits = m_radix == Dec ? digits <= PRECISION
                                             : (m_bits == 64 ? next.size() <= (m_radix == Hex ? 16 : m_radix == Oct ? 22 : 64)
                                                             : next.toULongLong(nullptr, m_radix) < (quint64(1) << m_bits))
                                                   && (m_radix != Oct || m_bits != 64 || next.size() < 22 || next[0] <= QLatin1Char('1'));
            if (fits)
                m_entry = (negative ? QStringLiteral("-") : QString()) + next;
        }
        m_last = id;
        return;
    }
    switch (id) {
    case IDC_PNT:
        if (m_radix != Dec)
            return;
        if (!m_entering) {
            m_entry = QStringLiteral("0");
            m_entering = true;
            m_exponent = false;
        }
        if (!m_exponent && !m_entry.contains(QLatin1Char('.')))
            m_entry += QLatin1Char('.');
        break;
    case IDC_EXP:
        if (m_radix != Dec)
            return;
        if (!m_entering) {
            m_entry = QStringLiteral("0");
            m_entering = true;
        }
        if (!m_exponent) {
            m_entry += QStringLiteral("e+");
            m_exponent = true;
        }
        break;
    case IDC_SIGN:
        if (m_entering) {
            if (m_exponent) {
                const int at = m_entry.indexOf(QLatin1Char('e')) + 1;
                m_entry[at] = m_entry[at] == QLatin1Char('+') ? QLatin1Char('-') : QLatin1Char('+');
            } else if (m_entry.startsWith(QLatin1Char('-'))) {
                m_entry.remove(0, 1);
            } else {
                m_entry.prepend(QLatin1Char('-'));
            }
            return;
        }
        m_value = wrap(-m_value);
        break;
    case IDC_BACK:
        if (!m_entering)
            return;
        if (m_exponent && (m_entry.endsWith(QLatin1Char('+')) || m_entry.endsWith(QLatin1Char('-')))) {
            m_entry.chop(2);
            m_exponent = false;
        } else {
            m_entry.chop(1);
        }
        if (m_entry.isEmpty() || m_entry == QLatin1String("-"))
            m_entry = QStringLiteral("0");
        return;
    case IDC_CENTR:
        m_entry = QStringLiteral("0");
        m_entering = true;
        m_exponent = false;
        break;
    case IDC_CLEAR:
        m_entering = false;
        m_exponent = false;
        m_value = 0;
        m_frames = {{}};
        m_repeatOp = 0;
        break;
    case IDC_EQU: {
        commitEntry();
        // "=" again does the last operation again, with its number
        bool pending = false;
        for (const QList<Pending> &frame : std::as_const(m_frames))
            pending = pending || !frame.isEmpty();
        if (pending) {
            m_repeatOp = m_frames.last().isEmpty() ? m_repeatOp : m_frames.last().last().op;
            m_repeatOperand = m_value;
            while (m_error.isEmpty()) {
                resolve(m_frames.last(), 0);
                if (m_frames.size() == 1 || !m_error.isEmpty())
                    break;
                m_frames.removeLast();
            }
        } else if (m_repeatOp) {
            m_value = apply(m_value, m_repeatOp, m_repeatOperand);
        }
        m_frames = {{}};
        break;
    }
    case IDC_OPENP:
        // XP's 25 levels
        if (m_frames.size() <= 25) {
            commitEntry();
            m_frames.append(QList<Pending>());
        }
        break;
    case IDC_CLOSEP:
        if (m_frames.size() > 1) {
            commitEntry();
            resolve(m_frames.last(), 0);
            if (m_error.isEmpty())
                m_frames.removeLast();
        }
        break;
    case IDC_STORE:
        commitEntry();
        m_memory = m_value;
        break;
    case IDC_RECALL:
        m_value = m_memory;
        m_entering = false;
        break;
    case IDC_MCLEAR:
        m_memory = 0;
        break;
    case IDC_MPLUS:
        commitEntry();
        m_memory = wrap(m_memory + m_value);
        break;
    case IDC_PI:
        m_value = inv ? 2 * PI : PI;
        m_entering = false;
        break;
    case IDC_FE:
        m_fixedExponent = !m_fixedExponent;
        break;
    case IDC_PERCENT:
        commitEntry();
        m_value = m_frames.last().isEmpty() ? 0 : m_frames.last().last().operand * m_value / 100;
        break;
    case IDC_DATA:
        commitEntry();
        m_data.append(m_value);
        break;
    case IDC_AVE:
    case IDC_SUM:
    case IDC_DEV: {
        commitEntry();
        CalcReal sum = 0, squares = 0;
        for (CalcReal x : std::as_const(m_data)) {
            sum += x;
            squares += x * x;
        }
        const int n = m_data.size();
        if (id == IDC_SUM)
            m_value = inv ? squares : sum;
        else if (id == IDC_AVE)
            m_value = n ? (inv ? squares : sum) / n : 0;
        else {
            // s: the sample's, n - 1; Inv: the population's
            const int over = inv ? n : n - 1;
            m_value = over > 0 ? r_sqrt(qMax(CalcReal(0), (squares - sum * sum / n) / over)) : 0;
        }
        m_value = wrap(m_value);
        break;
    }
    case IDC_AND: case IDC_OR: case IDC_XOR: case IDC_DIV: case IDC_MUL: case IDC_ADD: case IDC_SUB: case IDC_MOD:
        binary(id);
        break;
    case IDC_LSHF:
        binary(inv ? OP_RSH : IDC_LSHF);
        id = inv ? OP_RSH : IDC_LSHF;
        break;
    case IDC_PWR:
        binary(inv ? OP_ROOT : IDC_PWR);
        id = inv ? OP_ROOT : IDC_PWR;
        break;
    default:
        commitEntry();
        m_value = unary(id, m_value, inv, hyp);
        break;
    }
    m_last = id;
}

QString CalcEngine::format(CalcReal value) const
{
    if (m_radix != Dec) {
        const quint64 mask = m_bits == 64 ? ~quint64(0) : (quint64(1) << m_bits) - 1;
        const QString digits = QString::number(quint64(qint64(wrap(value))) & mask, m_radix).toUpper();
        return m_grouping ? group(digits, m_radix) : digits;
    }
    if (r_nan(value))
        return messages.value(IDS_UNDEFINED);
    if (!r_finite(value))
        return messages.value(value > 0 ? IDS_POSITIVE_INFINITY : IDS_NEGATIVE_INFINITY);
    const QString point = QLocale::system().decimalPoint();
    if (value == 0)
        return QStringLiteral("0") + point;
    // XP's 32 significant digits: fixed while they fit in 32 places, otherwise with an exponent
    const QString written = scientific(r_abs(value), PRECISION);
    const int e = written.indexOf(QLatin1Char('e'));
    QString digits;
    for (QChar c : written.left(e))
        if (c.isDigit())
            digits += c;
    const int exponent = written.mid(e + 1).toInt();
    while (digits.size() > 1 && digits.endsWith(QLatin1Char('0')))
        digits.chop(1);
    const int n = digits.size();
    const int places = (exponent >= 0 ? exponent + 1 : 0) + qMax(0, n - 1 - exponent);
    QString out;
    if (!m_fixedExponent && exponent < PRECISION && places <= PRECISION) {
        QString whole, fraction;
        if (exponent >= 0) {
            whole = digits.left(exponent + 1).leftJustified(exponent + 1, QLatin1Char('0'));
            fraction = digits.mid(exponent + 1);
        } else {
            whole = QStringLiteral("0");
            fraction = QString(-exponent - 1, QLatin1Char('0')) + digits;
        }
        out = (m_grouping ? group(whole, 10) : whole) + point + fraction;
    } else {
        out = digits.left(1) + point + digits.mid(1) + QLatin1Char('e') + (exponent < 0 ? QLatin1Char('-') : QLatin1Char('+')) + QString::number(qAbs(exponent));
    }
    return value < 0 ? QLatin1Char('-') + out : out;
}

QString CalcEngine::display() const
{
    if (!m_error.isEmpty())
        return m_error;
    if (!m_entering)
        return format(m_value);
    // What is being typed, as typed: decimal always shows its point
    if (m_radix != Dec)
        return m_grouping ? group(m_entry, m_radix) : m_entry;
    const bool negative = m_entry.startsWith(QLatin1Char('-'));
    QString mantissa = m_entry.mid(negative ? 1 : 0);
    QString exponent;
    if (const int at = mantissa.indexOf(QLatin1Char('e')); at >= 0) {
        exponent = mantissa.mid(at);
        mantissa.truncate(at);
    }
    QString whole = mantissa, fraction;
    if (const int at = mantissa.indexOf(QLatin1Char('.')); at >= 0) {
        whole = mantissa.left(at);
        fraction = mantissa.mid(at + 1);
    }
    return (negative ? QStringLiteral("-") : QString()) + (m_grouping ? group(whole, 10) : whole) + QLocale::system().decimalPoint() + fraction + exponent;
}

// The window

Calculator::Calculator()
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("calc.exe"))))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (!m_resources)
        return;
    setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, QStringLiteral("SC"), 32))));
    for (int id : {IDS_DIVIDE_BY_ZERO, IDS_INVALID_INPUT, IDS_UNDEFINED, IDS_POSITIVE_INFINITY, IDS_NEGATIVE_INFINITY})
        m_engine.messages.insert(id, xpfiles::string(*m_resources, id));
    m_keys = xpprogram::accelerators(*m_resources, QStringLiteral("105"));
    // XP keeps the view and Digit grouping (win.ini's [SciCalc] layout and UseSep)
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_scientific = settings.value(QStringLiteral("layout"), 1).toInt() == 0;
    m_engine.setGrouping(settings.value(QStringLiteral("UseSep"), false).toBool());
    build();
}

void Calculator::build()
{
    m_engine.setScientific(m_scientific);
    // Standard has only decimal
    if (!m_scientific && m_engine.radix() != CalcEngine::Dec)
        m_engine.setRadix(CalcEngine::Dec);
    auto *central = new QWidget(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, m_scientific ? 101 : 102, central);
    setWindowTitle(page.caption);
    m_controls = page.controls;
    // calc.exe sizes its window to the 1-unit control at the bottom
    if (QWidget *marker = m_controls.value(IDC_SIZE))
        central->setFixedSize(central->width(), marker->geometry().bottom() + 1);

    menuBar()->clear();
    const int menu = !m_scientific ? 106 : m_engine.radix() == CalcEngine::Dec ? 107 : 108;
    m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, menu);
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const int id = it.key();
        connect(it.value(), &QAction::triggered, this, [this, id] { press(id); });
    }
    // Help Topics is calc.chm, which nothing here can show
    if (QAction *help = m_actions.value(IDM_HELPTOPICS))
        help->setEnabled(false);
    const auto check = [this](int id, bool on) {
        if (QAction *action = m_actions.value(id)) {
            action->setCheckable(true);
            action->setChecked(on);
        }
    };
    // CheckMenuRadioItem's dot for each family
    for (const QList<int> &family : {QList<int>{IDM_STANDARD, IDM_SCIENTIFIC}, QList<int>{IDM_HEX, IDM_DEC, IDM_OCT, IDM_BIN},
                                     QList<int>{IDM_QWORD, IDM_DWORD, IDM_WORD, IDM_BYTE}, QList<int>{IDM_DEG, IDM_RAD, IDM_GRAD}}) {
        auto *group = new QActionGroup(menuBar());
        for (int id : family)
            if (QAction *action = m_actions.value(id))
                group->addAction(action);
    }
    check(IDM_STANDARD, !m_scientific);
    check(IDM_SCIENTIFIC, m_scientific);
    check(IDM_GROUPING, m_engine.grouping());
    check(IDM_HEX, m_engine.radix() == CalcEngine::Hex);
    check(IDM_DEC, m_engine.radix() == CalcEngine::Dec);
    check(IDM_OCT, m_engine.radix() == CalcEngine::Oct);
    check(IDM_BIN, m_engine.radix() == CalcEngine::Bin);
    check(IDM_QWORD, m_engine.wordBits() == 64);
    check(IDM_DWORD, m_engine.wordBits() == 32);
    check(IDM_WORD, m_engine.wordBits() == 16);
    check(IDM_BYTE, m_engine.wordBits() == 8);
    check(IDM_DEG, m_engine.angle() == CalcEngine::Degrees);
    check(IDM_RAD, m_engine.angle() == CalcEngine::Radians);
    check(IDM_GRAD, m_engine.angle() == CalcEngine::Grads);

    // The keys go to the window, as calc.exe's accelerators, not to a button
    for (QWidget *control : central->findChildren<QWidget *>())
        control->setFocusPolicy(Qt::NoFocus);
    m_display = qobject_cast<QLineEdit *>(m_controls.value(IDC_DISPLAY));
    if (m_display) {
        m_display->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // Read-only, yet white as calc.exe paints it: an edit that takes neither the keyboard nor the mouse
        m_display->setReadOnly(false);
        // and a read-only edit's fill in the pixel inside the border, which calc.exe's white leaves
        if (const w4w::Renderer *renderer = ShellTheme::instance()->renderer()) {
            const QColor fill = renderer->colour(w4w::Part{QStringLiteral("edit"), QStringLiteral("edittext"), QStringLiteral("readonly"), 6, {}},
                                                 QStringLiteral("fillcolor")).value_or(QColor(235, 235, 228));
            auto *ring = new EdgeRing(fill, m_display);
            ring->setGeometry(m_display->rect().adjusted(1, 1, -1, -1));
        }
        m_display->setContextMenuPolicy(Qt::NoContextMenu);
        m_display->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    const auto sunkenBox = [this](int id) {
        auto *box = qobject_cast<QLabel *>(m_controls.value(id));
        if (box) {
            // WS_EX_CLIENTEDGE on a static: the classic sunken edge
            box->setFrameStyle(QFrame::NoFrame);
            box->setContentsMargins(2, 2, 2, 2);
            (new SunkenEdge(box))->setGeometry(box->rect());
            box->setAlignment(Qt::AlignCenter);
            box->setWordWrap(false);
        }
        return box;
    };
    m_memoryBox = sunkenBox(IDC_MEMTEXT);
    m_parenthesesBox = sunkenBox(IDC_PARTEXT);
    m_inv = qobject_cast<QCheckBox *>(m_controls.value(IDC_INV));
    m_hyp = qobject_cast<QCheckBox *>(m_controls.value(IDC_HYP));

    // The buttons' colours as calc.exe paints their words: numbers blue, operators and the memory
    // red, Scientific's functions magenta
    const QColor blue(0, 0, 255), red(255, 0, 0), magenta(255, 0, 255);
    for (auto it = m_controls.cbegin(); it != m_controls.cend(); ++it) {
        const int id = it.key();
        auto *button = qobject_cast<QAbstractButton *>(it.value());
        if (!button)
            continue;
        connect(button, &QAbstractButton::clicked, this, [this, id] { press(id); });
        if (!qobject_cast<QPushButton *>(button))
            continue;
        // calc.exe writes its buttons' words from its strings, the command less 80 ("sqrt" where the template has "sqt")
        if (id >= IDC_SIGN && id <= IDC_F)
            if (const QString words = xpfiles::string(*m_resources, id - IDC_SIGN); !words.isEmpty())
                button->setText(words);
        QColor colour;
        if (isDigit(id) || id == IDC_SIGN || id == IDC_PNT || id == IDC_STAT || (id >= IDC_AVE && id <= IDC_DATA) || id == IDC_PI
            || id == IDC_SQRT || id == IDC_PERCENT || (id == IDC_REC && !m_scientific))
            colour = blue;
        else if ((id >= IDC_MCLEAR && id <= IDC_MPLUS) || (id >= IDC_AND && id <= IDC_MOD) || id == IDC_EQU || id == IDC_CLEAR
                 || id == IDC_CENTR || id == IDC_BACK || id == IDC_CHOP || id == IDC_COM)
            colour = red;
        else
            colour = magenta;
        QPalette palette = button->palette();
        palette.setColor(QPalette::Active, QPalette::ButtonText, colour);
        palette.setColor(QPalette::Inactive, QPalette::ButtonText, colour);
        button->setPalette(palette);
    }
    // The three families of radio buttons share the dialog: each is a group of its own
    for (const auto &[first, last] : {std::pair(IDM_HEX, IDM_BIN), std::pair(IDM_QWORD, IDM_BYTE), std::pair(IDM_DEG, IDM_GRAD)}) {
        auto *family = new QButtonGroup(central);
        for (int id = first; id <= last; ++id)
            if (auto *button = qobject_cast<QRadioButton *>(m_controls.value(id)))
                family->addButton(button);
    }
    const auto radio = [this](int id, bool on) {
        if (auto *button = qobject_cast<QRadioButton *>(m_controls.value(id)))
            button->setChecked(on);
    };
    radio(IDM_HEX, m_engine.radix() == CalcEngine::Hex);
    radio(IDM_DEC, m_engine.radix() == CalcEngine::Dec);
    radio(IDM_OCT, m_engine.radix() == CalcEngine::Oct);
    radio(IDM_BIN, m_engine.radix() == CalcEngine::Bin);
    radio(IDM_QWORD, m_engine.wordBits() == 64);
    radio(IDM_DWORD, m_engine.wordBits() == 32);
    radio(IDM_WORD, m_engine.wordBits() == 16);
    radio(IDM_BYTE, m_engine.wordBits() == 8);
    radio(IDM_DEG, m_engine.angle() == CalcEngine::Degrees);
    radio(IDM_RAD, m_engine.angle() == CalcEngine::Radians);
    radio(IDM_GRAD, m_engine.angle() == CalcEngine::Grads);
    // The angles in decimal, the word sizes otherwise, in the same place
    const bool decimal = m_engine.radix() == CalcEngine::Dec;
    for (int id = IDM_QWORD; id <= IDM_BYTE; ++id)
        if (QWidget *control = m_controls.value(id))
            control->setVisible(m_scientific && !decimal);
    for (int id = IDM_DEG; id <= IDM_GRAD; ++id)
        if (QWidget *control = m_controls.value(id))
            control->setVisible(m_scientific && decimal);

    // (A click of the old view's may be what rebuilds it)
    if (QWidget *old = takeCentralWidget())
        old->deleteLater();
    setCentralWidget(central);
    setFixedSize(central->width(), central->height() + menuBar()->sizeHint().height());
    refresh();
}

void Calculator::press(int id, bool fromKey)
{
    switch (id) {
    case IDM_COPY: {
        QString text = m_engine.display();
        if (m_engine.grouping())
            text.remove(m_engine.radix() == CalcEngine::Dec ? QLocale::system().groupSeparator() : QStringLiteral(" "));
        QApplication::clipboard()->setText(text);
        return;
    }
    case IDM_PASTE:
        paste();
        return;
    case IDM_ABOUT:
        applets::about(xpfiles::string(*m_resources, IDS_CALCULATOR), xpfiles::icon(*m_resources, QStringLiteral("SC"), 32));
        return;
    case IDM_GROUPING:
        m_engine.setGrouping(!m_engine.grouping());
        if (QAction *action = m_actions.value(IDM_GROUPING))
            action->setChecked(m_engine.grouping());
        QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("UseSep"), m_engine.grouping());
        refresh();
        return;
    case IDM_STANDARD:
    case IDM_SCIENTIFIC:
        m_scientific = id == IDM_SCIENTIFIC;
        QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("layout"), m_scientific ? 0 : 1);
        build();
        return;
    case IDM_HEX: case IDM_DEC: case IDM_OCT: case IDM_BIN: {
        if (!m_scientific)
            return;
        static const CalcEngine::Radix radices[] = {CalcEngine::Hex, CalcEngine::Dec, CalcEngine::Oct, CalcEngine::Bin};
        m_engine.setRadix(radices[id - IDM_HEX]);
        build();
        return;
    }
    case IDM_QWORD: case IDM_DWORD: case IDM_WORD: case IDM_BYTE:
        if (!m_scientific || m_engine.radix() == CalcEngine::Dec)
            return;
        m_engine.setWordBits(64 >> (id - IDM_QWORD));
        build();
        return;
    case IDM_DEG: case IDM_RAD: case IDM_GRAD:
        if (!m_scientific || m_engine.radix() != CalcEngine::Dec)
            return;
        m_engine.setAngle(CalcEngine::Angle(id - IDM_DEG));
        build();
        return;
    case IDC_INV:
    case IDC_HYP:
        // A click has toggled it already; a key toggles it
        if (fromKey)
            if (QCheckBox *box = id == IDC_INV ? m_inv : m_hyp; box && box->isVisible())
                box->toggle();
        return;
    default:
        break;
    }
    QWidget *control = m_controls.value(id);
    if (!control || !control->isVisible() || !control->isEnabled())
        return;
    if (fromKey)
        if (auto *button = qobject_cast<QPushButton *>(control)) {
            // The button goes down a moment, as calc.exe shows a key on it
            button->setDown(true);
            QTimer::singleShot(100, button, [button] { button->setDown(false); });
        }
    if (id == IDC_STAT) {
        showStatistics();
        return;
    }
    const bool inv = m_inv && m_inv->isChecked();
    const bool hyp = m_hyp && m_hyp->isChecked();
    m_engine.command(id, inv, hyp);
    // Inv and Hyp last one operation
    if (!isDigit(id) && id != IDC_PNT && id != IDC_BACK && id != IDC_SIGN && id != IDC_EXP) {
        if (m_inv)
            m_inv->setChecked(false);
        if (m_hyp)
            m_hyp->setChecked(false);
    }
    refresh();
}

void Calculator::refresh()
{
    if (m_display)
        m_display->setText(m_engine.display());
    if (m_memoryBox)
        m_memoryBox->setText(m_engine.memory() ? QStringLiteral("M") : QString());
    if (m_parenthesesBox)
        m_parenthesesBox->setText(m_engine.parentheses() ? QStringLiteral("(=%1").arg(m_engine.parentheses()) : QString());
    refreshStatistics();
    updateEnabled();
}

void Calculator::updateEnabled()
{
    const int radix = m_engine.radix();
    for (int id = IDC_0; id <= IDC_F; ++id)
        if (QWidget *control = m_controls.value(id))
            control->setEnabled(id - IDC_0 < radix);
    // What has no meaning for whole numbers
    for (int id : {IDC_FE, IDC_DMS, IDC_EXP, IDC_SIN, IDC_COS, IDC_TAN, IDC_PI})
        if (QWidget *control = m_controls.value(id))
            control->setEnabled(radix == 10);
    // Ave, Sum, s and Dat while the Statistics Box is open
    for (int id = IDC_AVE; id <= IDC_DATA; ++id)
        if (QWidget *control = m_controls.value(id))
            control->setEnabled(!m_statistics.isNull());
}

void Calculator::showStatistics()
{
    if (m_statistics) {
        m_statistics->show();
        m_statistics->raise();
        m_statistics->activateWindow();
        return;
    }
    auto *dialog = new QDialog(this);
    m_statistics = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const xpdialog::Page page = xpdialog::build(*m_resources, IDD_STATISTICS, dialog);
    dialog->setWindowTitle(page.caption);
    // The list box, which the template builder leaves empty
    m_statisticsList = new QListWidget(dialog);
    if (QWidget *placeholder = page.controls.value(IDC_STATLIST)) {
        m_statisticsList->setGeometry(placeholder->geometry());
        placeholder->hide();
    }
    m_statisticsCount = qobject_cast<QLabel *>(page.controls.value(IDC_NUMTEXT));
    const auto onClick = [&page, dialog](int id, auto slot) {
        if (auto *button = qobject_cast<QPushButton *>(page.controls.value(id)))
            QObject::connect(button, &QPushButton::clicked, dialog, slot);
    };
    onClick(IDC_RET, [this] { activateWindow(); });
    onClick(IDC_LOAD, [this] {
        const int row = m_statisticsList->currentRow();
        if (row >= 0 && row < m_engine.statistics().size()) {
            m_engine.setValue(m_engine.statistics().at(row));
            refresh();
        }
    });
    onClick(IDC_CD, [this] {
        const int row = m_statisticsList->currentRow();
        if (row >= 0 && row < m_engine.statistics().size()) {
            m_engine.statistics().removeAt(row);
            refresh();
        }
    });
    onClick(IDC_CAD, [this] {
        m_engine.statistics().clear();
        refresh();
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        m_statisticsList = nullptr;
        m_statisticsCount = nullptr;
        QTimer::singleShot(0, this, [this] { updateEnabled(); });
    });
    dialog->show();
    refresh();
}

void Calculator::refreshStatistics()
{
    if (!m_statisticsList)
        return;
    const int row = m_statisticsList->currentRow();
    m_statisticsList->clear();
    for (CalcReal value : std::as_const(m_engine.statistics()))
        m_statisticsList->addItem(m_engine.format(value));
    m_statisticsList->setCurrentRow(qMin(row, m_statisticsList->count() - 1));
    if (m_statisticsCount)
        m_statisticsCount->setText(QString::number(m_engine.statistics().size()));
}

void Calculator::paste()
{
    // Each character as its key, as calc.exe takes the clipboard
    const QString text = QApplication::clipboard()->text();
    for (QChar c : text) {
        int id = 0;
        if (c.isDigit())
            id = IDC_0 + c.digitValue();
        else if (c == QLatin1Char('\n'))
            id = IDC_EQU;
        else if (c == QLocale::system().decimalPoint().at(0))
            id = IDC_PNT;
        for (const xpprogram::Accelerator &key : std::as_const(m_keys)) {
            if (id)
                break;
            if (!key.virtualKey && key.key == c.unicode())
                id = key.command;
            else if (key.virtualKey && key.modifiers == Qt::NoModifier && key.key == c.toUpper().unicode() && c.isLetter())
                id = key.command;
        }
        if (id)
            press(id);
    }
}

void Calculator::keyPressEvent(QKeyEvent *event)
{
    int id = xpprogram::command(m_keys, event);
    if (!id) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    // One table for both views: in Standard "%" is the percent and "@" the square root; F2 to F4 are the
    // word sizes outside decimal
    if (!m_scientific && id == IDC_MOD)
        id = IDC_PERCENT;
    if (!m_scientific && id == IDC_SQR)
        id = IDC_SQRT;
    if (m_engine.radix() != CalcEngine::Dec && id >= IDM_DEG && id <= IDM_GRAD)
        id += IDM_DWORD - IDM_DEG;
    press(id, true);
}

void Calculator::closeEvent(QCloseEvent *event)
{
    if (m_statistics)
        m_statistics->close();
    event->accept();
}
