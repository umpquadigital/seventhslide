#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/freeform/media_refs.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/freeform/doc_edit.hpp>

#include <set>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <string>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using seventhslide::app::SessionFacade;
using seventhslide::db::FreeformRepository;
using seventhslide::db::Database;
using seventhslide::db::MediaKind;
using seventhslide::db::MediaRepository;
using seventhslide::db::ServiceRepository;

namespace {

Database open_db()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

/// Native Freeform document with one image object and, optionally, an image
/// slide background.
nlohmann::json doc_with_image(const std::string& object_file, const std::string& bg_file = {})
{
    auto doc = seventhslide::freeform::make_document("Promo", {"Main"});
    auto& slide = doc.decks.front().slides.front();
    if (!object_file.empty()) {
        seventhslide::freeform::Object img;
        img.id = seventhslide::freeform::alloc_id(doc, 'o');
        img.type = seventhslide::freeform::ObjectType::Image;
        img.filename = object_file;
        slide.objects.push_back(img);
    }
    if (!bg_file.empty()) {
        slide.background.kind = seventhslide::freeform::SlideBackgroundKind::Image;
        slide.background.image = bg_file;
    }
    return doc.to_json();
}

/// A media folder in ``tmp``, with its images and videos subfolders.
const std::filesystem::path& fresh_media_root(const seventhslide::test::TempDir& tmp)
{
    std::filesystem::create_directories(tmp.path / "images");
    std::filesystem::create_directories(tmp.path / "videos");
    return tmp.path;
}

void touch(const std::filesystem::path& p)
{
    std::ofstream(p, std::ios::binary) << "x";
}

[[nodiscard]] bool referenced(MediaRepository& media, const std::string& filename)
{
    return media.referenced_images({filename}).contains(filename);
}

}  // namespace

TEST_CASE("referenced_images sees every place an image is shown", "[db][media][refs]")
{
    Database db = open_db();
    MediaRepository media(db);
    ServiceRepository services(db);
    FreeformRepository freeforms(db);
    const auto sid = services.create_service("S");

    REQUIRE_FALSE(referenced(media, "a.png"));

    SECTION("image service item")
    {
        (void)services.add_item(sid, "image", {{"filename", "a.png"}});
        REQUIRE(referenced(media, "a.png"));
    }
    SECTION("image folder service item lists its images under 'images'")
    {
        (void)services.add_item(sid, "image_folder",
                                {{"folder_id", 1},
                                 {"folder_name", "F"},
                                 {"images", nlohmann::json::array({"b.png", "a.png"})}});
        REQUIRE(referenced(media, "a.png"));
        REQUIRE(referenced(media, "b.png"));
    }
    SECTION("freeform service item document: object and background")
    {
        (void)services.add_item(sid, "freeform", {{"document", doc_with_image("a.png")}});
        (void)services.add_item(sid, "freeform",
                                {{"document", doc_with_image({}, "/static/images/c.png")}});
        REQUIRE(referenced(media, "a.png"));
        REQUIRE(referenced(media, "c.png"));
    }
    SECTION("freeform library item")
    {
        seventhslide::model::FreeformItem item;
        item.name = "Promo";
        item.document = doc_with_image("a.png");
        (void)freeforms.create_item(item);
        REQUIRE(referenced(media, "a.png"));
    }
    SECTION("a picture fill on any object")
    {
        auto doc = doc_with_image("a.png");
        doc["decks"][0]["slides"][0]["objects"][0]["fill_kind"] = "picture";
        doc["decks"][0]["slides"][0]["objects"][0]["fill_picture"] = {{"file", "f.png"}, {"fit", "cover"}};
        seventhslide::model::FreeformItem item;
        item.name = "Promo";
        item.document = doc;
        (void)freeforms.create_item(item);
        REQUIRE(referenced(media, "f.png"));
    }
    SECTION("other filenames and malformed rows do not count")
    {
        (void)services.add_item(sid, "image", {{"filename", "other.png"}});
        (void)services.add_item(sid, "image_folder", {{"images", "not-an-array"}});
        (void)services.add_item(sid, "freeform", {{"document", "not-an-object"}});
        REQUIRE_FALSE(referenced(media, "a.png"));
    }
}

