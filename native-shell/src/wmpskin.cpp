// SPDX-License-Identifier: LGPL-2.1-or-later
#include "wmpskin.h"

#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtEndian>

namespace wmpskin {

QVariant Node::attribute(const QString &name) const
{
    for (const auto &[key, value] : attributes)
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    return {};
}

namespace {

// The interfaces of the skin elements, whose DISPIDs do not overlap.
const char *const ElementInterfaces[] = {
    "IWMPObjectExtendedProps", "IWMPLayoutSubView", "IWMPLayoutView", "IWMPButtonCtrl", "IWMPRegionalButtonCtrl",
    "IWMPRegionalButton", "IWMPSliderCtrl", "IWMPCustomSlider", "IWMPTextCtrl", "IWMPVideoCtrl", "IWMPEffectsCtrl",
    "IWMPPlaylistCtrl", "IWMPEqualizerSettingsCtrl", "IWMPVideoSettingsCtrl", "IWMPLibraryTreeCtrl", "IWMPEditCtrl",
    "IWMPListBoxCtrl", "IWMPMenuCtrl", "IWMPAutoMenuCtrl", "IWMPPluginUIHost",
};

// The element kinds of a compiled skin, as wmp.dll's skins have them.
QString kindTag(int kind)
{
    switch (kind) {
    case 1: return QStringLiteral("button");
    case 2: return QStringLiteral("buttongroup");
    case 3: return QStringLiteral("buttonelement");
    case 6: return QStringLiteral("playlist");
    case 7: return QStringLiteral("text");
    case 8: return QStringLiteral("slider");
    case 9: return QStringLiteral("video");
    case 10: return QStringLiteral("effects");
    case 11: return QStringLiteral("theme");
    case 12: return QStringLiteral("view");
    case 13: return QStringLiteral("subview");
    case 14: return QStringLiteral("plugin");
    case 15: return QStringLiteral("menu");
    case 16: return QStringLiteral("taskcenter");
    case 17: return QStringLiteral("automenu");
    }
    return {};
}

// Elements named by their CLSID only: the REGISTRY scripts of wmp.dll name them.
QString classTag(const QUuid &clsid)
{
    static const QHash<QString, QString> tags{
        {QStringLiteral("{87291b51-0c8e-11d3-bb2a-00a0c93ca73a}"), QStringLiteral("button")},
        {QStringLiteral("{ae3b6831-25a9-11d3-bd41-00c04f6ea5ae}"), QStringLiteral("buttongroup")},
        {QStringLiteral("{f2bf2c90-405f-11d3-bb39-00a0c93ca73a}"), QStringLiteral("slider")},
        {QStringLiteral("{95f45aa3-ed0a-11d2-ba67-0000f80855e6}"), QStringLiteral("customslider")},
        {QStringLiteral("{ddda102e-0e17-11d3-a2e2-00c04f79f88e}"), QStringLiteral("text")},
        {QStringLiteral("{61cecf11-fc3a-11d2-a1cd-005004602752}"), QStringLiteral("video")},
        {QStringLiteral("{47dea830-d619-4154-b8d8-6b74845d6a2d}"), QStringLiteral("effects")},
        {QStringLiteral("{5f9cfd93-8cad-11d3-9a7e-00c04f8efb70}"), QStringLiteral("playlist")},
        {QStringLiteral("{d9de732a-aee9-4503-9d11-5605589977a8}"), QStringLiteral("librarytree")},
        {QStringLiteral("{6342fced-25ea-4033-bddb-d049a14382d3}"), QStringLiteral("editbox")},
        {QStringLiteral("{fc1880cf-83b9-43a7-a066-c44ce8c82583}"), QStringLiteral("listbox")},
        {QStringLiteral("{aa1ac37b-49a8-4b41-af69-b0176c5ffc33}"), QStringLiteral("plugin")},
        {QStringLiteral("{bab3768b-8883-4aec-9f9b-e14c947913ef}"), QStringLiteral("menu")},
        {QStringLiteral("{6b28f900-8d64-4b80-9963-cc52ddd1fbb4}"), QStringLiteral("automenu")},
        {QStringLiteral("{93eb32f5-87b1-45ad-acc6-0f2483db83bb}"), QStringLiteral("equalizersettings")},
        {QStringLiteral("{ae7bfafe-dcc8-4a73-92c8-cc300ca88859}"), QStringLiteral("videosettings")},
        {QStringLiteral("{395bf287-6477-495f-8427-2c09a23c3248}"), QStringLiteral("taskcenter")},
    };
    return tags.value(clsid.toString(QUuid::WithBraces));
}

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}
    bool fits(qsizetype at, qsizetype size) const { return at >= 0 && size >= 0 && at + size <= m_data.size(); }
    quint8 u8(qsizetype at) const { return fits(at, 1) ? quint8(m_data[at]) : 0; }
    quint16 u16(qsizetype at) const { return fits(at, 2) ? qFromLittleEndian<quint16>(m_data.constData() + at) : 0; }
    quint32 u32(qsizetype at) const { return fits(at, 4) ? qFromLittleEndian<quint32>(m_data.constData() + at) : 0; }
    // A NUL-terminated UTF-16 string at AT; *END past its NUL.
    QString wide(qsizetype at, qsizetype *end) const
    {
        qsizetype stop = at;
        while (fits(stop, 2) && u16(stop))
            stop += 2;
        *end = stop + 2;
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(m_data.constData() + at), (stop - at) / 2);
    }
    const QByteArray &data() const { return m_data; }

