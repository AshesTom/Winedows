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
    QStringList command;
    QStringList categories;
};

// Read each time: the Start menu has what is installed when it opens.
QMap<QString, QList<Program>> folders();
std::optional<Program> find(const QString &id);
// A desktop entry's file, shown or not; nullopt if it is none.
std::optional<Program> fromFile(const QString &path);
QIcon iconOf(const Program &program);
// Starts it and counts the use, for the Start menu's most used programs.
void launch(const Program &program);
// Programs by their uses, most used first.
QList<Program> mostUsed(int count);
// The user's default programs, through xdg-settings and xdg-mime.
std::optional<Program> defaultBrowser();
std::optional<Program> defaultMail();
// Opens a path or URL with its program.
void open(const QString &target);
// XP's Command Prompt in FOLDER.
void openCommandPrompt(const QString &folder);
// The icon theme for programs' icons: the one chosen in linux-look.conf, the
// Plasma user's, or a common one that is installed.
void useIconTheme();

} // namespace programs
