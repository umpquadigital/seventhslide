// Lists in the operator's order (list_order.hpp): rows a gap apart, so a row
// added or moved changes only its own number, and the order syncs row by row.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/db/web_page_repository.hpp>

#include "test_qt_app.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

using seventhslide::db::Database;
using seventhslide::db::ServiceRepository;

namespace {

Database current_library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

/// The service's items, by their "n" in their data, in order.
std::vector<int> listed(ServiceRepository& services, std::int64_t service)
{
    std::vector<int> out;
    for (const auto& item : services.list_items(service)) {
        out.push_back(item.data.value("n", -1));
    }
    return out;
}

std::int64_t add(ServiceRepository& services, std::int64_t service, int n,
                 std::optional<int> at = std::nullopt)
{
    return services.add_item(service, "divider", nlohmann::json{{"n", n}}, std::nullopt, at);
}

std::size_t items_marked(Database& db)
{
    std::size_t n = 0;
    for (const auto& mark : seventhslide::db::marked_records(db)) {
        n += mark.table == "service_items" ? 1 : 0;
    }
    return n;
}

}  // namespace

TEST_CASE("An item added between two changes no other item", "[db][service]")
{
    Database db = current_library();
    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    for (int n = 0; n < 3; ++n) {
        add(services, service, n);
    }
    db.exec("DELETE FROM sync_outbox");
    add(services, service, 9, 1);
    add(services, service, 8, 0);
    CHECK(listed(services, service) == std::vector<int>{8, 0, 9, 1, 2});
    CHECK(items_marked(db) == 2);
}

TEST_CASE("A service's item count leaves out its dividers", "[db][service]")
{
    Database db = current_library();
    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    const auto other = services.create_service("Vespers");
    CHECK(services.item_count(service) == 0);
    // (``add`` makes dividers: they keep this file's lists plain.)
    const auto freeform = [&services](std::int64_t into) {
        (void)services.add_item(into, seventhslide::model::k_item_type_freeform,
                                nlohmann::json{{"title", "Welcome"}});
    };
    for (int n = 0; n < 3; ++n) {
        freeform(service);
    }
    add(services, service, 0);
    add(services, service, 1);
    freeform(other);
    CHECK(services.item_count(service) == 3);
    CHECK(services.item_count(other) == 1);
    CHECK(services.item_count(9999) == 0);
}

TEST_CASE("Reordering moves only the items that move", "[db][service]")
{
    Database db = current_library();
    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    std::vector<std::int64_t> ids;
    for (int n = 0; n < 5; ++n) {
        ids.push_back(add(services, service, n));
    }
    db.exec("DELETE FROM sync_outbox");
    services.reorder_items(service, {ids[4], ids[0], ids[1], ids[2], ids[3]});
    CHECK(listed(services, service) == std::vector<int>{4, 0, 1, 2, 3});
    CHECK(items_marked(db) == 1);

    db.exec("DELETE FROM sync_outbox");
    services.reorder_items(service, {ids[4], ids[2], ids[0], ids[1], ids[3]});
    CHECK(listed(services, service) == std::vector<int>{4, 2, 0, 1, 3});
    CHECK(items_marked(db) == 1);
}

TEST_CASE("A gap filled up is spaced out again, in order", "[db][service]")
{
    Database db = current_library();
    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    add(services, service, 0);
    add(services, service, 1);
    std::vector<int> expected = {0, 1};
    // Each goes in just after the first: the gap halves every time.
    for (int n = 2; n < 40; ++n) {
        add(services, service, n, 1);
        expected.insert(expected.begin() + 1, n);
    }
    CHECK(listed(services, service) == expected);
}

TEST_CASE("Items with the same number are listed by uid", "[db][service]")
{
    Database db = current_library();
    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    db.exec("INSERT INTO service_items (service_id, order_num, item_type, data, uid) VALUES ("
            + std::to_string(service) + R"(, 5, 'divider', '{"n":1}', 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'))");
    db.exec("INSERT INTO service_items (service_id, order_num, item_type, data, uid) VALUES ("
            + std::to_string(service) + R"(, 5, 'divider', '{"n":0}', 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'))");
    CHECK(listed(services, service) == std::vector<int>{0, 1});
}

namespace {

std::size_t marked_in(Database& db, const std::string& table)
{
    std::size_t n = 0;
    for (const auto& mark : seventhslide::db::marked_records(db)) {
        n += mark.table == table ? 1 : 0;
    }
    return n;
}

}  // namespace

