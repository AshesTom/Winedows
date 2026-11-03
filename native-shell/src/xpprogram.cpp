// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpprogram.h"

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QtEndian>

#include <functional>

namespace xpprogram {
namespace {

// MENUITEMTEMPLATE's flags
const quint16 MF_GRAYED = 0x01, MF_CHECKED = 0x08, MF_POPUP = 0x10, MF_END = 0x80;

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}
    bool ok() const { return m_ok; }
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
    QString text()
    {
        QString out;
        for (quint16 c = u16(); c && m_ok; c = u16())
            out += QChar(c);
        return out;
    }
    void skip(int bytes) { m_at += bytes; }

private:
    const QByteArray &m_data;
    int m_at = 0;
    bool m_ok = true;
};

// A virtual key of winuser.h as Qt's
int qtKey(int vk)
{
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
        return vk;
    if (vk >= 0x60 && vk <= 0x69)   // VK_NUMPAD0..9
        return Qt::Key_0 + vk - 0x60;
    if (vk >= 0x70 && vk <= 0x87)   // VK_F1..F24
        return Qt::Key_F1 + vk - 0x70;
    switch (vk) {
    case 0x08: return Qt::Key_Backspace;
    case 0x09: return Qt::Key_Tab;
    case 0x0d: return Qt::Key_Return;
    case 0x1b: return Qt::Key_Escape;
    case 0x20: return Qt::Key_Space;
    case 0x21: return Qt::Key_PageUp;
    case 0x22: return Qt::Key_PageDown;
    case 0x23: return Qt::Key_End;
    case 0x24: return Qt::Key_Home;
    case 0x25: return Qt::Key_Left;
    case 0x26: return Qt::Key_Up;
    case 0x27: return Qt::Key_Right;
    case 0x28: return Qt::Key_Down;
    case 0x2d: return Qt::Key_Insert;
    case 0x2e: return Qt::Key_Delete;
    case 0x6a: return Qt::Key_Asterisk;
    case 0x6b: return Qt::Key_Plus;
    case 0x6d: return Qt::Key_Minus;
    case 0x6e: return Qt::Key_Period;
    case 0x6f: return Qt::Key_Slash;
    default: return 0;
    }
}

} // namespace

QHash<int, QAction *> fillMenuBar(QMenuBar *bar, const w4w::PeResources &pe, int id)
{
    QHash<int, QAction *> actions;
    const QByteArray data = pe.find(QStringLiteral("4"), QString::number(id));
    Reader r(data);
    // MENUHEADER: version 0 and the offset of the items
    if (r.u16() != 0)
        return actions;
    r.skip(r.u16());
    std::function<void(QMenu *)> items = [&](QMenu *menu) {
        while (r.ok()) {
            const quint16 flags = r.u16();
            if (flags & MF_POPUP) {
                const QString text = r.text();
                QMenu *popup = menu ? menu->addMenu(text) : bar->addMenu(text);
                items(popup);
            } else {
                const int command = r.u16();
                const QString text = r.text();
                if (command == 0 && text.isEmpty()) {
                    menu->addSeparator();
                } else {
                    QAction *action = menu->addAction(text);
                    action->setCheckable(flags & MF_CHECKED);
                    action->setChecked(flags & MF_CHECKED);
                    action->setEnabled(!(flags & MF_GRAYED));
                    actions.insert(command, action);
                }
            }
            if (flags & MF_END)
                return;
        }
    };
    items(nullptr);
    return actions;
}

QList<Accelerator> accelerators(const w4w::PeResources &pe, const QString &table)
{
    // ACCELTABLEENTRY: flags, key, command, padding
    QList<Accelerator> out;
    const QByteArray data = pe.find(QStringLiteral("9"), table);
    for (qsizetype at = 0; at + 8 <= data.size(); at += 8) {
        const quint16 flags = qFromLittleEndian<quint16>(data.constData() + at);
        Accelerator entry;
        entry.key = qFromLittleEndian<quint16>(data.constData() + at + 2);
        entry.command = qFromLittleEndian<quint16>(data.constData() + at + 4);
        entry.virtualKey = flags & 0x01;   // FVIRTKEY
        if (entry.virtualKey) {
            entry.key = qtKey(entry.key);
            if (flags & 0x04)
                entry.modifiers |= Qt::ShiftModifier;
            if (flags & 0x08)
                entry.modifiers |= Qt::ControlModifier;
            if (flags & 0x10)
                entry.modifiers |= Qt::AltModifier;
        }
        if (!entry.virtualKey || entry.key)
            out.append(entry);
        if (flags & 0x80)
            break;
    }
    return out;
}

QList<QKeySequence> shortcuts(const QList<Accelerator> &table, int command)
{
    QList<QKeySequence> out;
    for (const Accelerator &entry : table)
        if (entry.command == command && entry.virtualKey)
            out.append(QKeySequence(QKeyCombination(entry.modifiers, Qt::Key(entry.key))));
    return out;
}

int command(const QList<Accelerator> &table, const QKeyEvent *event)
{
    // The key first, as WM_KEYDOWN comes before WM_CHAR
    const Qt::KeyboardModifiers modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    int key = event->key();
    if (key == Qt::Key_Enter)
        key = Qt::Key_Return;
    for (const Accelerator &entry : table)
        if (entry.virtualKey && entry.key == key && entry.modifiers == modifiers)
            return entry.command;
    const QString text = event->text();
    if (text.size() != 1)
        return 0;
    for (const Accelerator &entry : table)
        if (!entry.virtualKey && entry.key == text.at(0).unicode())
            return entry.command;
    return 0;
}

} // namespace xpprogram
