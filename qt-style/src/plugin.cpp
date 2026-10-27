// SPDX-License-Identifier: LGPL-2.1-or-later
// The style plugin: QT_STYLE_OVERRIDE=Win4Wine-XP (session/supervisor.py
// sets it for the session's Linux applications).
#include "xpstyle.h"

#include <QStylePlugin>

class Win4WineStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "win4wine-xp.json")
public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("Win4Wine-XP"), Qt::CaseInsensitive) == 0)
            return new w4w::XpStyle;
        return nullptr;
    }
};

#include "plugin.moc"
