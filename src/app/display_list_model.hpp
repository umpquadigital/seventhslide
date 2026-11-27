#pragma once

// Display / output-strip helpers.

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <vector>

namespace seventhslide::app {

/// Physical screens from display_rows() (destination_rows.hpp).
class DisplayListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        LabelRole,
        AssignedOutputRole,
    };

    explicit DisplayListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// ``displays`` are maps from display_rows() (destination_rows.hpp). Of those,
    /// the dropdown shows key, name, label and assignedOutput; they are read
    /// here once, not per paint.
    void set_displays(const QVariantList& displays);

private:
    /// One display as the roles give it. The name and label already fall
    /// back (a label to the name, a name to the key), so a view shows label.
    struct Row {
        QString key;
        QString name;
        QString label;
        QString assigned_output;
    };
    std::vector<Row> rows_;
};

} // namespace seventhslide::app
