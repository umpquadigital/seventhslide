#pragma once

// Canvas edits the way the editor stages send them.

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace seventhslide::test {

/// One object's box for ``setGeometries``, as a drag or a resize sends it.
[[nodiscard]] inline QVariantList geometry(const QString& object_id, double x, double y, double w,
                                           double h, double rotation = 0)
{
    return {QVariantMap{{QStringLiteral("id"), object_id},
                        {QStringLiteral("x"), x},
                        {QStringLiteral("y"), y},
                        {QStringLiteral("w"), w},
                        {QStringLiteral("h"), h},
                        {QStringLiteral("rotation"), rotation}}};
}

}  // namespace seventhslide::test
