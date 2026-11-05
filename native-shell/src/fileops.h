// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's file operations: copying and moving in a thread with its progress window,
// the Recycle Bin, and the clipboard's files.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QWidget;

namespace fileops {

enum class Action { Copy, Move };

// Starts it; a window shows its progress and can cancel it. Files of the same
// name are asked about.
void transfer(const QStringList &sources, const QString &targetFolder, Action action, QWidget *parent);
// Sends them to the Recycle Bin, after asking. With PERMANENT they are deleted, after asking.
void remove(const QStringList &paths, bool permanent, QWidget *parent);

// Puts a rename done elsewhere (a folder window's in-place edit) on the undo stack.
void recordRename(const QString &before, const QString &after);

// shell32's undo stack: the last ten moves, copies, deletes to the Recycle Bin and renames of this
// process's windows, undone from the newest.
class UndoStack : public QObject
{
    Q_OBJECT
public:
    static UndoStack *instance();
    bool canUndo() const;
    // "&Undo Delete\tCtrl+Z", or "&Undo\tCtrl+Z" with nothing to undo
    QString menuText() const;
    void undo(QWidget *parent);

Q_SIGNALS:
    void changed();
};

void copyToClipboard(const QStringList &paths, bool cut);
bool clipboardHasFiles();
void pasteInto(const QString &folder, QWidget *parent);

// The Recycle Bin (the freedesktop trash)
QString trashFiles();
void emptyTrash(QWidget *parent);
// Back where it was, from its .trashinfo; false if one could not be.
bool restoreFromTrash(const QStringList &paths, QWidget *parent);

// A name that does not exist in FOLDER yet, from BASE: "New Folder", "New Folder (2)"
QString freeName(const QString &folder, const QString &base);

// The target of a Windows .lnk (Shell Link): its local base path plus common suffix, or its
// relative path; empty if PATH is not a readable shortcut.
QString shortcutTarget(const QString &path);

// Writes a Shell Link (.lnk) at LINKPATH pointing at TARGET, its LinkInfo's local base path; false
// if the file cannot be written.
bool createShortcut(const QString &target, const QString &linkPath);

} // namespace fileops
