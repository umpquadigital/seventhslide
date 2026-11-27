#pragma once

// Object geometry as both canvas editors' QML hands it over (a drag, a
// marquee move, a resize of several objects at once).

#include <seventhslide/freeform/doc_edit.hpp>

#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace seventhslide::app {

/// ``[{id, x, y, w|width, h|height, rotation, flipH|flip_h, flipV|flip_v}]``.
/// Flip is only written when the row carries it (or ``hasFlip``).
[[nodiscard]] inline std::vector<freeform::ObjectGeometry> geometries_from_qml(
    const QVariantList& list)
{
    std::vector<freeform::ObjectGeometry> out;
    out.reserve(static_cast<std::size_t>(list.size()));
    for (const auto& row : list) {
        const auto m = row.toMap();
        freeform::ObjectGeometry g;
        g.id = m.value(QStringLiteral("id")).toString().toStdString();
        g.x = m.value(QStringLiteral("x")).toDouble();
        g.y = m.value(QStringLiteral("y")).toDouble();
        g.w = m.value(QStringLiteral("w"), m.value(QStringLiteral("width"))).toDouble();
        g.h = m.value(QStringLiteral("h"), m.value(QStringLiteral("height"))).toDouble();
        g.rotation = m.value(QStringLiteral("rotation")).toDouble();
        if (m.contains(QStringLiteral("flipH")) || m.contains(QStringLiteral("flip_h"))
            || m.contains(QStringLiteral("hasFlip"))) {
            g.has_flip = true;
            g.flip_h = m.value(QStringLiteral("flipH"), m.value(QStringLiteral("flip_h"))).toBool();
            g.flip_v = m.value(QStringLiteral("flipV"), m.value(QStringLiteral("flip_v"))).toBool();
        }
        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace seventhslide::app
