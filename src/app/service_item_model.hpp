#pragma once

// QAbstractListModel over Session service items.

#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace seventhslide::app {

class ServiceItemModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns its list models; QML reads their roles by name")
    /// The ids of the items in an Also-play set (``LinkSetRole`` is not -1).
    Q_PROPERTY(QVariantList linkedIds READ linkedIds NOTIFY linkedIdsChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ItemTypeRole,
        ItemIndexRole,
        ItemIdRole,
        SelectedRole,
        HasOverridesRole,
        /// 1-based ordinal among non-divider items; 0 for dividers.
        DisplayNumberRole,
        /// Filenames snapshot for ``image_folder`` items (empty otherwise).
        FolderImagesRole,
        /// An ``image`` item's file in the library (empty otherwise): its
        /// title is the image's, the file named for good on import.
        ImageFileRole,
        /// The library file an ``image``, ``video`` or ``audio`` item plays
        /// (empty otherwise), and its kind as QML names it (``MediaKind``; -1
        /// otherwise).
        MediaFileRole,
        MediaKindRole,
        StackIdRole,
        StackIconRole,
        AlsoPlayIdsRole,
        AlsoPlayWithRole,
        /// Which Also-play set the row belongs to, numbered 0, 1, 2... in
        /// rundown order; -1 when it is not linked.
        LinkSetRole,
        /// The focused stack's Preview holds the row: it is cued.
        CuedRole,
    };
    Q_ENUM(Roles)

    explicit ServiceItemModel(session::Session& session, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QVariantList linkedIds() const;

    /// Re-read the rundown. When the rows are the same items in the same order
    /// (a selection or content change), this is a ``dataChanged`` over the list;
    /// with items come and gone, it is those rows alone. Only another rundown
    /// is a reset, so QML keeps its delegates and scroll position.
    void reload();
    /// Re-read only which row is live and which is cued, after the focused
    /// stack changes.
    void notify_selection_changed();

signals:
    void linkedIdsChanged();

private:
    friend struct RowSteps;

    /// How many rows there are while ``reload`` announces them step by step.
    std::optional<int> rows_in_step_;

    /// Row data that would otherwise be recomputed per row per role.
    struct Derived {
        std::vector<std::int64_t> item_ids;
        /// 1-based ordinal among non-divider rows; 0 for a divider.
        std::vector<int> display_numbers;
        /// Item id -> title of the row that plays it alongside itself.
        std::unordered_map<std::int64_t, QString> also_play_with;
        /// Item id -> its Also-play set's number (see ``LinkSetRole``).
        std::unordered_map<std::int64_t, int> link_set;
    };

    void rebuild_derived();
    /// Tell views how the rows changed from ``previous_ids`` to now.
    void announce_rows(const std::vector<std::int64_t>& previous_ids);
    [[nodiscard]] bool derived_matches_rows() const;

    session::Session& session_;
    Derived derived_;
};

} // namespace seventhslide::app
