#include "author_list_model.hpp"

#include <QVariantMap>

namespace seventhslide::app {
namespace {

const QString k_type = QStringLiteral("type");
const QString k_label = QStringLiteral("label");
const QString k_lang = QStringLiteral("lang");
const QString k_other = QStringLiteral("other");

[[nodiscard]] QVariantMap credit(const QString& type, const QString& label = {},
                                 const QString& lang = {})
{
    return {{k_type, type}, {k_label, label}, {k_lang, lang}};
}

} // namespace

int AuthorListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant AuthorListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !has_row(index.row())) {
        return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case NameRole:
        return row.name;
    case CreditsRole:
        return row.credits;
    default:
        return {};
    }
}

QHash<int, QByteArray> AuthorListModel::roleNames() const
{
    return {{NameRole, "name"}, {CreditsRole, "credits"}};
}

void AuthorListModel::load(const QVariantList& authors)
{
    beginResetModel();
    rows_.clear();
    for (const auto& entry : authors) {
        const auto map = entry.toMap();
        Row row{map.value(QStringLiteral("name")).toString().trimmed(), {}};
        if (row.name.isEmpty()) {
            continue;
        }
        for (const auto& raw : map.value(QStringLiteral("roles")).toList()) {
            const auto r = raw.toMap();
            const auto type = r.value(k_type).toString();
            if (!type.isEmpty()) {
                row.credits.push_back(
                    credit(type, r.value(k_label).toString(), r.value(k_lang).toString()));
            }
        }
        rows_.push_back(std::move(row));
    }
    endResetModel();
    emit countChanged();
}

void AuthorListModel::clear()
{
    load({});
}

void AuthorListModel::append(const QString& name)
{
    beginInsertRows({}, count(), count());
    rows_.push_back(Row{name, {}});
    endInsertRows();
    emit countChanged();
}

void AuthorListModel::remove(int row)
{
    if (!has_row(row)) {
        return;
    }
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    emit countChanged();
}

QString AuthorListModel::nameAt(int row) const
{
    return has_row(row) ? rows_[static_cast<std::size_t>(row)].name : QString();
}

void AuthorListModel::setName(int row, const QString& name)
{
    if (!has_row(row)) {
        return;
    }
    rows_[static_cast<std::size_t>(row)].name = name;
    const auto at = index(row);
    emit dataChanged(at, at, {NameRole});
}

QVariantList AuthorListModel::credits_at(int row) const
{
    return has_row(row) ? rows_[static_cast<std::size_t>(row)].credits : QVariantList();
}

template <class Match>
int AuthorListModel::find_credit(int row, Match matches) const
{
    const auto& credits = rows_[static_cast<std::size_t>(row)].credits;
    for (qsizetype i = 0; i < credits.size(); ++i) {
        if (matches(credits[i].toMap())) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AuthorListModel::set_credits(int row, QVariantList credits)
{
    rows_[static_cast<std::size_t>(row)].credits = std::move(credits);
    const auto at = index(row);
    emit dataChanged(at, at, {CreditsRole});
}

void AuthorListModel::toggleCredit(int row, const QString& type)
{
    if (!has_row(row)) {
        return;
    }
    auto credits = credits_at(row);
    const int found = find_credit(row, [&](const QVariantMap& c) { return c.value(k_type) == type; });
    if (found >= 0) {
        credits.removeAt(found);
    } else {
        credits.push_back(credit(type));
    }
    set_credits(row, std::move(credits));
}

void AuthorListModel::addCustomCredit(int row, const QString& label)
{
    const auto text = label.trimmed();
    if (!has_row(row) || text.isEmpty()) {
        return;
    }
    const auto same = [&](const QVariantMap& c) {
        return c.value(k_type) == k_other && c.value(k_label) == text;
    };
    if (find_credit(row, same) >= 0) {
        return;
    }
    auto credits = credits_at(row);
    credits.push_back(credit(k_other, text));
    set_credits(row, std::move(credits));
}

void AuthorListModel::toggleCustomCredit(int row, const QString& label)
{
    const auto text = label.trimmed();
    if (!has_row(row) || text.isEmpty()) {
        return;
    }
    auto credits = credits_at(row);
    const int found = find_credit(row, [&](const QVariantMap& c) {
        return c.value(k_type) == k_other && c.value(k_label) == text;
    });
    if (found >= 0) {
        credits.removeAt(found);
    } else {
        credits.push_back(credit(k_other, text));
    }
    set_credits(row, std::move(credits));
}

QVariantList AuthorListModel::authors() const
{
    QVariantList out;
    for (const auto& row : rows_) {
        const auto name = row.name.trimmed();
        if (!name.isEmpty()) {
            out.push_back(QVariantMap{{QStringLiteral("name"), name},
                                      {QStringLiteral("roles"), row.credits}});
        }
    }
    return out;
}

} // namespace seventhslide::app