TEST_CASE("Folders, Freeform documents and a folder's files move one row at a time",
          "[db][service]")
{
    Database db = current_library();
    seventhslide::db::MediaRepository media(db);
    auto folders = media.folders(seventhslide::db::MediaKind::Image);
    std::vector<std::int64_t> folder_ids;
    for (const auto* name : {"A", "B", "C", "D"}) {
        folder_ids.push_back(folders.create(name));
    }
    std::vector<std::int64_t> files;
    for (const auto* name : {"a.png", "b.png", "c.png", "d.png"}) {
        files.push_back(media.add_folder_item(seventhslide::db::MediaKind::Image, folder_ids[0], name));
    }
    seventhslide::db::FreeformRepository freeform(db);
    std::vector<std::int64_t> documents;
    for (const auto* name : {"One", "Two", "Three"}) {
        seventhslide::model::FreeformItem item;
        item.name = name;
        documents.push_back(freeform.create_item(item));
    }
    db.exec("DELETE FROM sync_outbox");

    folders.reorder_children(std::nullopt, {folder_ids[3], folder_ids[0], folder_ids[1], folder_ids[2]});
    std::vector<std::string> names;
    for (const auto& f : folders.children(std::nullopt)) {
        names.push_back(f.name);
    }
    CHECK(names == std::vector<std::string>{"D", "A", "B", "C"});
    CHECK(marked_in(db, "image_folders") == 1);

    media.reorder_folder_items(seventhslide::db::MediaKind::Image, folder_ids[0],
                               {files[1], files[0], files[2], files[3]});
    std::vector<std::string> filed;
    for (const auto& item : media.list_folder_items(seventhslide::db::MediaKind::Image, folder_ids[0])) {
        filed.push_back(item.filename);
    }
    CHECK(filed == std::vector<std::string>{"b.png", "a.png", "c.png", "d.png"});
    CHECK(marked_in(db, "image_folder_items") == 1);

    freeform.reorder_items(std::nullopt, {documents[2], documents[0], documents[1]});
    std::vector<std::string> titled;
    for (const auto& item : freeform.list_items_in_folder(std::nullopt)) {
        titled.push_back(item.name);
    }
    CHECK(titled == std::vector<std::string>{"Three", "One", "Two"});
    CHECK(marked_in(db, "freeform_items") == 1);
}

TEST_CASE("A list numbered past what 32 bits hold still adds at its end", "[db][service]")
{
    // Each row added last takes a gap past the largest: about 520,000 adds
    // to one list reach 2^31. Past it, a number cut to 32 bits went negative
    // and the new row was listed first.
    Database db = current_library();
    constexpr std::int64_t high = std::int64_t{1} << 31;  // one past INT32_MAX

    ServiceRepository services(db);
    const auto service = services.create_service("Sabbath");
    add(services, service, 0);
    db.exec("UPDATE service_items SET order_num = " + std::to_string(high));
    add(services, service, 1);
    CHECK(listed(services, service) == std::vector<int>{0, 1});
    CHECK(services.list_items(service).back().order_num > high);

    seventhslide::db::MediaRepository media(db);
    auto folders = media.folders(seventhslide::db::MediaKind::Image);
    const auto first = folders.create("First");
    db.exec("UPDATE image_folders SET sort_order = " + std::to_string(high));
    (void)folders.create("Second");
    const auto made = folders.children(std::nullopt);
    REQUIRE(made.size() == 2);
    CHECK(made.back().name == "Second");
    CHECK(made.back().sort_order > high);

    (void)media.add_folder_item(seventhslide::db::MediaKind::Image, first, "a.png");
    db.exec("UPDATE image_folder_items SET sort_order = " + std::to_string(high));
    (void)media.add_folder_item(seventhslide::db::MediaKind::Image, first, "b.png");
    const auto filed = media.list_folder_items(seventhslide::db::MediaKind::Image, first);
    REQUIRE(filed.size() == 2);
    CHECK(filed.back().filename == "b.png");
    CHECK(filed.back().sort_order > high);

    seventhslide::db::FreeformRepository freeform(db);
    seventhslide::model::FreeformItem one;
    one.name = "One";
    (void)freeform.create_item(one);
    db.exec("UPDATE freeform_items SET sort_order = " + std::to_string(high));
    seventhslide::model::FreeformItem two;
    two.name = "Two";
    (void)freeform.create_item(two);
    CHECK(two.sort_order > high);
    const auto titled = freeform.list_items_in_folder(std::nullopt);
    REQUIRE(titled.size() == 2);
    CHECK(titled.back().name == "Two");
    // Saved again, its number stays as it was.
    freeform.update_item(titled.back());
    CHECK(freeform.list_items_in_folder(std::nullopt).back().sort_order == two.sort_order);

    seventhslide::db::WebPageRepository pages(db);
    seventhslide::model::WebPage home{.name = "Home", .url = "https://example.org/"};
    (void)pages.create_item(home);
    db.exec("UPDATE web_pages SET sort_order = " + std::to_string(high));
    seventhslide::model::WebPage news{.name = "News", .url = "https://example.org/news"};
    (void)pages.create_item(news);
    const auto shown = pages.list_items_in_folder(std::nullopt);
    REQUIRE(shown.size() == 2);
    CHECK(shown.back().name == "News");
    CHECK(shown.back().sort_order > high);
}
