#include "service_item_model.hpp"

#include "list_model_replace.hpp"
#include "media_kind_qml.hpp"

#include "qml_ids.hpp"

#include <seventhslide/model/content.hpp>
#include <seventhslide/model/stack.hpp>

#include <QStringList>

namespace seventhslide::app {

ServiceItemModel::ServiceItemModel(session::Session& session, QObject* parent)
    : QAbstractListModel(parent)
    , session_(session)
{
}

int ServiceItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return rows_in_step_ ? *rows_in_step_ : static_cast<int>(session_.service_items().size());
}

QVariant ServiceItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto& items = session_.service_items();
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= items.size()) {
        return {};
    }
    const auto& item = items[row];
    switch (role) {
    case TitleRole: {
        return QString::fromStdString(item.title());
    }
    case ItemTypeRole:
        return QString::fromStdString(item.item_type);
    case ItemIndexRole:
        return index.row();
    case ItemIdRole:
        return qml_id(item.item_id);
    case SelectedRole: {
        // Blue marks what the focused stack is showing. Other stacks may be
        // live with linked items, but they are not the one being driven.
        // A row with no id cannot be matched to a stack, so it keeps the
        // plain last-selected rule.
        if (!item.item_id) {
            return index.row() == session_.service_item_index();
        }
        return item.item_id == session_.focused_stack().program.service_item_id();
    }
    case CuedRole: {
        const auto& preview = session_.focused_stack().preview;
        return item.item_id && preview && item.item_id == preview->service_item_id();
    }
    case HasOverridesRole:
        return item.has_overrides();
    case DisplayNumberRole: {
        if (item.item_type == model::k_item_type_divider) {
            return 0;
        }
        if (derived_matches_rows()) {
            return derived_.display_numbers[row];
        }
        int num = 0;
        for (std::size_t i = 0; i <= row; ++i) {
            if (items[i].item_type != model::k_item_type_divider) {
                ++num;
            }
        }
        return num;
    }
    case FolderImagesRole: {
        QStringList names;
        if (item.item_type == model::k_item_type_image_folder && item.data.is_object()) {
            const auto& images = item.data.value("images", nlohmann::json::array());
            if (images.is_array()) {
                names.reserve(static_cast<int>(images.size()));
                for (const auto& v : images) {
                    if (v.is_string()) {
                        names.push_back(QString::fromStdString(v.get<std::string>()));
                    }
                }
            }
        }
        return names;
    }
    case ImageFileRole:
        if (item.item_type == model::k_item_type_image && item.data.is_object()) {
            const auto file = item.data.find("filename");
            if (file != item.data.end() && file->is_string()) {
                return QString::fromStdString(file->get<std::string>());
            }
        }
        return QString();
    case MediaFileRole:
    case MediaKindRole: {
        const auto kind = item.item_type == model::k_item_type_image   ? std::optional(MediaKindQml::Image)
                          : item.item_type == model::k_item_type_video ? std::optional(MediaKindQml::Video)
                          : item.item_type == model::k_item_type_audio ? std::optional(MediaKindQml::Audio)
                                                                       : std::nullopt;
        const auto file = kind && item.data.is_object() ? item.data.find("filename") : item.data.end();
        const bool named = kind && file != item.data.end() && file->is_string();
        if (role == MediaKindRole) {
            return named ? static_cast<int>(*kind) : -1;
        }
        return named ? QString::fromStdString(file->get<std::string>()) : QString();
    }
    case StackIdRole:
        return QString::fromStdString(model::service_item_stack_id(item));
    case StackIconRole: {
        const auto sid = model::service_item_stack_id(item);
        const auto* def = sid.empty()
            ? nullptr
            : model::find_stack(session_.stack_defs(), sid);
        if (def) {
            return QString::fromStdString(def->icon);
        }
        const auto resolved = session_.resolve_planned_stack(item.item_type, sid);
        def = model::find_stack(session_.stack_defs(), resolved);
        return QString::fromStdString(def ? def->icon
                                          : std::string(model::k_default_stack_icon));
    }
    case AlsoPlayIdsRole: {
        QVariantList ids;
        for (const auto id : model::service_item_also_play(item)) {
            ids.push_back(static_cast<qint64>(id));
        }
        return ids;
    }
    case AlsoPlayWithRole: {
        if (!item.item_id) {
            return QString();
        }
        const auto self = *item.item_id;
        if (derived_matches_rows()) {
            const auto it = derived_.also_play_with.find(self);
            return it == derived_.also_play_with.end() ? QString() : it->second;
        }
        for (const auto& other : items) {
            if (!other.item_id || *other.item_id == self) {
                continue;
            }
            for (const auto id : model::service_item_also_play(other)) {
                if (id == self) {
                    return QString::fromStdString(other.title());
                }
            }
        }
        return QString();
    }
    case LinkSetRole: {
        if (!item.item_id || !derived_matches_rows()) {
            return -1;
        }
        const auto it = derived_.link_set.find(*item.item_id);
        return it == derived_.link_set.end() ? -1 : it->second;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> ServiceItemModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ItemTypeRole, "itemType"},
        {ItemIndexRole, "itemIndex"},
        {ItemIdRole, "itemId"},
        {SelectedRole, "selected"},
        {HasOverridesRole, "hasOverrides"},
        {DisplayNumberRole, "displayNumber"},
        {FolderImagesRole, "folderImages"},
        {ImageFileRole, "imageFile"},
        {MediaFileRole, "mediaFile"},
        {MediaKindRole, "mediaKind"},
        {StackIdRole, "stackId"},
        {StackIconRole, "stackIcon"},
        {AlsoPlayIdsRole, "alsoPlayIds"},
        {AlsoPlayWithRole, "alsoPlayWith"},
        {LinkSetRole, "linkSet"},
        {CuedRole, "cued"},
    };
}