TEST_CASE("retarget_file repoints every place referenced_images looks", "[db][media][refs]")
{
    Database db = open_db();
    MediaRepository media(db);
    ServiceRepository services(db);
    FreeformRepository freeforms(db);
    const auto sid = services.create_service("S");
    const auto image = services.add_item(sid, "image", {{"filename", "a.png"}});
    const auto folder_item = services.add_item(
        sid, "image_folder",
        {{"folder_id", 1}, {"images", nlohmann::json::array({"b.png", "images/a.png"})}});
    const auto in_service = services.add_item(
        sid, "freeform", {{"document", doc_with_image("a.png", "/static/images/a.png")}});
    const auto other = services.add_item(sid, "image", {{"filename", "keep.png"}});
    seventhslide::model::FreeformItem promo;
    promo.name = "Promo";
    promo.document = doc_with_image("a.png");
    const auto promo_id = freeforms.create_item(promo);
    const auto f1 = media.folders(MediaKind::Image).create("One");
    const auto f2 = media.folders(MediaKind::Image).create("Two");
    (void)media.add_folder_item(MediaKind::Image, f1, "b.png");
    (void)media.add_folder_item(MediaKind::Image, f1, "a.png");
    (void)media.add_folder_item(MediaKind::Image, f2, "a.png");

    // Two folder links, three service items, one library Freeform.
    CHECK(media.retarget_file(MediaKind::Image, "a.png", "n.webp") == 6);

    CHECK(services.get_item(image)->data["filename"] == "n.webp");
    CHECK(services.get_item(folder_item)->data["images"]
          == nlohmann::json::array({"b.png", "images/n.webp"}));
    // The path before the leaf stays.
    const auto shown = services.get_item(in_service)->data["document"];
    CHECK(shown["decks"][0]["slides"][0]["objects"][0]["filename"] == "n.webp");
    CHECK(shown["decks"][0]["slides"][0]["background"]["image"] == "/static/images/n.webp");
    CHECK(services.get_item(other)->data["filename"] == "keep.png");
    CHECK(freeforms.get_item(promo_id)->document["decks"][0]["slides"][0]["objects"][0]["filename"]
          == "n.webp");
    // Each folder link keeps its place.
    const auto one = media.list_folder_items(MediaKind::Image, f1);
    REQUIRE(one.size() == 2);
    CHECK(one[0].filename == "b.png");
    CHECK(one[1].filename == "n.webp");
    CHECK(media.list_folder_items(MediaKind::Image, f2)[0].filename == "n.webp");
    REQUIRE_FALSE(referenced(media, "a.png"));
    REQUIRE(referenced(media, "n.webp"));

    // Nothing else uses it now.
    CHECK(media.retarget_file(MediaKind::Image, "a.png", "z.png") == 0);
}

TEST_CASE("retarget_file repoints the service items that play a video or audio file",
          "[db][media][refs]")
{
    Database db = open_db();
    MediaRepository media(db);
    ServiceRepository services(db);
    const auto sid = services.create_service("S");
    const auto video = services.add_item(sid, "video", {{"filename", "a.mp4"}, {"title", "A"}});
    const auto audio = services.add_item(sid, "audio", {{"filename", "a.mp4"}});
    const auto f = media.folders(MediaKind::Video).create("Clips");
    (void)media.add_folder_item(MediaKind::Video, f, "a.mp4");

    CHECK(media.retarget_file(MediaKind::Video, "a.mp4", "n.webm") == 2);
    CHECK(services.get_item(video)->data["filename"] == "n.webm");
    // The title it was made with stays; so does another kind's item.
    CHECK(services.get_item(video)->data["title"] == "A");
    CHECK(services.get_item(audio)->data["filename"] == "a.mp4");
    CHECK(media.count_playback_refs(MediaKind::Video, "n.webm") == 1);
    CHECK(media.list_folder_items(MediaKind::Video, f)[0].filename == "n.webm");
}

TEST_CASE("list_folder_tree_filenames is read-only and covers nested folders", "[db][media][refs]")
{
    Database db = open_db();
    MediaRepository media(db);
    const auto root = media.folders(MediaKind::Video).create("Root");
    const auto child = media.folders(MediaKind::Video).create("Child", root);
    (void)media.add_folder_item(MediaKind::Video, root, "a.mp4");
    (void)media.add_folder_item(MediaKind::Video, child, "b.mp4");

    const auto names = media.list_folder_tree_filenames(MediaKind::Video, root);
    REQUIRE(names.size() == 2);
    REQUIRE(media.folders(MediaKind::Video).list().size() == 2);
}

