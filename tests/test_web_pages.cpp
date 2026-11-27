// The Browser tab's web pages: what an address the operator types becomes,
// and the pages and folders as the library stores them (schema 8).

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/web_page.hpp>

#include <nlohmann/json.hpp>

#include "test_qt_app.hpp"

#include <algorithm>
#include <string>
#include <vector>

using seventhslide::db::Database;
using seventhslide::db::WebPageRepository;
using seventhslide::model::normalized;
using seventhslide::model::web_url_from_text;
using seventhslide::model::WebPage;

namespace {

Database library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

WebPage page(std::string name, std::string url)
{
    WebPage p;
    p.name = std::move(name);
    p.url = std::move(url);
    return p;
}

std::vector<std::string> names(const std::vector<WebPage>& pages)
{
    std::vector<std::string> out;
    for (const auto& p : pages) {
        out.push_back(p.name);
    }
    return out;
}

}  // namespace

TEST_CASE("A typed address becomes the page to load", "[web]")
{
    // A site without its scheme is a secure one.
    CHECK(web_url_from_text("example.com") == "https://example.com");
    CHECK(web_url_from_text("  example.com/countdown?t=5  ") == "https://example.com/countdown?t=5");
    CHECK(web_url_from_text("http://192.168.1.20:8080/") == "http://192.168.1.20:8080/");
    CHECK(web_url_from_text("HTTPS://Example.com/Page") == "https://Example.com/Page");
    CHECK(web_url_from_text("file:///home/me/wall.html") == "file:///home/me/wall.html");
    // A host and port is a site, not a scheme; a path from / is a file.
    CHECK(web_url_from_text("localhost:8080/clock") == "https://localhost:8080/clock");
    CHECK(web_url_from_text("/home/me/wall.html") == "file:///home/me/wall.html");
    // A path is the file's name, whatever it holds: what an address reads
    // otherwise is escaped.
    CHECK(web_url_from_text("/home/me/My Slides/wall #2?.html")
          == "file:///home/me/My%20Slides/wall%20%232%3F.html");
    CHECK(web_url_from_text("/home/me/100%.html") == "file:///home/me/100%25.html");
    // So is a Windows path, from a drive (either slash) or on a share, whose
    // server is the address's host.
    CHECK(web_url_from_text("C:\\Users\\me\\My Slides\\wall #2.html")
          == "file:///C:/Users/me/My%20Slides/wall%20%232.html");
    CHECK(web_url_from_text("d:/Screens/wall.html") == "file:///d:/Screens/wall.html");
    CHECK(web_url_from_text("C:\\") == "file:///C:/");
    CHECK(web_url_from_text(R"(\\church-nas\Media\Welcome Loop.html)")
          == "file://church-nas/Media/Welcome%20Loop.html");

    // Nothing, words, and schemes a page cannot be shown from.
    CHECK_FALSE(web_url_from_text(""));
    CHECK_FALSE(web_url_from_text("   "));
    CHECK_FALSE(web_url_from_text("the church website"));
    CHECK_FALSE(web_url_from_text("example.com/a page"));
    CHECK_FALSE(web_url_from_text("/home/me/a\tb.html"));
    // A drive alone, or a path from no folder on it, is not a file's.
    CHECK_FALSE(web_url_from_text("C:"));
    CHECK_FALSE(web_url_from_text("C:wall.html"));
    CHECK_FALSE(web_url_from_text(R"(\\)"));
    CHECK_FALSE(web_url_from_text(R"(\\\server)"));
    CHECK_FALSE(web_url_from_text("javascript:alert(1)"));
    CHECK_FALSE(web_url_from_text("mailto:office@example.com"));
    CHECK_FALSE(web_url_from_text("about:blank"));
    CHECK_FALSE(web_url_from_text("ftp://example.com/file"));
    CHECK_FALSE(web_url_from_text("data://x"));
    // A web address names its site; a file's names a path.
    CHECK_FALSE(web_url_from_text("https://"));
    CHECK_FALSE(web_url_from_text("https:///home/me/wall.html"));
    CHECK_FALSE(web_url_from_text("http://user@:8080/"));
    CHECK_FALSE(web_url_from_text("file://"));
}

TEST_CASE("A stored page's size and zoom stay in range, and it always has a name", "[web]")
{
    WebPage p = page("  Countdown  ", "https://example.com");
    p.width = 20;
    p.height = 100000;
    p.zoom = 40;
    const auto n = normalized(p);
    CHECK(n.name == "Countdown");
    CHECK(n.width == seventhslide::model::k_web_min_side);
    CHECK(n.height == seventhslide::model::k_web_max_side);
    CHECK(n.zoom == seventhslide::model::k_web_max_zoom);

    // No name: the site's.
    CHECK(normalized(page("", "https://user@www.example.com:8443/a?b")).name == "www.example.com");
    CHECK(normalized(page(" ", "file:///wall.html")).name == "file:///wall.html");
}

