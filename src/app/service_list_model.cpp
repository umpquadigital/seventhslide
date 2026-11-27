#include "service_list_model.hpp"

#include "qml_ids.hpp"
#include "role_rows.hpp"

#include <QStringList>

namespace seventhslide::app {

ServiceListModel::ServiceListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ServiceListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(services_.size());
}

QVariant ServiceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= services_.size()) {
        return {};
    }
    const auto& s = services_[row];
    switch (role) {
    case ServiceIdRole:
        return qml_id(s.id);
    case NameRole:
        return QString::fromStdString(s.name);
    case IsOpenRole:
        return open_id_.has_value() && s.id.has_value() && *s.id == *open_id_;
    case TagsRole: {
        QStringList tags;
        for (const auto& t : s.tags) {
            tags.push_back(QString::fromStdString(t));
        }
        return tags;
    }
    case CreatedAtRole:
        return QString::fromStdString(s.created_at);
    case ServiceAtRole:
        return QString::fromStdString(s.service_at);
    default:
        return {};
    }
}

QHash<int, QByteArray> ServiceListModel::roleNames() const
{
    return {
        {ServiceIdRole, "serviceId"},
        {NameRole, "name"},
        {IsOpenRole, "isOpen"},
        {TagsRole, "tags"},
        {CreatedAtRole, "createdAt"},
        {ServiceAtRole, "serviceAt"},
    };
}

void ServiceListModel::set_services(std::vector<model::Service> services)
{
    beginResetModel();
    services_ = std::move(services);
    endResetModel();
    emit servicesChanged();
}

void ServiceListModel::set_open_service_id(std::optional<std::int64_t> id)
{
    if (open_id_ == id) {
        return;
    }
    open_id_ = id;
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {IsOpenRole});
    }
    emit servicesChanged();
}

QVariantList ServiceListModel::snapshot() const
{
    return role_rows(*this);
}

} // namespace seventhslide::app
