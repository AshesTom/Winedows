// SPDX-License-Identifier: LGPL-2.1-or-later
// The Linux applications, from their desktop entries, in XP's Programs folders.
#pragma once

#include <QIcon>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

namespace programs {

struct Program
{
    QString id;           // its desktop entry's file name
    QString name, icon;
    QString comment;      // its Comment, which XP's Start menu shows as the item's infotip
    QStringList command;
    QStringList categories;
};

// Read each time: the Start menu has what is installed when it opens.
QMap<QString, QList<Program>> folders();
std::optional<Program> find(const QString &id);
// The application whose windows have this WM_CLASS class; nullopt if no entry names it.
std::optional<Program> forWindowClass(const QString &wmClass);
// A desktop entry's file, shown or not; nullopt if it is none.
std::optional<Program> fromFile(const QString &path);
QIcon iconOf(const Program &program);
// Starts it and counts the use, for the Start menu's most used programs.
void launch(const Program &program);
// Starts it with FILES, filling %f, %F, %u, %U and %% (or appending them).
void launchWith(const Program &program, const QStringList &files);
// All installed desktop entries, deduplicated by id and sorted by name.
QList<Program> allPrograms();
// XP's Open With dialog: returns the chosen program (without starting it), or nullopt.
std::optional<Program> chooseProgram(const QString &path, QWidget *parent);
// Makes PROGRAM the default for PATH's MIME type through xdg-mime.
void setDefaultFor(const QString &path, const Program &program);
// Forget the Start menu's launch counts (Customize Start Menu's Clear List).
void clearUses();
// Remove one program from the most-used list.
void removeUse(const QString &id);
// Programs by their uses, most used first.
QList<Program> mostUsed(int count);
// The user's default programs, through xdg-settings and xdg-mime.
std::optional<Program> defaultBrowser();
std::optional<Program> defaultMail();
// Opens a path or URL with its program.
void open(const QString &target);
// XP's Command Prompt in FOLDER.
void openCommandPrompt(const QString &folder);
// XP's Notepad (with FILE, if any) and Calculator, each a program of its own
void openNotepad(const QString &file = QString());
void openCalculator();
void openMediaPlayer(const QStringList &files = {});
// The icon theme for programs' icons: the one chosen in linux-look.conf, the
// Plasma user's, or a common one that is installed.
void useIconTheme();

} // namespace programs