TEST_CASE("A web page's item carries a snapshot of the page", "[web]")
{
    using seventhslide::model::ContentKind;
    CHECK(seventhslide::model::to_string(ContentKind::Web) == "web");
    CHECK(seventhslide::model::content_kind_from_string("web") == ContentKind::Web);

    WebPage p = page("Countdown", "https://example.com/countdown");
    p.id = 7;
    p.width = 1280;
    p.height = 720;
    p.zoom = 1.5;
    const auto data = seventhslide::model::web_item_data(p);
    CHECK(seventhslide::model::web_page_from_item_data(data) == p);

    seventhslide::model::ServiceItem item;
    item.item_type = "web";
    item.data = data;
    CHECK(item.kind() == ContentKind::Web);
    CHECK(item.title() == "Countdown");

    // What a stored item (or another computer's) holds is read in range, and
    // an address no page can show is none.
    const auto odd = seventhslide::model::web_page_from_item_data(
        {{"url", "javascript:alert(1)"}, {"width", 0}, {"zoom", 99}});
    CHECK(odd.url.empty());
    CHECK(odd.width == seventhslide::model::k_web_min_side);
    CHECK(odd.zoom == seventhslide::model::k_web_max_zoom);
    CHECK(seventhslide::model::web_page_from_item_data(nlohmann::json::array()).url.empty());
}

TEST_CASE("Web pages are stored, edited, filed and removed", "[web][db]")
{
    Database db = library();
    WebPageRepository repo(db);

    WebPage a = page("Announcements", "https://example.com/a");
    a.width = 1280;
    a.height = 720;
    a.zoom = 1.5;
    const auto a_id = repo.create_item(a);
    WebPage b = page("Countdown", "https://example.com/b");
    (void)repo.create_item(b);
    CHECK(names(repo.list_items_in_folder(std::nullopt))
          == std::vector<std::string>{"Announcements", "Countdown"});

    auto stored = repo.get_item(a_id);
    REQUIRE(stored);
    CHECK(stored->url == "https://example.com/a");
    CHECK(stored->width == 1280);
    CHECK(stored->height == 720);
    CHECK(stored->zoom == 1.5);

    stored->name = "Welcome";
    stored->url = "https://example.com/welcome";
    REQUIRE(repo.update_item(*stored));
    CHECK(repo.get_item(a_id)->name == "Welcome");
    CHECK(repo.get_item(a_id)->url == "https://example.com/welcome");

    // Into a folder, and its count; reordered within it.
    auto folders = repo.folders();
    const auto folder = folders.create("Screens");
    REQUIRE(repo.move_item(a_id, folder));
    REQUIRE(repo.move_item(*b.id, folder));
    CHECK(repo.list_items_in_folder(std::nullopt).empty());
    CHECK(repo.folder_item_counts().at(folder) == 2);
    repo.reorder_items(folder, {*b.id, a_id});
    CHECK(names(repo.list_items_in_folder(folder)) == std::vector<std::string>{"Countdown", "Welcome"});

    // A folder deleted takes its pages with it.
    const auto removed = repo.delete_folder_tree(folder);
    CHECK(removed.size() == 2);
    CHECK_FALSE(repo.get_item(a_id));

    WebPage c = page("Wall", "https://example.com/wall");
    const auto c_id = repo.create_item(c);
    REQUIRE(repo.remove_item(c_id));
    CHECK_FALSE(repo.remove_item(c_id));
    CHECK_FALSE(repo.update_item(c));
}

TEST_CASE("A web page reads back in range however it was written", "[web][db][sync]")
{
    Database db = library();
    WebPageRepository repo(db);
    // As another computer's copy might arrive by sync.
    db.exec("INSERT INTO web_pages (name, url, width, height, zoom) "
            "VALUES ('  ', 'https://example.com/a', 0, 99999, 0)");
    const auto pages = repo.list_items_in_folder(std::nullopt);
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].name == "example.com");
    CHECK(pages[0].width == seventhslide::model::k_web_min_side);
    CHECK(pages[0].height == seventhslide::model::k_web_max_side);
    CHECK(pages[0].zoom == seventhslide::model::k_web_min_zoom);
    CHECK(repo.get_item(*pages[0].id) == pages[0]);
}

TEST_CASE("A web page marks the sync outbox under its uid", "[web][db][sync]")
{
    Database db = library();
    WebPageRepository repo(db);
    WebPage p = page("Wall", "https://example.com/wall");
    (void)repo.create_item(p);
    (void)repo.folders().create("Screens");
    const auto marked = seventhslide::db::marked_records(db);
    CHECK(std::ranges::count(marked, std::string("web_pages"),
                             &seventhslide::db::MarkedRecord::table) == 1);
    CHECK(std::ranges::count(marked, std::string("web_folders"),
                             &seventhslide::db::MarkedRecord::table) == 1);
}
