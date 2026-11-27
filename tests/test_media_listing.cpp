// The one set of browse rules the operator console and the phone remote share.
// The root rule in particular - only unfiled files show there - is easy to get
// subtly wrong.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/media_listing.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>

#include <algorithm>
#include <string>
#include <vector>

using seventhslide::db::Database;
using seventhslide::db::is_media_filename;
using seventhslide::db::list_media;
using seventhslide::db::MediaKind;
using seventhslide::db::MediaRepository;

namespace {

std::vector<std::string> filenames(const seventhslide::db::MediaListing& listing)
{
    std::vector<std::string> out;
    out.reserve(listing.files.size());
    for (const auto& f : listing.files) {
        out.push_back(f.filename);
    }
    return out;
}

std::vector<std::string> folder_names(const seventhslide::db::MediaListing& listing)
{
    std::vector<std::string> out;
    out.reserve(listing.folders.size());
    for (const auto& f : listing.folders) {
        out.push_back(f.name);
    }
    return out;
}

/// In-memory library with the schema applied.
Database open_db()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

} // namespace

TEST_CASE("extension predicates accept the pipeline's formats, any case", "[media][listing]")
{
    REQUIRE(is_media_filename(MediaKind::Image, "photo.JPG"));
    REQUIRE(is_media_filename(MediaKind::Image, "a.b.png"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Image, "clip.mp4"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Image, "noextension"));

    REQUIRE(is_media_filename(MediaKind::Video, "clip.MOV"));
    REQUIRE(is_media_filename(MediaKind::Video, "clip.mkv"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Video, "photo.png"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Video, "noextension"));

    REQUIRE(is_media_filename(MediaKind::Audio, "hymn.MP3"));
    REQUIRE(is_media_filename(MediaKind::Audio, "pad.flac"));
    REQUIRE(is_media_filename(MediaKind::Audio, "take.m4a"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Audio, "clip.mp4"));
    REQUIRE_FALSE(is_media_filename(MediaKind::Audio, "photo.png"));

    // An Ogg file can hold either, so both kinds take it.
    REQUIRE(is_media_filename(MediaKind::Video, "x.ogg"));
    REQUIRE(is_media_filename(MediaKind::Audio, "x.ogg"));

    REQUIRE(seventhslide::db::is_any_media_filename("a.png"));
    REQUIRE(seventhslide::db::is_any_media_filename("a.wav"));
    REQUIRE_FALSE(seventhslide::db::is_any_media_filename("notes.txt"));
}

TEST_CASE("images: the root lists only unfiled files", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    const auto folder = repo.folders(MediaKind::Image).create("Backgrounds");
    for (const auto* fn : {"filed.png", "loose.png", "hidden.png"}) {
        repo.upsert_file(seventhslide::db::MediaKind::Image, fn, fn);
    }
    (void)repo.add_folder_item(MediaKind::Image, folder, "filed.png");

    auto root = list_media(repo, MediaKind::Image, std::nullopt, "");
    REQUIRE(folder_names(root) == std::vector<std::string>{"Backgrounds"});
    // filed.png lives in the folder, so the root does not repeat it.
    auto names = filenames(root);
    std::sort(names.begin(), names.end());
    REQUIRE(names == std::vector<std::string>{"hidden.png", "loose.png"});

    // An image hidden from the library disappears from both views.
    repo.set_file_visible(seventhslide::db::MediaKind::Image, "hidden.png", false);
    names = filenames(list_media(repo, MediaKind::Image, std::nullopt, ""));
    REQUIRE(names == std::vector<std::string>{"loose.png"});

    const auto inside = list_media(repo, MediaKind::Image, folder, "");
    REQUIRE(filenames(inside) == std::vector<std::string>{"filed.png"});
    REQUIRE(inside.folders.empty());
    REQUIRE(inside.files.front().item_id > 0);
}

TEST_CASE("images: a hidden file stays hidden inside its folder", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    const auto folder = repo.folders(MediaKind::Image).create("Slides");
    repo.upsert_file(seventhslide::db::MediaKind::Image, "gone.png", "gone.png");
    (void)repo.add_folder_item(MediaKind::Image, folder, "gone.png");
    repo.set_file_visible(seventhslide::db::MediaKind::Image, "gone.png", false);

    REQUIRE(list_media(repo, MediaKind::Image, folder, "").files.empty());
}

