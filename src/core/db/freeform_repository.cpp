#include "seventhslide/db/freeform_repository.hpp"

#include "seventhslide/db/list_order.hpp"
#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"
#include "seventhslide/freeform/compile.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/json/read.hpp"

#include <stdexcept>
#include <unordered_set>

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {
namespace {

model::FreeformItem item_from_row(QSqlQuery& q)
{
    model::FreeformItem item;
    item.id = q.value(0).toLongLong();
    item.name = q.value(1).toString().toStdString();
    if (!q.value(2).isNull()) {
        item.folder_id = q.value(2).toLongLong();
    }
    item.sort_order = q.value(3).toLongLong();
    item.tags = json::strings_in(parse_json_text(q.value(4).toString(), nlohmann::json::array()));
    item.document = parse_json_text(q.value(5).toString(), nlohmann::json::object());
    if (!item.document.is_object()) {
        item.document = nlohmann::json::object();
    }
    item.thumb_output_id = q.value(6).toString().toStdString();
    item.thumb_key = q.value(7).toString().toStdString();
    return item;
}

void stamp_thumb(model::FreeformItem& item)
{
    item.thumb_output_id.clear();
    item.thumb_key.clear();
    const auto doc = freeform::load_document(item.document, item.name);
    if (!doc) {
        return;
    }
    const auto ref = freeform::library_thumb_ref(*doc);
    if (!ref) {
        return;
    }
    item.thumb_output_id = ref->output_id;
    item.thumb_key = ref->preview_key;
}

}  // namespace

FolderTree FreeformRepository::folders() const
{
    return FolderTree(db_, "freeform_folders");
}

FolderedRows FreeformRepository::rows() const
{
    return FolderedRows(db_, "freeform_items", "freeform_folders", "freeform item");
}

std::vector<std::int64_t> FreeformRepository::delete_folder_tree(std::int64_t id)
{
    return rows().delete_folder_tree(id);
}

std::int64_t FreeformRepository::create_item(model::FreeformItem& item)
{
    stamp_thumb(item);
    const auto sort_order = next_order(db_, {"freeform_items", "sort_order", "folder_id", item.folder_id});

    const auto sql = QStringLiteral(
        "INSERT INTO freeform_items (name, folder_id, sort_order, tags, document, "
        "thumb_output_id, thumb_key) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    exec(db_, sql,
         {QString::fromStdString(item.name),
          sql_id(item.folder_id),
          static_cast<qint64>(sort_order),
          json_to_qstring(nlohmann::json(item.tags)),
          json_to_qstring(item.document),
          QString::fromStdString(item.thumb_output_id),
          QString::fromStdString(item.thumb_key)},
         "create freeform item");
    item.id = db_.last_insert_id();
    item.sort_order = sort_order;
    return *item.id;
}

void FreeformRepository::update_item(const model::FreeformItem& item)
{
    if (!item.id) {
        throw std::invalid_argument("update_item requires id");
    }
    model::FreeformItem stamped = item;
    stamp_thumb(stamped);
    const auto sql = QStringLiteral(
        "UPDATE freeform_items SET name=?, folder_id=?, sort_order=?, tags=?, document=?, "
        "thumb_output_id=?, thumb_key=? WHERE id=?");
    exec(db_, sql,
         {QString::fromStdString(stamped.name),
          sql_id(stamped.folder_id),
          static_cast<qint64>(stamped.sort_order),
          json_to_qstring(nlohmann::json(stamped.tags)),
          json_to_qstring(stamped.document),
          QString::fromStdString(stamped.thumb_output_id),
          QString::fromStdString(stamped.thumb_key),
          static_cast<qint64>(*item.id)},
         "update freeform item");
}

std::optional<model::FreeformItem> FreeformRepository::get_item(std::int64_t id)
{
    const auto sql = QStringLiteral(
        "SELECT id, name, folder_id, sort_order, tags, document, "
        "thumb_output_id, thumb_key "
        "FROM freeform_items WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(id)}, "get freeform item");
    if (!q.next()) {
        return std::nullopt;
    }
    return item_from_row(q);
}

std::vector<model::FreeformItem> FreeformRepository::list_items_in_folder(
    std::optional<std::int64_t> folder_id)
{
    const auto sql = QStringLiteral(
        "SELECT id, name, folder_id, sort_order, tags, document, "
        "thumb_output_id, thumb_key "
        "FROM freeform_items WHERE folder_id IS ? ORDER BY sort_order ASC, uid ASC");
    auto q = exec(db_, sql, {sql_id(folder_id)}, "list freeform items in folder");
    std::vector<model::FreeformItem> out;
    while (q.next()) {
        out.push_back(item_from_row(q));
    }
    return out;
}

std::vector<model::FreeformItem> FreeformRepository::list_items()
{
    const auto sql = QStringLiteral(
        "SELECT id, name, folder_id, sort_order, tags, document, "
        "thumb_output_id, thumb_key "
        "FROM freeform_items ORDER BY folder_id, sort_order ASC, uid ASC");
    auto q = exec(db_, sql, {}, "list freeform items");
    std::vector<model::FreeformItem> out;
    while (q.next()) {
        out.push_back(item_from_row(q));
    }
    return out;
}

std::vector<FreeformBrowserRow> FreeformRepository::list_browser_rows(
    std::optional<std::int64_t> folder_id)
{
    const auto sql = QStringLiteral(
        "SELECT id, name, thumb_output_id, thumb_key "
        "FROM freeform_items WHERE folder_id IS ? ORDER BY sort_order ASC, uid ASC");
    auto q = exec(db_, sql, {sql_id(folder_id)}, "list freeform browser rows");
    std::vector<FreeformBrowserRow> out;
    while (q.next()) {
        FreeformBrowserRow row;
        row.id = q.value(0).toLongLong();
        row.name = q.value(1).toString().toStdString();
        row.thumb_output_id = q.value(2).toString().toStdString();
        row.thumb_key = q.value(3).toString().toStdString();
        out.push_back(std::move(row));
    }
    return out;
}

std::unordered_map<std::int64_t, int> FreeformRepository::folder_item_counts()
{
    return rows().counts();
}

bool FreeformRepository::move_item(std::int64_t id, std::optional<std::int64_t> new_folder_id)
{
    return rows().move(id, new_folder_id);
}

void FreeformRepository::reorder_items(std::optional<std::int64_t> folder_id,
                                       const std::vector<std::int64_t>& ordered_item_ids)
{
    rows().reorder(folder_id, ordered_item_ids);
}

int FreeformRepository::move_items(const std::vector<std::int64_t>& ids,
                                   std::optional<std::int64_t> new_folder_id)
{
    return rows().move_all(ids, new_folder_id);
}

bool FreeformRepository::remove_item(std::int64_t id)
{
    return rows().remove(id);
}

}  // namespace seventhslide::db
