#pragma once

// A list model's rows as QML reads them, for a view that wants them all at
// once (the service picker).

#include <QAbstractItemModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace seventhslide::app {

/// Every row of ``model`` as a map from role name to the value data() gives:
/// the names a delegate uses, so a role added to roleNames() is in it with
/// no other edit. The remote protocol's rows are model_rows (qvariant_json),
/// which name their wire fields one by one on purpose.
[[nodiscard]] inline QVariantList role_rows(const QAbstractItemModel& model)
{
    const auto roles = model.roleNames();
    QVariantList out;
    out.reserve(model.rowCount());
    for (int r = 0; r < model.rowCount(); ++r) {
        const QModelIndex idx = model.index(r, 0);
        QVariantMap row;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            row.insert(QString::fromUtf8(it.value()), model.data(idx, it.key()));
        }
        out.push_back(row);
    }
    return out;
}

}  // namespace seventhslide::app
