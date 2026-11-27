#include "display_list_model.hpp"

namespace seventhslide::app {

DisplayListModel::DisplayListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int DisplayListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant DisplayListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KeyRole:
        return row.key;
    case NameRole:
        return row.name;
    case LabelRole:
        return row.label;
    case AssignedOutputRole:
        return row.assigned_output;
    default:
        return {};
    }
}

QHash<int, QByteArray> DisplayListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {NameRole, "name"},
        {LabelRole, "label"},
        {AssignedOutputRole, "assignedOutput"},
    };
}

void DisplayListModel::set_displays(const QVariantList& displays)
{
    std::vector<Row> rows;
    rows.reserve(static_cast<std::size_t>(displays.size()));
    for (const auto& d : displays) {
        const auto map = d.toMap();
        Row row;
        row.key = map.value(QStringLiteral("key")).toString();
        row.name = map.value(QStringLiteral("name")).toString();
        if (row.name.isEmpty()) {
            row.name = row.key;
        }
        row.label = map.value(QStringLiteral("label")).toString();
        if (row.label.isEmpty()) {
            row.label = row.name;
        }
        row.assigned_output = map.value(QStringLiteral("assignedOutput")).toString();
        rows.push_back(std::move(row));
    }
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

} // namespace seventhslide::app
