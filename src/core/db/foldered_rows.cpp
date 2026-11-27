#include "seventhslide/db/foldered_rows.hpp"

#include "seventhslide/db/folder_tree.hpp"
#include "seventhslide/db/list_order.hpp"
#include "seventhslide/db/query.hpp"

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {

FolderedRows::FolderedRows(Database& db, std::string_view table, std::string_view folder_table,
                           std::string_view noun)
    : db_(db)
    , table_(table)
    , folder_table_(folder_table)
    , noun_(noun)
{
}

std::unordered_map<std::int64_t, int> FolderedRows::counts() const
{
    auto q = exec(db_,
                  QStringLiteral("SELECT folder_id, COUNT(*) FROM %1 "
                                 "WHERE folder_id IS NOT NULL GROUP BY folder_id")
                      .arg(sql_text(table_)),
                  {}, noun_ + " folder counts");
    std::unordered_map<std::int64_t, int> out;
    while (q.next()) {
        out[q.value(0).toLongLong()] = q.value(1).toInt();
    }
    return out;
}

bool FolderedRows::move(std::int64_t id, std::optional<std::int64_t> folder)
{
    const auto sort_order = next_order(db_, {table_, "sort_order", "folder_id", folder});
    auto q = exec(db_,
                  QStringLiteral("UPDATE %1 SET folder_id = ?, sort_order = ? WHERE id = ?")
                      .arg(sql_text(table_)),
                  {sql_id(folder), static_cast<qint64>(sort_order), static_cast<qint64>(id)},
                  "move " + noun_);
    return q.numRowsAffected() > 0;
}

int FolderedRows::move_all(const std::vector<std::int64_t>& ids, std::optional<std::int64_t> folder)
{
    Transaction tx(db_);
    int moved = 0;
    for (const auto id : ids) {
        moved += move(id, folder) ? 1 : 0;
    }
    tx.commit();
    return moved;
}

void FolderedRows::reorder(std::optional<std::int64_t> folder, const std::vector<std::int64_t>& ids)
{
    reorder_rows(db_, {table_, "sort_order", "folder_id", folder}, ids);
}

bool FolderedRows::remove(std::int64_t id)
{
    auto q = exec(db_, QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(sql_text(table_)),
                  {static_cast<qint64>(id)}, "remove " + noun_);
    return q.numRowsAffected() > 0;
}

std::vector<std::int64_t> FolderedRows::delete_folder_tree(std::int64_t id)
{
    FolderTree tree(db_, folder_table_);
    Transaction tx(db_);
    const QString folders = id_in_list(tree.subtree_ids(id));
    std::vector<std::int64_t> ids;
    {
        auto q = exec(db_,
                      QStringLiteral("SELECT id FROM %1 WHERE folder_id IN %2")
                          .arg(sql_text(table_), folders),
                      {}, "list the " + noun_ + "s of a folder");
        while (q.next()) {
            ids.push_back(q.value(0).toLongLong());
        }
    }
    // Rows before folders: ``folder_id`` is ON DELETE SET NULL, so dropping
    // the folders first would strand their rows at the root.
    exec(db_, QStringLiteral("DELETE FROM %1 WHERE folder_id IN %2").arg(sql_text(table_), folders),
         {}, "delete the " + noun_ + "s of a folder");
    (void)tree.remove(id);
    tx.commit();
    return ids;
}

}  // namespace seventhslide::db
