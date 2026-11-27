#include "seventhslide/db/folder_tree.hpp"

#include "seventhslide/db/list_order.hpp"

#include "seventhslide/db/query.hpp"

#include <algorithm>

namespace seventhslide::db {
namespace {

[[nodiscard]] std::vector<model::Folder> folders_from(QSqlQuery& q)
{
    std::vector<model::Folder> out;
    while (q.next()) {
        model::Folder f;
        f.id = q.value(0).toLongLong();
        f.name = q.value(1).toString().toStdString();
        f.sort_order = q.value(2).toLongLong();
        if (!q.value(3).isNull()) {
            f.parent_id = q.value(3).toLongLong();
        }
        out.push_back(std::move(f));
    }
    return out;
}

/// ``sql`` with ``table`` for ``%1``.
[[nodiscard]] QString in_table(const std::string& table, const char* sql)
{
    return QString::fromLatin1(sql).arg(sql_text(table));
}

}  // namespace

FolderTree::FolderTree(Database& db, std::string_view table)
    : db_(db)
    , table_(table)
{
}

std::int64_t FolderTree::create(std::string_view name, std::optional<std::int64_t> parent)
{
    const auto sort_order = next_order(db_, {table_, "sort_order", "parent_id", parent});
    exec(db_, in_table(table_, "INSERT INTO %1 (name, sort_order, parent_id) VALUES (?, ?, ?)"),
         {sql_text(name), static_cast<qint64>(sort_order), sql_id(parent)}, "create folder");
    return db_.last_insert_id();
}

bool FolderTree::rename(std::int64_t id, std::string_view name)
{
    auto q = exec(db_, in_table(table_, "UPDATE %1 SET name = ? WHERE id = ?"),
                  {sql_text(name), static_cast<qint64>(id)}, "rename folder");
    return q.numRowsAffected() > 0;
}

bool FolderTree::move(std::int64_t id, std::optional<std::int64_t> parent)
{
    if (parent) {
        // A missing parent is refused here rather than by the foreign key,
        // which would throw.
        if (!get(*parent)) {
            return false;
        }
        // The subtree includes ``id`` itself, so this also refuses a self-parent.
        const auto subtree = subtree_ids(id);
        if (std::find(subtree.begin(), subtree.end(), *parent) != subtree.end()) {
            return false;
        }
    }
    const auto sort_order = next_order(db_, {table_, "sort_order", "parent_id", parent});
    auto q = exec(db_, in_table(table_, "UPDATE %1 SET parent_id = ?, sort_order = ? WHERE id = ?"),
                  {sql_id(parent), static_cast<qint64>(sort_order), static_cast<qint64>(id)}, "move folder");
    return q.numRowsAffected() > 0;
}

void FolderTree::reorder_children(std::optional<std::int64_t> parent,
                                  const std::vector<std::int64_t>& ordered_ids)
{
    reorder_rows(db_, {table_, "sort_order", "parent_id", parent}, ordered_ids);
}

std::optional<model::Folder> FolderTree::get(std::int64_t id) const
{
    auto q = exec(db_,
                  in_table(table_, "SELECT id, name, sort_order, parent_id FROM %1 WHERE id = ?"),
                  {static_cast<qint64>(id)}, "get folder");
    auto found = folders_from(q);
    if (found.empty()) {
        return std::nullopt;
    }
    return std::move(found.front());
}

std::vector<model::Folder> FolderTree::list() const
{
    auto q = exec(db_,
                  in_table(table_, "SELECT id, name, sort_order, parent_id FROM %1 "
                                   "ORDER BY sort_order ASC, uid ASC"),
                  {}, "list folders");
    return folders_from(q);
}

std::vector<model::Folder> FolderTree::children(std::optional<std::int64_t> parent) const
{
    auto q = exec(db_,
                  in_table(table_, "SELECT id, name, sort_order, parent_id FROM %1 "
                                   "WHERE parent_id IS ? ORDER BY sort_order ASC, uid ASC"),
                  {sql_id(parent)}, "list child folders");
    return folders_from(q);
}

std::vector<std::int64_t> FolderTree::subtree_ids(std::int64_t id) const
{
    return descendant_folder_ids(db_, table_, id);
}

bool FolderTree::remove(std::int64_t id)
{
    auto q = exec(db_, in_table(table_, "DELETE FROM %1 WHERE id = ?"), {static_cast<qint64>(id)},
                  "delete folder");
    return q.numRowsAffected() > 0;
}

}  // namespace seventhslide::db
