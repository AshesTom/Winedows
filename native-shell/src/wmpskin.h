// SPDX-License-Identifier: LGPL-2.1-or-later
// Windows Media Player's skins as files: the compiled .wsz that wmploc.dll holds (its full mode
// and its tasks are skins too), the .wms of a .wmz, and the names their attributes have, which are
// the properties of wmp.dll's type library. Only these resources are read; no code of wmp.dll runs.
#pragma once

#include "msstyles.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

#include <optional>

namespace wmpskin {

// An element of a skin: its tag as a .wms spells it (lower-case), its attributes in their order
// and its children. A value is text (with its jscript:, wmpprop:, wmpenabled: or res:// prefix as a
// .wms has it), a number or a boolean.
struct Node
{
    QString tag;
    QList<std::pair<QString, QVariant>> attributes;
    QList<Node> children;
    QVariant attribute(const QString &name) const;
};

// The skin elements' properties and methods, from the TYPELIB of wmp.dll: a compiled skin names
// its attributes by their DISPIDs, and a handler sees its element's members by name.
struct Properties
{
    QHash<quint32, QString> names;
    // interface (IWMPButtonCtrl...) -> lower-case member -> whether it is a method
    QHash<QString, QHash<QString, bool>> members;
};
Properties properties(const w4w::PeResources &wmp);
// The interfaces of an element by its tag, IWMPObjectExtendedProps first.
QStringList interfaces(const QString &tag);

// A compiled skin (RT_TEXT/*.WSZ of wmploc.dll, wmp.dll's own format):
//   a node:      u16 offset of its next sibling (0: the last), u16 offset of its first child,
//                u8 kind (0x80: a CLSID follows the name), u8 children, u8 attributes,
//                its name (UTF-16, for the global objects: player, mediacenter...), [CLSID]
//   an attribute: u16 offset of the next one, u8 type, then a DISPID (u32) or, for type & 0x1f == 0,
//                its name; its value is UTF-16 (type & 8), a string ID of wmploc.dll (type & 0x10),
//                a u32 (type & 4) or a VARIANT_BOOL (type & 1). Type 0x80 is a jscript: value with its
//                prefix taken away, 0x40 a wmpprop: one.
// STRINGS gives the words of a string ID (wmploc.dll's RT_STRING).
std::optional<Node> readCompiled(const QByteArray &data, const QHash<quint32, QString> &names,
                                 const std::function<QString(int)> &strings);
// A .wms (XML, UTF-16 or 8-bit).
std::optional<Node> readXml(const QByteArray &data);

} // namespace wmpskin
