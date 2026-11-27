// A Freeform document's own pictures and videos (freeform/own_media.hpp):
// how they are named, taken in from elsewhere, and let go.

#include "test_temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/sync_tables.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/library/library.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace ff = seventhslide::freeform;
namespace fs = std::filesystem;

namespace {

const std::string k_owner(32, 'a');
const std::string k_other(32, 'b');

void write_file(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A one-slide document with a picture object named ``picture``, a
/// background named ``background`` and a video object named ``video``.
[[nodiscard]] ff::Document document(const std::string& picture, const std::string& background,
                                    const std::string& video = {})
{
    auto doc = ff::make_document("Promo", {"out-main"});
    auto& slide = doc.decks[0].slides[0];
    slide.background.kind = ff::SlideBackgroundKind::Image;
    slide.background.image = background;
    ff::Object image;
    image.id = "pic";
    image.type = ff::ObjectType::Image;
    image.filename = picture;
    slide.objects.push_back(image);
    if (!video.empty()) {
        ff::Object clip;
        clip.id = "clip";
        clip.type = ff::ObjectType::Video;
        clip.filename = video;
        slide.objects.push_back(clip);
    }
    return doc;
}

}  // namespace

TEST_CASE("A document's own file is named by a URL that says whose it is", "[freeform][own-media]")
{
    const ff::OwnMedia file{k_owner, "1234abcd-Sunrise.jpg"};
    const auto url = ff::own_media_url(file);
    CHECK(url == "/static/freeform_media/" + k_owner + "/1234abcd-Sunrise.jpg");
    REQUIRE(ff::own_media_of(url));
    CHECK(*ff::own_media_of(url) == file);
    CHECK(ff::own_media_at(file.place()) == file);

    // A library file, a theme's background and a path that leaves the folder
    // are none of a document's.
    CHECK_FALSE(ff::own_media_of("ab12.png"));
    CHECK_FALSE(ff::own_media_of("/static/images/ab12.png"));
    CHECK_FALSE(ff::own_media_of("/static/theme_backgrounds/t1.image.ab12.png"));
    CHECK_FALSE(ff::own_media_of("/static/freeform_media/" + k_owner));
    CHECK_FALSE(ff::own_media_of("/static/freeform_media/../images/ab12.png"));
    CHECK_FALSE(ff::own_media_of("/static/freeform_media/" + k_owner + "/../x.png"));
    CHECK_FALSE(ff::own_media_of("/static/freeform_media/" + k_owner + "/a/b.png"));
    CHECK_FALSE(ff::own_media_of("/static/freeform_media/" + k_owner + "/my photo.png"));

    // Sync takes it as a file of its own, one folder down.
    CHECK(seventhslide::db::is_sync_media_path("freeform_media/" + file.place()));
    CHECK_FALSE(seventhslide::db::is_sync_media_path("freeform_media/" + file.name));
    CHECK_FALSE(seventhslide::db::is_sync_media_path("freeform_media/" + k_owner + "/a/b.png"));
    CHECK_FALSE(seventhslide::db::is_sync_media_path("images/" + file.place()));
}

TEST_CASE("A new own file is named for what it came in as", "[freeform][own-media]")
{
    const auto name = ff::new_own_media_name("Sunrise over the lake.JPG", ".JPG");
    INFO(name);
    CHECK(ff::is_own_media_part(name));
    CHECK(name.size() > 9);
    CHECK(name.substr(8) == "-Sunrise_over_the_lake.jpg");
    CHECK(ff::own_media_title(name) == "Sunrise over the lake.jpg");
    // Two of one title are two files.
    CHECK(ff::new_own_media_name("Sunrise.jpg", ".jpg") != ff::new_own_media_name("Sunrise.jpg", ".jpg"));

    // What a URL cannot carry is a gap; with nothing left, the name is its tag.
    const auto odd = ff::new_own_media_name("Café: \"Ünïcode\"?", ".png");
    INFO(odd);
    CHECK(ff::is_own_media_part(odd));
    // Only ASCII is kept, whatever the locale: a name reads the same anywhere.
    CHECK(ff::new_own_media_name("Café", ".png").substr(8) == "-Caf.png");
    const auto bare = ff::new_own_media_name("???", ".png");
    CHECK(bare.size() == 8 + 4);
    CHECK(ff::is_own_media_part(bare));
    // A name made elsewhere is shown as it is.
    CHECK(ff::own_media_title("photo.png") == "photo.png");
}

TEST_CASE("Taking a document's files makes each its owner's own", "[freeform][own-media]")
{
    const seventhslide::test::TempDir tmp;
    const auto& root = tmp.path;
    write_file(root / "images" / "ab12.png", "library picture");
    write_file(root / "theme_backgrounds" / "t1.image.cd34.png", "theme picture");
    const ff::OwnMedia others{k_other, "1234abcd-Clip.mp4"};
    write_file(ff::own_media_path(root, others), "another's video");

    auto doc = document("ab12.png", "/static/theme_backgrounds/t1.image.cd34.png",
                        ff::own_media_url(others));
    // The library's picture named twice: one file.
    doc.decks[0].slides[0].objects[0].fill.picture.file = "/static/images/ab12.png";
    const auto taken = ff::take_own_media(doc, k_owner, root, [](std::string_view ref, seventhslide::canvas::MediaRefKind) {
        return ref.starts_with("/static/theme_backgrounds/") ? std::string("Dawn") : std::string("Lilies.png");
    });
    CHECK(taken.taken == 4);
    CHECK(taken.missing == 0);
    CHECK(taken.failed == 0);

    const auto& slide = doc.decks[0].slides[0];
    const auto picture = ff::own_media_of(slide.objects[0].filename);
    const auto fill = ff::own_media_of(slide.objects[0].fill.picture.file);
    const auto background = ff::own_media_of(slide.background.image);
    const auto video = ff::own_media_of(slide.objects[1].filename);
    REQUIRE(picture);
    REQUIRE(fill);
    REQUIRE(background);
    REQUIRE(video);
    for (const auto* f : {&*picture, &*fill, &*background, &*video}) {
        CHECK(f->owner == k_owner);
    }
    CHECK(read_file(ff::own_media_path(root, *picture)) == "library picture");
    CHECK(ff::own_media_title(picture->name) == "Lilies.png");
    CHECK(ff::own_media_title(background->name) == "Dawn.png");
    CHECK(read_file(ff::own_media_path(root, *background)) == "theme picture");
    // Another's file keeps its name; its bytes are the same.
    CHECK(video->name == others.name);
    CHECK(read_file(ff::own_media_path(root, *video)) == "another's video");
    // The originals stay where they were.
    CHECK(fs::exists(root / "images" / "ab12.png"));
    CHECK(fs::exists(ff::own_media_path(root, others)));

    // Taken again, nothing moves.
    const auto before = doc.to_json();
    CHECK(ff::take_own_media(doc, k_owner, root).taken == 0);
    CHECK(doc.to_json() == before);

    // A file not on this computer stays named as it was.
    auto waiting = document("not-here.png", "");
    const auto missing = ff::take_own_media(waiting, k_owner, root);
    CHECK(missing.missing == 1);
    CHECK(waiting.decks[0].slides[0].objects[0].filename == "not-here.png");
}

TEST_CASE("An owner's files no document names go", "[freeform][own-media]")
{
    const seventhslide::test::TempDir tmp;
    const auto& root = tmp.path;
    const ff::OwnMedia kept{k_owner, "1111aaaa-Kept.png"};
    const ff::OwnMedia shared{k_owner, "2222bbbb-Shared.png"};
    const ff::OwnMedia dropped{k_owner, "3333cccc-Dropped.png"};
    for (const auto& f : {kept, shared, dropped}) {
        write_file(ff::own_media_path(root, f), f.name);
    }
    // A file an import is still copying is no one's to take.
    const auto staging = ff::own_media_dir(root, k_owner)
                         / (std::string(seventhslide::library::k_staging_prefix) + "x"
                            + std::string(seventhslide::library::k_staging_suffix));
    write_file(staging, "half");
    // Nor one sync is still fetching, or a write's temp file.
    const auto fetching = ff::own_media_dir(root, k_owner) / "4444dddd-Coming.png.part";
    write_file(fetching, "some");
    const auto writing = ff::own_media_dir(root, k_owner) / "x.tmp";
    write_file(writing, "some");

    // The item names one; a service's copy of it, another.
    std::set<std::string, std::less<>> named;
    ff::note_own_media(document(ff::own_media_url(kept), "").to_json(), named);
    ff::note_own_media(document("library.png", ff::own_media_url(shared)).to_json(), named);
    CHECK(named == std::set<std::string, std::less<>>{kept.place(), shared.place()});

    CHECK(ff::remove_unnamed_own_media(root, k_owner, named) == 1);
    CHECK(fs::exists(ff::own_media_path(root, kept)));
    CHECK(fs::exists(ff::own_media_path(root, shared)));
    CHECK_FALSE(fs::exists(ff::own_media_path(root, dropped)));
    CHECK(fs::exists(staging));
    CHECK(fs::exists(fetching));
    CHECK(fs::exists(writing));

    // Once nothing is named, the folder goes too.
    fs::remove(staging);
    fs::remove(fetching);
    fs::remove(writing);
    CHECK(ff::remove_unnamed_own_media(root, k_owner, {}) == 2);
    CHECK_FALSE(fs::exists(ff::own_media_dir(root, k_owner)));
}
