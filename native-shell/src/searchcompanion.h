// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Search Companion, the bar beside the search results: its steps are the
// .xml files of XP's own srchasst folder, read and built into widgets here.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <optional>

class AgentWidget;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

struct SearchQuery
{
    QString name, text, folder;
    QStringList extensions;
    bool hidden = false, system = false, subfolders = true, caseSensitive = false;
    qint64 minSize = -1, maxSize = -1;
    QDateTime after, before;
    int whichDate = 1;   // 1 modified, 2 created, 3 accessed
    bool byType = false;
    QString step;        // the step it came from, to go back to
};

class SearchCompanion : public QWidget
{
    Q_OBJECT
public:
    explicit SearchCompanion(QWidget *parent = nullptr);
    // Where a search looks when it does not say.
    void setCurrentFolder(const QString &folder) { m_currentFolder = folder; }
    void restart();
    void searchFinished(int count);

signals:
    void searchFiles(const SearchQuery &query);
    void searchWeb(const QString &text, const QString &engine);
    void highlightWeb(const QString &text);
    void stopSearch();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Node
    {
        QString tag, text;
        QHash<QString, QString> attributes, properties;
        QList<QPair<QString, QString>> searchProps, items;
        QList<Node> children;
        int id() const { return attributes.value(QStringLiteral("Id")).toInt(); }
    };
    struct Control { Node node; QWidget *widget; };

    std::optional<Node> loadStep(const QString &name) const;
    void showStep(const QString &name, bool remember = true);
    void addNode(const Node &node, QVBoxLayout *layout);
    QWidget *controlFor(const Node &node);
    void addLink(const Node &node, QVBoxLayout *layout);
    void activate(int id);
    void goBack();
    void submit();
    void submitWeb(const QString &text);
    SearchQuery buildQuery() const;
    bool condition(const QString &text) const;
    QString engine() const;
    void characterChanged();
    void placeCharacter();
    QRect balloonRect() const;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_balloon = nullptr;
    QVBoxLayout *m_content = nullptr;
    AgentWidget *m_agent = nullptr;
    QPushButton *m_back = nullptr, *m_search = nullptr, *m_stop = nullptr;
    QList<Control> m_controls;
    QStringList m_history;
    QString m_step, m_currentFolder, m_titleOverride, m_webText;
    SearchQuery m_query;
    int m_found = 0, m_margin = 0;
    bool m_character = true;
    QHash<int, QWidget *> m_expanders;
    QColor m_top, m_bottom, m_body;
};