private:
    const QByteArray &m_data;
};

struct Compiled
{
    const Reader &in;
    const QHash<quint32, QString> &names;
    const std::function<QString(int)> &strings;
    int depth = 0;

    // The attribute at AT; *VALUE_END past its value.
    std::pair<QString, QVariant> attribute(qsizetype at, qsizetype *valueEnd) const
    {
        const quint8 type = in.u8(at + 2);
        qsizetype p = at + 3;
        QString name;
        QVariant value;
        if ((type & 0x1f) == 0) {
            name = in.wide(p, &p);
            value = in.wide(p, &p);
        } else {
            const quint32 dispid = in.u32(p);
            name = names.value(dispid, QString::number(dispid));
            p += 4;
            if (type & 0x10) {
                value = strings(int(in.u32(p)));
                p += 4;
            } else if (type & 8) {
                QString text = in.wide(p, &p);
                if ((type & 0xc0) == 0x80)
                    text.prepend(QLatin1String("jscript:"));
                else if ((type & 0xc0) == 0x40)
                    text.prepend(QLatin1String("wmpprop:"));
                value = text;
            } else if (type & 4) {
                value = qint32(in.u32(p));
                p += 4;
            } else if (type & 1) {
                value = in.u16(p) != 0;
                p += 2;
            }
        }
        *valueEnd = p;
        return {name, value};
    }

