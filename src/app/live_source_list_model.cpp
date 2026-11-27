#include "live_source_list_model.hpp"

#include <QSet>

#include <utility>

namespace seventhslide::app {

int LiveSourceListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LiveSourceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KeyRole:
        return row.key;
    case KindRole:
        return row.kind;
    case KindLabelRole:
        return row.kind_label;
    case NameRole:
        return row.name;
    case UrlRole:
        return row.url;
    case DetailRole:
        return row.detail;
    case SignalRole:
        return row.signal;
    default:
        return {};
    }
}

QHash<int, QByteArray> LiveSourceListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {KindRole, "kind"},
        {KindLabelRole, "kindLabel"},
        {NameRole, "name"},
        {UrlRole, "url"},
        {DetailRole, "detail"},
        {SignalRole, "signal"},
    };
}

void LiveSourceListModel::set_rows(const std::vector<Row>& rows)
{
    QSet<QString> keys;
    for (const Row& row : rows) {
        keys.insert(row.key);
    }
    for (int i = rowCount() - 1; i >= 0; --i) {
        if (!keys.contains(rows_[static_cast<std::size_t>(i)].key)) {
            beginRemoveRows({}, i, i);
            rows_.erase(rows_.begin() + i);
            endRemoveRows();
        }
    }
    // What is left is in the new list. Where the order holds, as it does but
    // when a source comes or goes, each row is looked at once. Two sources
    // may share a key (two capture dongles of one make): each has a row.
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const Row& want = rows[static_cast<std::size_t>(i)];
        int at = -1;
        for (int j = i; j < rowCount(); ++j) {
            if (rows_[static_cast<std::size_t>(j)].key == want.key) {
                at = j;
                break;
            }
        }
        if (at < 0) {
            beginInsertRows({}, i, i);
            rows_.insert(rows_.begin() + i, want);
            endInsertRows();
            continue;
        }
        if (at != i) {
            beginMoveRows({}, at, at, {}, i);
            Row moved = std::move(rows_[static_cast<std::size_t>(at)]);
            rows_.erase(rows_.begin() + at);
            rows_.insert(rows_.begin() + i, std::move(moved));
            endMoveRows();
        }
        if (!(rows_[static_cast<std::size_t>(i)] == want)) {
            rows_[static_cast<std::size_t>(i)] = want;
            emit dataChanged(index(i), index(i));
        }
    }
    // A key that was there twice and is now there once leaves a row over.
    const int want_count = static_cast<int>(rows.size());
    if (rowCount() > want_count) {
        beginRemoveRows({}, want_count, rowCount() - 1);
        rows_.erase(rows_.begin() + want_count, rows_.end());
        endRemoveRows();
    }
}

} // namespace seventhslide::app
