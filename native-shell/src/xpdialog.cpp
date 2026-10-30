// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QtEndian>

namespace xpdialog {
namespace {

// MS Shell Dlg (Tahoma 8) has dialog base units of 6 x 13 pixels at 96 DPI.
int unitX(int value) { return value * 6 / 4; }
int unitY(int value) { return value * 13 / 8; }

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}
    bool ok() const { return m_ok; }
    int at() const { return m_at; }
    void seek(int at) { m_at = at; }
    void align() { m_at = (m_at + 3) & ~3; }
    quint16 u16()
    {
        if (m_at + 2 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        const quint16 value = qFromLittleEndian<quint16>(m_data.constData() + m_at);
        m_at += 2;
        return value;
    }
    quint32 u32()
    {
        const quint32 low = u16();
        return low | quint32(u16()) << 16;
    }
    // A sz_Or_Ord: text, or a number as "#N" for the standard classes' atoms.
    QString text(int *ordinal = nullptr)
    {
        const quint16 first = u16();
        if (first == 0xffff) {
            const quint16 number = u16();
            if (ordinal)
                *ordinal = number;
            return QString();
        }
        QString out;
        for (quint16 c = first; c && m_ok; c = u16())
            out += QChar(c);
        return out;
    }

private:
    const QByteArray &m_data;
    int m_at = 0;
    bool m_ok = true;
};

} // namespace

Page build(const w4w::PeResources &pe, int id, QWidget *parent)
{
    Page page;
    const QByteArray data = pe.find(QStringLiteral("5"), QString::number(id));
    Reader r(data);
    if (data.size() < 26 || r.u16() != 1 || r.u16() != 0xffff)
        return page;
    r.seek(12);
    const quint32 style = r.u32();
    const int count = r.u16();
    r.u16();
    r.u16();
    const int width = qint16(r.u16());
    const int height = qint16(r.u16());
    r.text();
    r.text();
    page.caption = r.text();
    if (style & 0x40) {
        r.seek(r.at() + 6);
        r.text();
    }
    parent->setFixedSize(unitX(width), unitY(height));
    QWidget *previous = nullptr;
    for (int i = 0; i < count && r.ok(); ++i) {
        r.align();
        r.u32();
        r.u32();
        const quint32 controlStyle = r.u32();
        const int x = qint16(r.u16()), y = qint16(r.u16()), cx = qint16(r.u16()), cy = qint16(r.u16());
        const int control = int(r.u32());
        int ordinal = 0, titleOrdinal = 0;
        const QString className = r.text(&ordinal);
        const QString text = r.text(&titleOrdinal);
        r.seek(r.at() + 2 + r.u16());
        // 0x80 Button, 0x81 Edit, 0x82 Static, 0x85 ComboBox
        QWidget *widget = nullptr;
        if (ordinal == 0x80 || className.compare(QLatin1String("Button"), Qt::CaseInsensitive) == 0) {
            const int kind = controlStyle & 0xf;
            if (kind == 7)
                widget = new QGroupBox(text, parent);
            else if (kind == 2 || kind == 3)
                widget = new QCheckBox(text, parent);
            else if (kind == 4 || kind == 9)
                widget = new QRadioButton(text, parent);
            else
                widget = new QPushButton(text, parent);
        } else if (ordinal == 0x82 || className.compare(QLatin1String("Static"), Qt::CaseInsensitive) == 0) {
            auto *label = new QLabel(text, parent);
            label->setWordWrap(true);
            label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            widget = label;
        } else if (ordinal == 0x85 || className.compare(QLatin1String("ComboBox"), Qt::CaseInsensitive) == 0) {
            auto *combo = new QComboBox(parent);
            // A combo's height is its list's in the resource.
            widget = combo;
        } else {
            widget = new QWidget(parent);
        }
        if (auto *label = qobject_cast<QLabel *>(previous); label && qobject_cast<QComboBox *>(widget))
            label->setBuddy(widget);
        widget->setGeometry(unitX(x), unitY(y), unitX(cx), qobject_cast<QComboBox *>(widget) ? 21 : unitY(cy));
        page.controls.insert(control, widget);
        previous = widget;
    }
    return page;
}

} // namespace xpdialog
