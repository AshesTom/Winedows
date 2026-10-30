// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's file operations: copying and moving in a thread with its progress window,
// the Recycle Bin, and the clipboard's files.
#pragma once

#include <QList>
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

void copyToClipboard(const QStringList &paths, bool cut);
bool clipboardHasFiles();
void pasteInto(const QString &folder, QWidget *parent);

// The Recycle Bin (the freedesktop trash)
QString trashFiles();
void emptyTrash(QWidget *parent);
// Back where it was, from its .trashinfo.
void restoreFromTrash(const QStringList &paths, QWidget *parent);

// A name that does not exist in FOLDER yet, from BASE: "New Folder", "New Folder (2)"
QString freeName(const QString &folder, const QString &base);

} // namespace fileops