void ServiceItemModel::rebuild_derived()
{
    const auto& items = session_.service_items();
    derived_ = {};
    derived_.item_ids.reserve(items.size());
    derived_.display_numbers.reserve(items.size());

    int num = 0;
    for (const auto& item : items) {
        derived_.item_ids.push_back(item.item_id ? *item.item_id : -1);
        if (item.item_type == model::k_item_type_divider) {
            derived_.display_numbers.push_back(0);
        } else {
            derived_.display_numbers.push_back(++num);
        }
    }

    // Reverse also-play map: who lists me as a companion.
    for (const auto& other : items) {
        if (!other.item_id) {
            continue;
        }
        for (const auto id : model::service_item_also_play(other)) {
            if (id == *other.item_id) {
                continue;
            }
            derived_.also_play_with.try_emplace(
                id, QString::fromStdString(other.title()));
        }
    }

    // Number the Also-play sets in the order their first member appears.
    // Only a row that holds links or is listed by one can be in a set, so
    // the rest skip the walk over the rundown.
    int next_set = 0;
    for (const auto& item : items) {
        if (!item.item_id || derived_.link_set.count(*item.item_id)) {
            continue;
        }
        if (model::service_item_also_play(item).empty()
            && !derived_.also_play_with.count(*item.item_id)) {
            continue;
        }
        const auto members = session_.also_play_set(*item.item_id);
        if (members.empty()) {
            continue;
        }
        for (const auto id : members) {
            derived_.link_set.try_emplace(id, next_set);
        }
        ++next_set;
    }
}

bool ServiceItemModel::derived_matches_rows() const
{
    return derived_.display_numbers.size() == session_.service_items().size();
}

QVariantList ServiceItemModel::linkedIds() const
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(derived_.link_set.size()));
    for (const auto id : derived_.item_ids) {
        if (derived_.link_set.count(id)) {
            out.push_back(QVariant::fromValue(id));
        }
    }
    return out;
}

void ServiceItemModel::reload()
{
    const auto previous_ids = derived_.item_ids;
    const auto previous_links = derived_.link_set;
    rebuild_derived();
    announce_rows(previous_ids);
    // A link made or undone can leave every row where it was, and a view's
    // binding does not see a change in a row's data, so it is said apart.
    if (previous_links != derived_.link_set) {
        emit linkedIdsChanged();
    }
}

void ServiceItemModel::announce_rows(const std::vector<std::int64_t>& previous_ids)
{
    if (previous_ids == derived_.item_ids && !derived_.item_ids.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    // The rows that came and went alone, when others stay: the session has
    // all of it already, so only the count goes step by step.
    if (const auto kept = kept_rows(previous_ids, derived_.item_ids)) {
        rows_in_step_ = static_cast<int>(previous_ids.size());
        RowSteps::apply(
            *this, *kept, [this](int first, int last) { *rows_in_step_ -= last - first + 1; }, [] {},
            [this](int first, int last) { *rows_in_step_ += last - first + 1; });
        rows_in_step_.reset();
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    endResetModel();
}

void ServiceItemModel::notify_selection_changed()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole, CuedRole});
    }
}

} // namespace seventhslide::app