    std::optional<Node> node(qsizetype at, qsizetype *end)
    {
        if (!in.fits(at, 9) || ++depth > 64)
            return std::nullopt;
        const quint16 next = in.u16(at), child = in.u16(at + 2);
        const quint8 kind = in.u8(at + 4), children = in.u8(at + 5), attributes = in.u8(at + 6);
        qsizetype p;
        Node node;
        node.tag = in.wide(at + 7, &p).toLower();
        QUuid clsid;
        if (kind & 0x80) {
            if (!in.fits(p, 16))
                return std::nullopt;
            // a GUID structure: its first three fields little-endian
            clsid = QUuid(in.u32(p), in.u16(p + 4), in.u16(p + 6), in.u8(p + 8), in.u8(p + 9), in.u8(p + 10), in.u8(p + 11),
                          in.u8(p + 12), in.u8(p + 13), in.u8(p + 14), in.u8(p + 15));
            p += 16;
        }
        // Global objects (kind 0) keep their name: <player>, <mediacenter>...
        if (const QString tag = !clsid.isNull() ? classTag(clsid) : kindTag(kind & 0x7f); !tag.isEmpty())
            node.tag = tag;
        else if ((kind & 0x7f) != 0 && node.tag.isEmpty())
            node.tag = QStringLiteral("unknown%1").arg(kind & 0x7f);
        for (int i = 0; i < attributes; ++i) {
            const quint16 following = in.u16(p);
            qsizetype valueEnd;
            node.attributes.append(attribute(p, &valueEnd));
            if (valueEnd > in.data().size())
                return std::nullopt;
            p = following ? p + following : valueEnd;
        }
        if (child)
            p = at + child;
        for (int i = 0; i < children; ++i) {
            qsizetype childEnd;
            std::optional<Node> kid = this->node(p, &childEnd);
            if (!kid)
                return std::nullopt;
            node.children.append(std::move(*kid));
            p = childEnd;
        }
        *end = next ? at + next : p;
        --depth;
        return node;
    }
};

} // namespace

QStringList interfaces(const QString &tag)
{
    static const QHash<QString, QStringList> own{
        {QStringLiteral("view"), {QStringLiteral("IWMPLayoutSubView"), QStringLiteral("IWMPLayoutView")}},
        {QStringLiteral("subview"), {QStringLiteral("IWMPLayoutSubView")}},
        {QStringLiteral("theme"), {QStringLiteral("IWMPTheme")}},
        {QStringLiteral("button"), {QStringLiteral("IWMPButtonCtrl")}},
        {QStringLiteral("buttongroup"), {QStringLiteral("IWMPRegionalButtonCtrl")}},
        {QStringLiteral("buttonelement"), {QStringLiteral("IWMPRegionalButton")}},
        {QStringLiteral("slider"), {QStringLiteral("IWMPSliderCtrl")}},
        {QStringLiteral("progressbar"), {QStringLiteral("IWMPSliderCtrl")}},
        {QStringLiteral("customslider"), {QStringLiteral("IWMPCustomSlider")}},
        {QStringLiteral("text"), {QStringLiteral("IWMPTextCtrl")}},
        {QStringLiteral("video"), {QStringLiteral("IWMPVideoCtrl")}},
        {QStringLiteral("effects"), {QStringLiteral("IWMPEffectsCtrl")}},
        {QStringLiteral("playlist"), {QStringLiteral("IWMPPlaylistCtrl")}},
        {QStringLiteral("equalizersettings"), {QStringLiteral("IWMPEqualizerSettingsCtrl")}},
        {QStringLiteral("videosettings"), {QStringLiteral("IWMPVideoSettingsCtrl")}},
        {QStringLiteral("librarytree"), {QStringLiteral("IWMPLibraryTreeCtrl")}},
        {QStringLiteral("editbox"), {QStringLiteral("IWMPEditCtrl")}},
        {QStringLiteral("listbox"), {QStringLiteral("IWMPListBoxCtrl")}},
        {QStringLiteral("popup"), {QStringLiteral("IWMPListBoxCtrl")}},
        {QStringLiteral("menu"), {QStringLiteral("IWMPMenuCtrl")}},
        {QStringLiteral("automenu"), {QStringLiteral("IWMPAutoMenuCtrl")}},
        {QStringLiteral("plugin"), {QStringLiteral("IWMPPluginUIHost")}},
    };
    // A theme is not an element: it has no position.
    if (tag == QLatin1String("theme"))
        return own.value(tag);
    return QStringList{QStringLiteral("IWMPObjectExtendedProps")} + own.value(tag);
}

