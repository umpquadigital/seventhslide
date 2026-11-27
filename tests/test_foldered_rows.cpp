// Freeform items and web pages are filed alike (db::FolderedRows) and are
// otherwise different things: one test body runs against both repositories,
// and checks that filing a row never touches what it holds.

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/model/library.hpp>
#include <seventhslide/model/web_page.hpp>

#include <nlohmann/json.hpp>

#include "test_qt_app.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using seventhslide::db::Database;

namespace {

/// What each library holds of its own, made and read back the same way.
struct FreeformRows {
    using Repository = seventhslide::db::FreeformRepository;
    using Row = seventhslide::model::FreeformItem;
    static Row make(const std::string& name)
    {
        Row item;
        item.name = name;
        item.tags = {"easter"};
        item.document = {{"version", 2}, {"note", name + "'s document"}};
        return item;
    }
    /// The row's own content: what filing must leave alone.
    static nlohmann::json content(const Row& item)
    {
        return {{"name", item.name}, {"tags", item.tags}, {"document", item.document}};
    }
};

struct WebRows {
    using Repository = seventhslide::db::WebPageRepository;
    using Row = seventhslide::model::WebPage;
    static Row make(const std::string& name)
    {
        Row page;
        page.name = name;
        page.url = "https://example.com/" + name;
        page.width = 1280;
        page.height = 720;
        page.zoom = 1.25;
        return page;
    }
    static nlohmann::json content(const Row& page)
    {
        return {{"name", page.name}, {"url", page.url}, {"width", page.width},
                {"height", page.height}, {"zoom", page.zoom}};
    }
};

template <typename Kind>
std::vector<std::string> names_in(typename Kind::Repository& repo,
                                  std::optional<std::int64_t> folder)
{
    std::vector<std::string> out;
    for (const auto& row : repo.list_items_in_folder(folder)) {
        out.push_back(row.name);
    }
    return out;
}

}  // namespace

TEMPLATE_TEST_CASE("A library's rows are filed in its folders, and filing leaves them as they are",
                   "[db][folders]", FreeformRows, WebRows)
{
    using Kind = TestType;
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    typename Kind::Repository repo(db);

    std::vector<std::int64_t> ids;
    std::vector<nlohmann::json> contents;
    for (const char* name : {"Alpha", "Bravo", "Charlie", "Delta"}) {
        auto row = Kind::make(name);
        ids.push_back(repo.create_item(row));
        contents.push_back(Kind::content(*repo.get_item(ids.back())));
    }
    const auto outer = repo.folders().create("Outer");
    const auto inner = repo.folders().create("Inner", outer);
    const auto other = repo.folders().create("Other");

    // Into a folder, each at its end, all at once.
    CHECK(repo.move_items({ids[2], ids[0]}, outer) == 2);
    CHECK(names_in<Kind>(repo, outer) == std::vector<std::string>{"Charlie", "Alpha"});
    CHECK(names_in<Kind>(repo, std::nullopt) == std::vector<std::string>{"Bravo", "Delta"});
    REQUIRE(repo.move_item(ids[1], inner));
    CHECK_FALSE(repo.move_item(9999, inner));
    REQUIRE(repo.move_item(ids[3], other));
    CHECK(repo.folder_item_counts()
          == std::unordered_map<std::int64_t, int>{{outer, 2}, {inner, 1}, {other, 1}});

    // In the order asked.
    repo.reorder_items(outer, {ids[0], ids[2]});
    CHECK(names_in<Kind>(repo, outer) == std::vector<std::string>{"Alpha", "Charlie"});

    // Filed and refiled, each row holds what it did.
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(Kind::content(*repo.get_item(ids[i])) == contents[i]);
    }

    // A folder goes with the folders and rows under it, and says which rows.
    auto gone = repo.delete_folder_tree(outer);
    std::ranges::sort(gone);
    auto expected = std::vector<std::int64_t>{ids[0], ids[1], ids[2]};
    std::ranges::sort(expected);
    CHECK(gone == expected);
    CHECK_FALSE(repo.folders().get(inner));
    CHECK_FALSE(repo.get_item(ids[1]));
    CHECK(repo.get_item(ids[3]));
    CHECK(repo.folder_item_counts() == std::unordered_map<std::int64_t, int>{{other, 1}});

    CHECK(repo.remove_item(ids[3]));
    CHECK_FALSE(repo.remove_item(ids[3]));
    CHECK(repo.folder_item_counts().empty());
}
