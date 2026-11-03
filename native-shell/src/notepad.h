// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Notepad as a Linux program: its menus, keys, words and dialogs come from the user's
// notepad.exe, the Find, Replace and Font dialogs from comdlg32.dll, its font is XP's Lucida
// Console. Files are read and written as XP's Notepad does (ANSI, which is the locale's encoding
// here, Unicode, Unicode big endian and UTF-8 with their marks), .LOG files get the time and date.
#pragma once

#include "msstyles.h"

#include <QFont>
#include <QHash>
#include <QMainWindow>
#include <QMarginsF>
#include <QPageLayout>
#include <QPointer>

#include <optional>

class QAction;
class QDialog;
class QLabel;
class QPlainTextEdit;
class NotepadStatus;

class Notepad : public QMainWindow
{
public:
    enum Encoding { Ansi, Unicode, UnicodeBigEndian, Utf8 };

    explicit Notepad(const QString &file = QString());

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString text(int id) const;
    void message(int id, const QString &file, int icon) const;
    void updateTitle();
    void updateStatus();
    void updateEditMenu();
    bool confirmDiscard();
    void newFile();
    void openFile();
    bool load(const QString &path, std::optional<Encoding> encoding = std::nullopt);
    bool save();
    bool saveAs();
    bool write(const QString &path, Encoding encoding);
    QString fileDialog(bool saving, Encoding *encoding);
    void pageSetup();
    void print();
    void undo();
    void find(bool replacing);
    bool findNext();
    void replace();
    void replaceAll();
    void goTo();
    void insertTimeDate();
    void setWordWrap(bool on);
    void chooseFont();
    void applyFont();
    void readSettings();
    void writeSettings() const;

    std::optional<w4w::PeResources> m_resources, m_dialogs;
    QPlainTextEdit *m_edit;
    NotepadStatus *m_status = nullptr;
    QHash<int, QAction *> m_actions;
    QString m_path;
    Encoding m_encoding = Ansi;
    bool m_crlf = false;
    bool m_undone = false;

    // What Format and View chose, as Notepad keeps it
    QString m_family = QStringLiteral("Lucida Console");
    int m_points = 10;
    bool m_bold = false, m_italic = false, m_wrap = false, m_statusBar = false;

    // Find and Replace: one modeless dialog at a time, as comdlg32's
    QPointer<QDialog> m_findDialog;
    QString m_findText, m_replaceText;
    bool m_matchCase = false, m_down = true;

    // Page Setup: the header and footer with their codes, the margins in millimetres
    QString m_header, m_footer;
    QMarginsF m_margins = QMarginsF(19.05, 25.4, 19.05, 25.4);
    QPageLayout::Orientation m_orientation = QPageLayout::Portrait;
    QString m_paper;
    QString m_printer;
};