TEST_CASE("the schema keeps folder trees and their items consistent", "[db][media][schema]")
{
    Database db = open_db();
    MediaRepository media(db);

    SECTION("a file is in a folder at most once")
    {
        const auto folder = media.folders(MediaKind::Image).create("Slides");
        REQUIRE(media.add_folder_item(MediaKind::Image, folder, "a.png") > 0);
        // UNIQUE(folder_id, filename): the second add is rejected, not silently
        // duplicated into a folder that then lists the same file twice.
        REQUIRE_THROWS(media.add_folder_item(MediaKind::Image, folder, "a.png"));
    }

    SECTION("the same file may sit in two different folders")
    {
        const auto one = media.folders(MediaKind::Image).create("One");
        const auto two = media.folders(MediaKind::Image).create("Two");
        REQUIRE(media.add_folder_item(MediaKind::Image, one, "a.png") > 0);
        REQUIRE(media.add_folder_item(MediaKind::Image, two, "a.png") > 0);
    }

    SECTION("deleting a folder takes its subtree and every item with it")
    {
        const auto root = media.folders(MediaKind::Video).create("Root");
        const auto child = media.folders(MediaKind::Video).create("Child", root);
        const auto grandchild = media.folders(MediaKind::Video).create("Grand", child);
        (void)media.add_folder_item(MediaKind::Video, root, "a.mp4");
        (void)media.add_folder_item(MediaKind::Video, child, "b.mp4");
        (void)media.add_folder_item(MediaKind::Video, grandchild, "c.mp4");
        REQUIRE(media.folders(MediaKind::Video).list().size() == 3);

        const auto removed = media.delete_folder_tree(MediaKind::Video, root);
        REQUIRE(removed.size() == 3);
        // No stranded folders and no stranded items: parent_id and folder_id
        // both cascade, so nothing is left pointing at a row that is gone.
        REQUIRE(media.folders(MediaKind::Video).list().empty());
        REQUIRE(media.list_linked_filenames(MediaKind::Video).empty());
    }
}

TEST_CASE("every media kind has its own folder tree", "[db][media][schema]")
{
    Database db = open_db();
    MediaRepository media(db);

    for (const auto kind : seventhslide::db::k_media_kinds) {
        INFO("kind " << static_cast<int>(kind));
        const auto root = media.folders(kind).create("Root");
        const auto child = media.folders(kind).create("Child", root);
        REQUIRE(media.add_folder_item(kind, root, "a.file") > 0);
        REQUIRE(media.add_folder_item(kind, child, "b.file") > 0);
        REQUIRE_THROWS(media.add_folder_item(kind, root, "a.file"));
        REQUIRE(media.folder_item_counts(kind).at(root) == 1);

        // The other kinds see none of it.
        for (const auto other : seventhslide::db::k_media_kinds) {
            if (other != kind) {
                REQUIRE(media.folders(other).list().empty());
                REQUIRE(media.list_linked_filenames(other).empty());
            }
        }

        REQUIRE(media.delete_folder_tree(kind, root).size() == 2);
        REQUIRE(media.folders(kind).list().empty());
        REQUIRE(media.list_linked_filenames(kind).empty());
    }
}

