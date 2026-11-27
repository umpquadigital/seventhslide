#pragma once

// The library's services, newest first (ServiceRepository::list_services).
// ServiceBrowser groups them by date for the Services window.

#include <seventhslide/model/content.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <optional>
#include <vector>

namespace seventhslide::app {

class ServiceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        ServiceIdRole = Qt::UserRole + 1,
        NameRole,
        IsOpenRole,
        /// The service's tags, as a list of strings.
        TagsRole,
        CreatedAtRole,
        ServiceAtRole,
    };

    explicit ServiceListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void set_services(std::vector<model::Service> services);
    void set_open_service_id(std::optional<std::int64_t> id);
    [[nodiscard]] const std::vector<model::Service>& services() const noexcept { return services_; }
    [[nodiscard]] std::optional<std::int64_t> open_service_id() const noexcept { return open_id_; }

    /// Test seam: every row as QML reads it, by role name.
    [[nodiscard]] QVariantList snapshot() const;

signals:
    /// The rows changed in any way, the open service included.
    void servicesChanged();

private:
    std::vector<model::Service> services_;
    std::optional<std::int64_t> open_id_;
};

} // namespace seventhslide::app
