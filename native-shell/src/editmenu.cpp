// SPDX-License-Identifier: LGPL-2.1-or-later
#include "editmenu.h"

#include "xpfiles.h"
#include "xpprogram.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextEdit>

namespace editmenu {
namespace {

// user32's menu 1 (MENUEX) by its commands: WM_UNDO, WM_CUT, WM_COPY, WM_PASTE, WM_CLEAR and
// EM_SETSEL for Select All. Its reading order and Unicode control character items (0x8000 up) are
// there only with complex script support, and its IME items only with an IME: not on an English XP
// as installed.
enum { UNDO = 0x304, CUT = 0x300, COPY = 0x301, PASTE = 0x302, DELETE = 0x303, SELECT_ALL = 0xb1, COMPLEX = 0x8000 };

// The three kinds of text box behind one face
struct Box
{
    QLineEdit *line = nullptr;
    QTextEdit *rich = nullptr;
    QPlainTextEdit *plain = nullptr;

    explicit operator bool() const { return line || rich || plain; }
    bool readOnly() const { return line ? line->isReadOnly() : rich ? rich->isReadOnly() : plain->isReadOnly(); }
    bool canUndo() const
    {
        return line ? line->isUndoAvailable() : rich ? rich->document()->isUndoAvailable() : plain->document()->isUndoAvailable();
    }
    bool hasSelection() const
    {
        return line ? line->hasSelectedText() : rich ? rich->textCursor().hasSelection() : plain->textCursor().hasSelection();
    }
    bool secret() const { return line && line->echoMode() != QLineEdit::Normal; }
    int length() const
    {
        return int(line ? line->text().size() : rich ? rich->document()->characterCount() - 1 : plain->document()->characterCount() - 1);
    }
    int selected() const
    {
        if (line)
            return int(line->selectedText().size());
        const QTextCursor cursor = rich ? rich->textCursor() : plain->textCursor();
        return cursor.selectionEnd() - cursor.selectionStart();
    }
    void run(int command) const
    {
        switch (command) {
        case UNDO:
            line ? line->undo() : rich ? rich->undo() : plain->undo();
            break;
        case CUT:
            line ? line->cut() : rich ? rich->cut() : plain->cut();
            break;
        case COPY:
            line ? line->copy() : rich ? rich->copy() : plain->copy();
            break;
        case PASTE:
            line ? line->paste() : rich ? rich->paste() : plain->paste();
            break;
        case DELETE:
            if (line) {
                line->del();
            } else {
                QTextCursor cursor = rich ? rich->textCursor() : plain->textCursor();
                cursor.removeSelectedText();
                rich ? rich->setTextCursor(cursor) : plain->setTextCursor(cursor);
            }
            break;
        case SELECT_ALL:
            line ? line->selectAll() : rich ? rich->selectAll() : plain->selectAll();
            break;
        }
    }
};

Box boxOf(QWidget *widget)
{
    Box box;
    for (int depth = 0; widget && depth < 2 && !box; ++depth, widget = widget->parentWidget()) {
        box.line = qobject_cast<QLineEdit *>(widget);
        box.rich = qobject_cast<QTextEdit *>(widget);
        box.plain = qobject_cast<QPlainTextEdit *>(widget);
    }
    return box;
}

class Watcher : public QObject
{
public:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::ContextMenu || !object->isWidgetType())
            return false;
        // (a text box's viewport has it first)
        // (an editable combo box's text box passes it to the combo box, which asks the text box)
        auto *widget = static_cast<QWidget *>(object);
        Box box;
        if (auto *combo = qobject_cast<QComboBox *>(widget))
            box.line = combo->lineEdit();
        else
            box = boxOf(widget);
        QWidget *owner = box.line ? static_cast<QWidget *>(box.line) : box.rich ? static_cast<QWidget *>(box.rich) : box.plain;
        if (!box || widget->contextMenuPolicy() != Qt::DefaultContextMenu)
            return false;
        show(box, owner, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    }

private:
    static void show(const Box &box, QWidget *owner, const QPoint &at)
    {
        static const auto user32 = xpfiles::resources(xpfiles::find(QStringLiteral("user32.dll")));
        if (!user32)
            return;
        QMenu menu(owner);
        const QHash<int, QAction *> items = xpprogram::fillMenu(&menu, *user32, 1);
        if (items.isEmpty())
            return;
        for (auto it = items.begin(); it != items.end(); ++it)
            if (it.key() >= COMPLEX && it.value()) {
                if (QMenu *sub = it.value()->menu())
                    sub->deleteLater();
                menu.removeAction(it.value());
            }
        // what is left of the separators: none at the end, none twice
        QAction *previous = nullptr;
        for (QAction *action : menu.actions()) {
            if (action->isSeparator() && (!previous || previous->isSeparator()))
                menu.removeAction(action);
            else
                previous = action;
        }
        while (!menu.actions().isEmpty() && menu.actions().last()->isSeparator())
            menu.removeAction(menu.actions().last());
        // the edit control's rules for each item
        const bool editable = !box.readOnly();
        const bool selection = box.hasSelection();
        const QMimeData *clip = QApplication::clipboard()->mimeData();
        const auto enable = [&](int command, bool on) {
            if (QAction *action = items.value(command))
                action->setEnabled(on);
        };
        enable(UNDO, editable && box.canUndo());
        enable(CUT, editable && selection && !box.secret());
        enable(COPY, selection && !box.secret());
        enable(PASTE, editable && clip && clip->hasText());
        enable(DELETE, editable && selection);
        enable(SELECT_ALL, box.length() > 0 && box.selected() != box.length());
        QPointer<QWidget> alive(owner);
        QAction *chosen = menu.exec(at);
        if (!chosen || !alive)
            return;
        for (auto it = items.cbegin(); it != items.cend(); ++it)
            if (it.value() == chosen)
                box.run(it.key());
    }
};

} // namespace

void install()
{
    static Watcher *watcher = new Watcher;
    qApp->installEventFilter(watcher);
}

} // namespace editmenu