TEST_CASE("Playback references answer per kind for a whole folder in one pass",
          "[db][media][refs][audio]")
{
    Database db = open_db();
    MediaRepository media(db);
    ServiceRepository services(db);

    const auto service = services.create_service("Sunday");
    auto add = [&](const char* type, const std::string& filename) {
        (void)services.add_item(service, type, nlohmann::json{{"filename", filename}});
    };
    add("video", "used.mp4");
    add("video", "used.mp4");  // the same clip twice in one rundown
    add("video", "other.mp4");
    add("audio", "hymn.mp3");
    // An audio item named like a video is not a reference to that video.
    add("audio", "spare.mp4");

    const std::vector<std::string> asked{"used.mp4", "spare.mp4", "missing.mp4"};
    const auto referenced = media.referenced_playback_files(MediaKind::Video, asked);
    REQUIRE(referenced == std::unordered_set<std::string>{"used.mp4"});

    // Same answer as asking one filename at a time, which is what the folder
    // delete guard used to do.
    for (const auto& name : asked) {
        REQUIRE((media.count_playback_refs(MediaKind::Video, name) > 0)
                == (referenced.count(name) == 1));
    }
    REQUIRE(media.count_playback_refs(MediaKind::Video, "used.mp4") == 2);
    REQUIRE(media.referenced_playback_files(MediaKind::Video, {}).empty());

    REQUIRE(media.referenced_playback_files(MediaKind::Audio, {"hymn.mp3", "used.mp4", "spare.mp4"})
            == std::unordered_set<std::string>{"hymn.mp3", "spare.mp4"});
    REQUIRE(media.count_playback_refs(MediaKind::Audio, "hymn.mp3") == 1);

    // Images are not played by filename alone.
    REQUIRE_THROWS(media.count_playback_refs(MediaKind::Image, "a.png"));
}

TEST_CASE("Deleting images keeps files that a service or Freeform still shows", "[app][media][refs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    const auto& root = fresh_media_root(tmp);
    facade.set_media_root(root);
    auto* media = facade.media_repo();

    for (const char* fn : {"in-folder-item.png", "in-freeform.png", "orphan.png", "solo.png"}) {
        touch(root / "images" / fn);
        media->upsert_file(seventhslide::db::MediaKind::Image, fn, std::string("Nice ") + fn);
    }
    const auto folder = media->folders(MediaKind::Image).create("Pack");
    (void)media->add_folder_item(MediaKind::Image, folder, "in-folder-item.png");
    (void)media->add_folder_item(MediaKind::Image, folder, "orphan.png");

    const auto sid = facade.library()->createService(QStringLiteral("S"));
    REQUIRE(sid > 0);
    (void)facade.service_repo()->add_item(
        sid, "image_folder",
        {{"folder_id", folder}, {"folder_name", "Pack"}, {"images", nlohmann::json::array({"in-folder-item.png"})}});
    seventhslide::model::FreeformItem promo;
    promo.name = "Promo";
    promo.document = doc_with_image("in-freeform.png");
    (void)facade.freeform_repo()->create_item(promo);

    SECTION("folder delete")
    {
        REQUIRE(facade.media()->library(MediaKindQml::Image)->deleteFolder(folder).isEmpty());
        REQUIRE(media->folders(MediaKind::Image).list().empty());
        // Used by the image-folder rundown item: kept on disk, hidden, name kept.
        REQUIRE(std::filesystem::exists(root / "images" / "in-folder-item.png"));
        const auto kept = media->get_file(seventhslide::db::MediaKind::Image, "in-folder-item.png");
        REQUIRE(kept);
        REQUIRE_FALSE(kept->library_visible);
        REQUIRE(kept->display_name == "Nice in-folder-item.png");
        // Nothing uses it: gone from disk and registry.
        REQUIRE_FALSE(std::filesystem::exists(root / "images" / "orphan.png"));
        REQUIRE_FALSE(media->get_file(seventhslide::db::MediaKind::Image, "orphan.png"));
    }
    SECTION("single image used only by a Freeform library item")
    {
        REQUIRE(facade.media()
                    ->library(MediaKindQml::Image)->deleteItem(QStringLiteral("in-freeform.png"))
                    .isEmpty());
        REQUIRE(std::filesystem::exists(root / "images" / "in-freeform.png"));
        REQUIRE_FALSE(media->get_file(seventhslide::db::MediaKind::Image, "in-freeform.png")->library_visible);
    }
    SECTION("single unused image is removed")
    {
        REQUIRE(facade.media()
                    ->library(MediaKindQml::Image)->deleteItem(QStringLiteral("solo.png"))
                    .isEmpty());
        REQUIRE_FALSE(std::filesystem::exists(root / "images" / "solo.png"));
    }
}

