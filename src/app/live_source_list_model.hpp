#pragma once

// The Live tab's rows: the sources found now that its search matches. Each new
// list is taken in by key, so a row that changed in place (a frame rate
// learned, a signal lost) keeps its delegate and the list keeps its place.

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>

#include <vector>

namespace seventhslide::app {

class LiveSourceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The live library owns its rows; QML reads their roles by name")

public:
    enum Roles {
        /// The source's key (model::live_source_key): what it is selected by.
        KeyRole = Qt::UserRole + 1,
        KindRole,
        KindLabelRole,
        NameRole,
        /// A stream's address; empty for the others.
        UrlRole,
        DetailRole,
        /// ``ok``, ``lost`` or empty (LiveLibraryController::sources).
        SignalRole,
    };
    Q_ENUM(Roles)

    struct Row {
        QString key;
        QString kind;
        QString kind_label;
        QString name;
        QString url;
        QString detail;
        QString signal;
        bool operator==(const Row&) const = default;
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Show ``rows``, in their order: rows gone are removed, new ones
    /// inserted, moved ones moved and changed ones refreshed. A key in
    /// ``rows`` twice has two rows.
    void set_rows(const std::vector<Row>& rows);

private:
    std::vector<Row> rows_;
};

} // namespace seventhslide::app