TEST_CASE("images: the display name is the title, and both are searched", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    repo.upsert_file(seventhslide::db::MediaKind::Image, "a1b2c3.png", "Sunrise Over Water");
    repo.upsert_file(seventhslide::db::MediaKind::Image, "plain.png", "");

    const auto titled = list_media(repo, MediaKind::Image, std::nullopt, "sunrise");
    REQUIRE(titled.files.size() == 1);
    REQUIRE(titled.files.front().title == "Sunrise Over Water");
    // A file with no display name falls back to its filename.
    const auto plain = list_media(repo, MediaKind::Image, std::nullopt, "plain");
    REQUIRE(plain.files.size() == 1);
    REQUIRE(plain.files.front().title == "plain.png");
    // The opaque on-disk name still finds it, so a search never loses a file.
    REQUIRE(list_media(repo, MediaKind::Image, std::nullopt, "a1b2").files.size() == 1);
    REQUIRE(list_media(repo, MediaKind::Image, std::nullopt, "SUNRISE").files.size() == 1);
}

TEST_CASE("audio: the registry is what is listed, by title", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    const auto folder = repo.folders(MediaKind::Audio).create("Preludes");
    repo.upsert_file(MediaKind::Audio, "a1.mp3", "Prelude in G.mp3");
    repo.upsert_file(MediaKind::Audio, "b2.flac", "Offertory.flac");
    (void)repo.add_folder_item(MediaKind::Audio, folder, "a1.mp3");
    // Filed, but not in the registry (never imported, or deleted): not listed.
    (void)repo.add_folder_item(MediaKind::Audio, folder, "stray.wav");

    REQUIRE(filenames(list_media(repo, MediaKind::Audio, folder, ""))
            == std::vector<std::string>{"a1.mp3"});
    REQUIRE(filenames(list_media(repo, MediaKind::Audio, std::nullopt, ""))
            == std::vector<std::string>{"b2.flac"});
    REQUIRE(list_media(repo, MediaKind::Audio, std::nullopt, "").files.front().title
            == "Offertory.flac");
    // Searched by title as well as by name.
    REQUIRE(filenames(list_media(repo, MediaKind::Audio, std::nullopt, "OFFERTORY"))
            == std::vector<std::string>{"b2.flac"});
    REQUIRE(filenames(list_media(repo, MediaKind::Audio, std::nullopt, "FLAC"))
            == std::vector<std::string>{"b2.flac"});
    // Each kind has its own registry and folders.
    REQUIRE(list_media(repo, MediaKind::Video, std::nullopt, "").folders.empty());
    REQUIRE(list_media(repo, MediaKind::Video, std::nullopt, "").files.empty());
}

TEST_CASE("videos: the root lists only unfiled, visible files", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    const auto folder = repo.folders(MediaKind::Video).create("Bumpers");
    for (const auto* fn : {"filed.mp4", "loose.mp4", "hidden.mp4"}) {
        repo.upsert_file(MediaKind::Video, fn, fn);
    }
    (void)repo.add_folder_item(MediaKind::Video, folder, "filed.mp4");
    repo.set_file_visible(MediaKind::Video, "hidden.mp4", false);

    REQUIRE(filenames(list_media(repo, MediaKind::Video, folder, ""))
            == std::vector<std::string>{"filed.mp4"});
    REQUIRE(filenames(list_media(repo, MediaKind::Video, std::nullopt, ""))
            == std::vector<std::string>{"loose.mp4"});
    REQUIRE(list_media(repo, MediaKind::Video, std::nullopt, "nomatch").files.empty());
}

TEST_CASE("folders are filtered by the same search", "[media][listing]")
{
    Database db = open_db();
    MediaRepository repo(db);

    (void)repo.folders(MediaKind::Image).create("Christmas");
    (void)repo.folders(MediaKind::Image).create("Easter");

    REQUIRE(folder_names(list_media(repo, MediaKind::Image, std::nullopt, "chris"))
            == std::vector<std::string>{"Christmas"});
    REQUIRE(list_media(repo, MediaKind::Image, std::nullopt, "").folders.size() == 2);
}
