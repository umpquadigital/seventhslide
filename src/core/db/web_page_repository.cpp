#include "seventhslide/db/web_page_repository.hpp"

#include "seventhslide/db/list_order.hpp"
#include "seventhslide/db/query.hpp"

#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace seventhslide::db {
namespace {

const QString k_columns =
    QStringLiteral("id, name, url, width, height, zoom, folder_id, sort_order");

model::WebPage page_from_row(QSqlQuery& q)
{
    model::WebPage page;
    page.id = q.value(0).toLongLong();
    page.name = q.value(1).toString().toStdString();
    page.url = q.value(2).toString().toStdString();
    page.width = q.value(3).toInt();
    page.height = q.value(4).toInt();
    page.zoom = q.value(5).toDouble();
    if (!q.value(6).isNull()) {
        page.folder_id = q.value(6).toLongLong();
    }
    page.sort_order = q.value(7).toLongLong();
    // In range however it was written: another computer's copy, or another
    // version's, comes in by sync.
    return model::normalized(std::move(page));
}

}  // namespace

FolderTree WebPageRepository::folders() const
{
    return FolderTree(db_, "web_folders");
}

FolderedRows WebPageRepository::rows() const
{
    return FolderedRows(db_, "web_pages", "web_folders", "web page");
}

std::vector<std::int64_t> WebPageRepository::delete_folder_tree(std::int64_t id)
{
    return rows().delete_folder_tree(id);
}

std::int64_t WebPageRepository::create_item(model::WebPage& page)
{
    page = model::normalized(std::move(page));
    page.sort_order = next_order(db_, {"web_pages", "sort_order", "folder_id", page.folder_id});
    exec(db_,
         QStringLiteral("INSERT INTO web_pages (name, url, width, height, zoom, folder_id, "
                        "sort_order) VALUES (?, ?, ?, ?, ?, ?, ?)"),
         {QString::fromStdString(page.name), QString::fromStdString(page.url), page.width,
          page.height, page.zoom, sql_id(page.folder_id), static_cast<qint64>(page.sort_order)},
         "create web page");
    page.id = db_.last_insert_id();
    return *page.id;
}

bool WebPageRepository::update_item(const model::WebPage& page)
{
    if (!page.id) {
        return false;
    }
    const auto p = model::normalized(page);
    auto q = exec(db_,
                  QStringLiteral("UPDATE web_pages SET name = ?, url = ?, width = ?, height = ?, "
                                 "zoom = ? WHERE id = ?"),
                  {QString::fromStdString(p.name), QString::fromStdString(p.url), p.width,
                   p.height, p.zoom, static_cast<qint64>(*page.id)},
                  "update web page");
    return q.numRowsAffected() > 0;
}

std::optional<model::WebPage> WebPageRepository::get_item(std::int64_t id)
{
    auto q = exec(db_, QStringLiteral("SELECT %1 FROM web_pages WHERE id = ?").arg(k_columns),
                  {static_cast<qint64>(id)}, "get web page");
    if (!q.next()) {
        return std::nullopt;
    }
    return page_from_row(q);
}

std::vector<model::WebPage> WebPageRepository::list_items_in_folder(
    std::optional<std::int64_t> folder_id)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT %1 FROM web_pages WHERE folder_id IS ? "
                                 "ORDER BY sort_order ASC, uid ASC")
                      .arg(k_columns),
                  {sql_id(folder_id)}, "list web pages");
    std::vector<model::WebPage> out;
    while (q.next()) {
        out.push_back(page_from_row(q));
    }
    return out;
}

std::vector<model::WebPage> WebPageRepository::all_items()
{
    auto q = exec(db_, QStringLiteral("SELECT %1 FROM web_pages").arg(k_columns), {},
                  "list all web pages");
    std::vector<model::WebPage> out;
    while (q.next()) {
        out.push_back(page_from_row(q));
    }
    return out;
}

std::unordered_map<std::int64_t, int> WebPageRepository::folder_item_counts()
{
    return rows().counts();
}

bool WebPageRepository::move_item(std::int64_t id, std::optional<std::int64_t> folder_id)
{
    return rows().move(id, folder_id);
}

int WebPageRepository::move_items(const std::vector<std::int64_t>& ids,
                                  std::optional<std::int64_t> folder_id)
{
    return rows().move_all(ids, folder_id);
}

void WebPageRepository::reorder_items(std::optional<std::int64_t> folder_id,
                                      const std::vector<std::int64_t>& ordered_ids)
{
    rows().reorder(folder_id, ordered_ids);
}

bool WebPageRepository::remove_item(std::int64_t id)
{
    return rows().remove(id);
}

}  // namespace seventhslide::db