Properties properties(const w4w::PeResources &wmp)
{
    Properties result;
    QHash<quint32, QString> &names = result.names;
    const QByteArray data = wmp.find(QStringLiteral("TYPELIB"), QStringLiteral("1"));
    const Reader in(data);
    if (data.left(4) != "MSFT")
        return result;
    // MSFT: the header, the type infos' offsets, then 15 segments (offset, length, two reserved)
    const quint32 flags = in.u32(20), count = in.u32(32);
    const qsizetype offsets = 0x54 + ((flags & 0x100) ? 4 : 0);
    const qsizetype segments = offsets + 4 * qsizetype(count);
    const auto segment = [&](int i) { return qint32(in.u32(segments + 16 * i)); };
    const qsizetype typeInfos = segment(0), nameTable = segment(7);
    const auto name = [&](qint32 offset) {
        const qsizetype at = nameTable + offset;
        return QString::fromLatin1(data.mid(at + 12, in.u8(at + 8)));
    };
    QSet<QString> wanted;
    for (const char *interface : ElementInterfaces)
        wanted.insert(QLatin1String(interface));
    wanted.insert(QStringLiteral("IWMPTheme"));
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype info = typeInfos + 0x64 * qsizetype(i);
        const QString interface = name(qint32(in.u32(info + 13 * 4)));
        if (!wanted.contains(interface))
            continue;
        const qint32 members = qint32(in.u32(info + 4));
        const quint32 elements = in.u32(info + 24);
        const int functions = int(elements & 0xffff), total = functions + int(elements >> 16);
        if (members <= 0 || !total)
            continue;
        // the records (a function's invoke kind in bits 3-6 of its word at 16: 1 a method),
        // then each member's ID, then each one's name
        const qsizetype ids = members + 4 + in.u32(members);
        QHash<QString, bool> &own = result.members[interface];
        qsizetype record = members + 4;
        for (int m = 0; m < total; ++m) {
            const bool method = m < functions && ((in.u32(record + 16) >> 3) & 0xf) == 1;
            record += in.u16(record);
            const qint32 offset = qint32(in.u32(ids + 4 * (total + m)));
            if (offset < 0)
                continue;
            const QString member = name(offset);
            if (interface != QLatin1String("IWMPTheme"))
                names.insert(in.u32(ids + 4 * m), member);
            own.insert(member.toLower(), method || own.value(member.toLower()));
        }
    }
    return result;
}

std::optional<Node> readCompiled(const QByteArray &data, const QHash<quint32, QString> &names,
                                 const std::function<QString(int)> &strings)
{
    const Reader in(data);
    Compiled compiled{in, names, strings};
    qsizetype end;
    return compiled.node(0, &end);
}

namespace {

std::optional<Node> readElement(QXmlStreamReader &xml)
{
    Node node;
    node.tag = xml.name().toString().toLower();
    for (const QXmlStreamAttribute &attribute : xml.attributes())
        node.attributes.append({attribute.name().toString(), attribute.value().toString()});
    while (xml.readNextStartElement())
        if (std::optional<Node> child = readElement(xml))
            node.children.append(std::move(*child));
    return xml.hasError() ? std::nullopt : std::optional<Node>(std::move(node));
}

} // namespace

std::optional<Node> readXml(const QByteArray &data)
{
    // Skins are often UTF-16 without an encoding declaration, or 8-bit with a wrong one.
    QString text;
    if (data.startsWith("\xff\xfe") || (data.size() > 1 && data[1] == '\0'))
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + (data.startsWith("\xff\xfe") ? 2 : 0)),
                                  (data.size() - (data.startsWith("\xff\xfe") ? 2 : 0)) / 2);
    else
        text = QString::fromLatin1(data);
    // The declaration names an encoding the text no longer is in.
    if (text.startsWith(QLatin1String("<?xml")))
        text = text.mid(text.indexOf(QLatin1String("?>")) + 2);
    // Skins write bare ampersands and comparisons in scripts.
    QXmlStreamReader xml(text);
    if (!xml.readNextStartElement())
        return std::nullopt;
    return readElement(xml);
}

} // namespace wmpskin