TEST_CASE("Deleting a video folder refuses while a service uses one of its videos", "[app][media][refs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    const auto& root = fresh_media_root(tmp);
    facade.set_media_root(root);
    auto* media = facade.media_repo();

    touch(root / "videos" / "used.mp4");
    touch(root / "videos" / "spare.mp4");
    const auto folder = media->folders(MediaKind::Video).create("Clips");
    const auto nested = media->folders(MediaKind::Video).create("More", folder);
    (void)media->add_folder_item(MediaKind::Video, folder, "spare.mp4");
    (void)media->add_folder_item(MediaKind::Video, nested, "used.mp4");

    const auto sid = facade.library()->createService(QStringLiteral("S"));
    REQUIRE(sid > 0);
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("used.mp4")));

    const auto refused = facade.media()->library(MediaKindQml::Video)->deleteFolder(folder);
    REQUIRE_FALSE(refused.isEmpty());
    // Refusal changes nothing.
    REQUIRE(media->folders(MediaKind::Video).list().size() == 2);
    REQUIRE(std::filesystem::exists(root / "videos" / "used.mp4"));
    REQUIRE(std::filesystem::exists(root / "videos" / "spare.mp4"));

    // Once the rundown no longer uses it, the folder and its files go.
    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();
    REQUIRE(facade.library()->remove_service_item(item_id));
    REQUIRE(facade.media()->library(MediaKindQml::Video)->deleteFolder(folder).isEmpty());
    REQUIRE(media->folders(MediaKind::Video).list().empty());
    REQUIRE_FALSE(std::filesystem::exists(root / "videos" / "used.mp4"));
    REQUIRE_FALSE(std::filesystem::exists(root / "videos" / "spare.mp4"));
}

TEST_CASE("A Freeform's video objects use their videos and their posters", "[db][media][refs][video]")
{
    Database db = open_db();
    MediaRepository media(db);
    ServiceRepository services(db);
    FreeformRepository freeforms(db);
    auto doc = seventhslide::freeform::make_document("Clip", {"Main"});
    seventhslide::freeform::Object clip;
    clip.id = seventhslide::freeform::alloc_id(doc, 'o');
    clip.type = seventhslide::freeform::ObjectType::Video;
    clip.filename = "a.mp4";
    clip.video.poster = "still.png";
    doc.decks.front().slides.front().objects.push_back(clip);
    seventhslide::model::FreeformItem item;
    item.name = "Clip";
    item.document = doc.to_json();
    const auto id = freeforms.create_item(item);
    const auto sid = services.create_service("S");
    const auto in_service = services.add_item(sid, "freeform", {{"document", doc.to_json()}});

    // In use: the library's item and the service's copy of it.
    CHECK(media.count_playback_refs(MediaKind::Video, "a.mp4") == 2);
    CHECK(media.referenced_playback_files(MediaKind::Video, {"a.mp4", "b.mp4"})
          == std::unordered_set<std::string>{"a.mp4"});
    CHECK(referenced(media, "still.png"));

    // Renamed, each follows.
    CHECK(media.retarget_file(MediaKind::Video, "a.mp4", "b.mp4") == 2);
    CHECK(freeforms.get_item(id)->document["decks"][0]["slides"][0]["objects"][0]["filename"] == "b.mp4");
    CHECK(services.get_item(in_service)->data["document"]["decks"][0]["slides"][0]["objects"][0]["filename"]
          == "b.mp4");
    CHECK(media.count_playback_refs(MediaKind::Video, "a.mp4") == 0);
}

TEST_CASE("A slide's video background is a reference to its file", "[freeform][media][refs]")
{
    using namespace seventhslide;
    freeform::SlideBackground bg;
    bg.kind = freeform::SlideBackgroundKind::Video;
    bg.video = "loop.mp4";
    std::vector<std::pair<std::string, canvas::MediaRefKind>> seen;
    freeform::visit_background_media(bg, [&](const std::string& file, canvas::MediaRefKind kind) {
        seen.emplace_back(file, kind);
    });
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].first == "loop.mp4");
    CHECK(seen[0].second == canvas::MediaRefKind::Video);

    nlohmann::json doc = nlohmann::json::parse(
        R"({"decks":[{"slides":[{"background":{"kind":"video","video":"loop.mp4"}}]}]})");
    int files = 0;
    freeform::visit_document_media(doc, [&](nlohmann::json& file, canvas::MediaRefKind kind) {
        CHECK(file == "loop.mp4");
        CHECK(kind == canvas::MediaRefKind::Video);
        ++files;
    });
    CHECK(files == 1);
}

