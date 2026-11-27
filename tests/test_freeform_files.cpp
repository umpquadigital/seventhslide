// Freeform files (core/exchange): the manifest and rundown, which outputs a
// file's decks may land on, what a document becomes on the way in, and a
// file written and opened again.

#include "test_temp_dir.hpp"

#include <seventhslide/exchange/adapt.hpp>
#include <seventhslide/exchange/bundle.hpp>
#include <seventhslide/exchange/bundle_io.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/util/zip_reader.hpp>
#include <seventhslide/util/zip_writer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace ex = seventhslide::exchange;
namespace ff = seventhslide::freeform;
using seventhslide::canvas::MediaRefKind;
using seventhslide::geom::CanvasSize;
using seventhslide::model::ContentKind;

namespace {

constexpr std::string_view k_sha_a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

[[nodiscard]] std::string read_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    REQUIRE(out);
}

[[nodiscard]] ex::Manifest sample_manifest()
{
    ex::Manifest m;
    m.freeform_schema_version = ff::k_schema_version;
    m.made_by = "SeventhSlide test";
    m.made_at = "2026-11-26T12:00:00Z";
    m.source_library = "lib-1";
    m.outputs = {{"main", "Main", {1920, 1080}}, {"stage", "Stage", {1024, 768}}};
    m.folders = {{"f1", "Easter", ""}, {"f2", "Sunday", "f1"}};
    m.items = {{"items/0001.json", "previews/0001.png", "Welcome", "f2", {"easter"}},
               {"items/0002.json", "", "Notices", "", {}}};
    m.media = {{"media/" + std::string(k_sha_a) + ".jpg", MediaRefKind::Picture, "Sunrise", 10, std::string(k_sha_a)}};
    m.fonts = {{"fonts/" + std::string(k_sha_a) + ".ttf", "Lato", "Bold", true, false, false, 20, std::string(k_sha_a)}};
    m.media_not_included = {"Gone.jpg"};
    m.fonts_not_included = {"Gotham"};
    m.has_rundown = true;
    return m;
}

/// A document with a deck per id, each with a picture at (100, 100, 400, 200).
[[nodiscard]] ff::Document sample_document(const std::vector<std::string>& outputs, const std::string& picture)
{
    auto doc = ff::make_document("Welcome", outputs);
    for (auto& deck : doc.decks) {
        ff::Object image;
        image.type = ff::ObjectType::Image;
        image.filename = picture;
        image.x = 100;
        image.y = 100;
        image.w = 400;
        image.h = 200;
        deck.slides.front().objects.push_back(image);
    }
    return doc;
}

}  // namespace

TEST_CASE("A manifest reads back as it was written", "[freeform-files]")
{
    const auto m = sample_manifest();
    std::string refusal;
    const auto back = ex::read_manifest(ex::to_json(m), refusal);
    REQUIRE(back);
    CHECK(refusal.empty());
    CHECK(*back == m);
}

TEST_CASE("A manifest out of shape is refused in words for people", "[freeform-files]")
{
    const auto good = ex::to_json(sample_manifest());
    const auto refused = [](nlohmann::json j) {
        std::string refusal;
        const bool ok = ex::read_manifest(j, refusal).has_value();
        if (!ok) {
            CHECK_FALSE(refusal.empty());
        }
        return !ok;
    };
    CHECK_FALSE(refused(good));

    auto other = good;
    other["format"] = "something.else";
    CHECK(refused(other));

    auto newer = good;
    newer["format_version"] = ex::k_bundle_format_version + 1;
    std::string why;
    CHECK_FALSE(ex::read_manifest(newer, why));
    CHECK(why.find("newer") != std::string::npos);

    auto escape = good;
    escape["items"][0]["entry"] = "items/../../etc/passwd";
    CHECK(refused(escape));

    auto wrong_dir = good;
    wrong_dir["media"][0]["entry"] = "fonts/" + std::string(k_sha_a) + ".jpg";
    CHECK(refused(wrong_dir));

    auto twice = good;
    twice["items"][1]["entry"] = "items/0001.json";
    CHECK(refused(twice));

    auto cycle = good;
    cycle["folders"][0]["parent"] = "f2";
    CHECK(refused(cycle));

    auto no_folder = good;
    no_folder["items"][0]["folder"] = "f9";
    CHECK(refused(no_folder));

    auto bad_sha = good;
    bad_sha["media"][0]["sha256"] = "not a hash";
    CHECK(refused(bad_sha));

    auto bad_kind = good;
    bad_kind["media"][0]["kind"] = "audio";
    CHECK(refused(bad_kind));

    auto bad_canvas = good;
    bad_canvas["outputs"][0]["width"] = 0;
    CHECK(refused(bad_canvas));

    auto wrong_type = good;
    wrong_type["items"][0]["name"] = 7;
    CHECK(refused(wrong_type));
}

TEST_CASE("JSON nested past the limit is refused before it is parsed", "[freeform-files]")
{
    CHECK(ex::parse_json_limited(R"({"a": [1, {"b": "[[[["}]})"));
    const std::string deep = std::string(ex::k_max_json_depth + 1, '[') + std::string(ex::k_max_json_depth + 1, ']');
    CHECK_FALSE(ex::parse_json_limited(deep));
    const std::string ok = std::string(ex::k_max_json_depth, '[') + std::string(ex::k_max_json_depth, ']');
    CHECK(ex::parse_json_limited(ok));
    CHECK_FALSE(ex::parse_json_limited("{not json"));
}

TEST_CASE("Entry names are ours alone", "[freeform-files]")
{
    CHECK(ex::item_entry(3) == "items/0003.json");
    CHECK(ex::preview_entry(12) == "previews/0012.png");
    CHECK(ex::media_entry(k_sha_a, ".JPG") == "media/" + std::string(k_sha_a) + ".jpg");
    CHECK(ex::is_entry_in("media/abc.jpg", ex::k_media_dir));
    CHECK_FALSE(ex::is_entry_in("media/", ex::k_media_dir));
    CHECK_FALSE(ex::is_entry_in("media/.hidden", ex::k_media_dir));
    CHECK_FALSE(ex::is_entry_in("media/a/b.jpg", ex::k_media_dir));
    CHECK_FALSE(ex::is_entry_in("media/..", ex::k_media_dir));
    CHECK(ex::is_package_file("Media/04 Notices/01 Bake sale.jpg"));
    CHECK_FALSE(ex::is_package_file("Media/../x.jpg"));
    CHECK_FALSE(ex::is_package_file("Media//x.jpg"));
    CHECK_FALSE(ex::is_package_file("Media\\x.jpg"));
    CHECK_FALSE(ex::is_package_file("other/x.jpg"));
    CHECK(ex::bundle_extension_for(1, false) == ".ssff");
    CHECK(ex::bundle_extension_for(1, true) == ".ssfb");
    CHECK(ex::bundle_extension_for(2, false) == ".ssfb");
}

TEST_CASE("A rundown of every kind of row reads back as it was written", "[freeform-files]")
{
    const auto m = sample_manifest();
    ex::Rundown r;
    r.name = "Sunday 30 November";
    r.tags = {"Communion"};
    r.service_at = "2026-11-30T10:00:00";
    r.audio_fades = {{"fade_in_ms", 500}};
    const auto row = [](ContentKind kind, std::string title) {
        ex::RundownRow made;
        made.kind = kind;
        made.title = std::move(title);
        return made;
    };
    auto song = row(ContentKind::Song, "Amazing Grace");
    song.item = "items/0001.json";
    song.stack_id = "lyrics";
    song.stack_name = "Lyrics";
    song.also_play = {2};
    auto video = row(ContentKind::Video, "Welcome loop");
    video.files = {"Media/02 Welcome loop.mp4"};
    video.data = {{"autoplay", true}, {"loop", true}};
    auto folder = row(ContentKind::ImageFolder, "Notices");
    folder.files = {"Media/03 Notices/01 a.jpg", "Media/03 Notices/02 b.jpg"};
    auto web = row(ContentKind::Web, "Giving");
    web.data = {{"name", "Giving"}, {"url", "https://example.org/give"}, {"width", 1920}, {"height", 1080}, {"zoom", 1.0}};
    auto live = row(ContentKind::Live, "Stream");
    live.data = {{"kind", "stream"}, {"name", "Stream"}, {"url", "srt://host:9000?passphrase=secret"}};
    auto divider = row(ContentKind::Divider, "Sermon");
    r.rows = {song, video, folder, web, live, divider};

    std::string refusal;
    const auto back = ex::read_rundown(ex::to_json(r), m.items, refusal);
    REQUIRE(back);
    CHECK(*back == r);
    // The key goes whole.
    CHECK(back->rows[4].data["url"] == "srt://host:9000?passphrase=secret");

    auto j = ex::to_json(r);
    j["rows"][0]["item"] = "items/0009.json";
    CHECK_FALSE(ex::read_rundown(j, m.items, refusal));
    j = ex::to_json(r);
    j["rows"][1]["files"] = {"../escape.mp4"};
    CHECK_FALSE(ex::read_rundown(j, m.items, refusal));
    j = ex::to_json(r);
    j["rows"][0]["also_play"] = {99};
    CHECK_FALSE(ex::read_rundown(j, m.items, refusal));
    j = ex::to_json(r);
    j["rows"][0]["kind"] = "nothing";
    CHECK_FALSE(ex::read_rundown(j, m.items, refusal));
}

TEST_CASE("A deck goes only to an output of exactly its shape", "[freeform-files]")
{
    const ex::BundleOutput hd{"x", "Main", {1920, 1080}};
    const std::vector<ex::LibraryOutput> outputs = {
        {"a", "4K", {3840, 2160}},
        {"b", "Laptop", {1366, 768}},
        {"c", "Wide", {1920, 1200}},
        {"d", "Small", {1280, 720}},
    };
    CHECK(ex::outputs_that_fit(hd, outputs) == std::vector<std::size_t>{0, 3});
    const ex::BundleOutput laptop{"y", "Laptop", {1366, 768}};
    CHECK(ex::outputs_that_fit(laptop, outputs) == std::vector<std::size_t>{1});
}

TEST_CASE("The first choice of outputs: id, then name, then the first free of its shape", "[freeform-files]")
{
    const std::vector<ex::BundleOutput> file = {
        {"main", "Main", {1920, 1080}},
        {"other", "STAGE", {1920, 1080}},
        {"third", "Lobby", {1920, 1080}},
        {"square", "Square", {1080, 1080}},
    };
    const std::vector<ex::LibraryOutput> library = {
        {"aa", "Lobby TV", {1280, 720}},
        {"main", "Projector", {1920, 1080}},
        {"bb", "Stage", {3840, 2160}},
        {"cc", "Stream", {1920, 1080}},
    };
    const auto mapping = ex::suggest_mapping(file, library);
    CHECK(mapping == ex::OutputMapping{"main", "bb", "aa", ""});
    CHECK(ex::mapping_refusal(file, library, mapping).empty());

    // A name of another shape is not a match.
    const std::vector<ex::BundleOutput> four_three = {{"z", "Stage", {1024, 768}}};
    CHECK(ex::suggest_mapping(four_three, library) == ex::OutputMapping{""});
}

TEST_CASE("A mapping that cannot be used says why", "[freeform-files]")
{
    const std::vector<ex::BundleOutput> file = {{"m", "Main", {1920, 1080}}, {"s", "Stage", {1920, 1080}}};
    const std::vector<ex::LibraryOutput> library = {{"a", "A", {1920, 1080}}, {"b", "B", {1024, 768}}};
    CHECK_FALSE(ex::mapping_refusal(file, library, {"a", "a"}).empty());
    CHECK_FALSE(ex::mapping_refusal(file, library, {"b", ""}).empty());
    CHECK_FALSE(ex::mapping_refusal(file, library, {"zz", ""}).empty());
    CHECK_FALSE(ex::mapping_refusal(file, library, {"", ""}).empty());
    CHECK_FALSE(ex::mapping_refusal(file, library, {"a"}).empty());
    CHECK(ex::mapping_refusal(file, library, {"", "a"}).empty());
}

TEST_CASE("A document lands on its outputs, fitted, with its files repointed", "[freeform-files]")
{
    auto doc = sample_document({"main", "stage", "gone"}, "media/pic.jpg");
    doc.decks[0].hide_verse_refs = true;
    const std::vector<ex::BundleOutput> file = {{"main", "Main", {1920, 1080}}, {"stage", "Stage", {1920, 1080}}};
    const std::vector<ex::LibraryOutput> library = {{"uhd", "UHD", {3840, 2160}}, {"hd", "HD", {1920, 1080}}};

    const auto adapted = ex::adapt_document(doc, file, library, {"uhd", ""}, {{"media/pic.jpg", "1234.jpg"}});
    REQUIRE(adapted.document);
    CHECK(adapted.refusal.empty());
    const auto& out = *adapted.document;
    REQUIRE(out.decks.size() == 1);
    CHECK(out.decks[0].output_id == "uhd");
    CHECK(out.decks[0].canvas == CanvasSize{3840, 2160});
    CHECK(out.decks[0].hide_verse_refs);
    const auto& image = out.decks[0].slides.front().objects.front();
    CHECK(image.filename == "1234.jpg");
    CHECK(image.x == 200);
    CHECK(image.w == 800);
    CHECK(image.h == 400);

    // A file not taken in leaves the reference empty.
    const auto unnamed = ex::adapt_document(doc, file, library, {"", "hd"}, {});
    REQUIRE(unnamed.document);
    CHECK(unnamed.document->decks[0].output_id == "hd");
    CHECK(unnamed.document->decks[0].slides.front().objects.front().filename.empty());

    // Every deck left out.
    const auto none = ex::adapt_document(doc, file, library, {"", ""}, {});
    CHECK_FALSE(none.document);
    CHECK_FALSE(none.refusal.empty());
}

TEST_CASE("A line-control document keeps its lines on the decks that land", "[freeform-files]")
{
    auto doc = ff::make_document("Song", {"main", "stage"});
    doc.line_control = true;
    doc.cues = {{"c1", "Line one"}, {"c2", "Line two"}};
    for (auto& deck : doc.decks) {
        deck.line_to_slide = {0, 0};
    }
    ff::normalize(doc);
    const std::vector<ex::BundleOutput> file = {{"main", "Main", {1920, 1080}}, {"stage", "Stage", {1920, 1080}}};
    const std::vector<ex::LibraryOutput> library = {{"hd", "HD", {1920, 1080}}};
    const auto adapted = ex::adapt_document(doc, file, library, {"", "hd"}, {});
    REQUIRE(adapted.document);
    CHECK(adapted.document->line_control);
    CHECK(adapted.document->cues.size() == 2);
    REQUIRE(adapted.document->decks.size() == 1);
    CHECK(adapted.document->decks[0].line_to_slide.size() == 2);
}

TEST_CASE("Only the library's outputs' decks leave it", "[freeform-files]")
{
    auto doc = sample_document({"main", "deleted"}, "a.jpg");
    CHECK(ex::keep_decks_of(doc, {"main", "stage"}));
    REQUIRE(doc.decks.size() == 1);
    CHECK(doc.decks[0].output_id == "main");
    CHECK_FALSE(ex::keep_decks_of(doc, {"stage"}));
    CHECK(ex::entries_named(sample_document({"m"}, "media/x.jpg")) == std::set<std::string>{"media/x.jpg"});
}

TEST_CASE("A Freeform file written opens again with its items, files and fonts", "[freeform-files]")
{
    seventhslide::test::TempDir tmp;
    write_file(tmp.path / "one.jpg", "picture bytes");
    write_file(tmp.path / "same.jpg", "picture bytes");  // the same bytes under another name
    write_file(tmp.path / "Lato-Bold.ttf", "font bytes");

    ex::BundleSource source;
    source.made_by = "test";
    source.source_library = "lib-1";
    source.outputs = {{"main", "Main", {1920, 1080}}};
    source.folders = {{"f1", "Easter", ""}};
    source.items.push_back({"Welcome", "f1", {"tag"}, sample_document({"main"}, "one.jpg"), "PNGDATA"});
    source.items.push_back({"Again", "", {}, sample_document({"main"}, "same.jpg"), ""});
    source.items.push_back({"Gone", "", {}, sample_document({"main"}, "missing.jpg"), ""});
    source.media["one.jpg"] = {tmp.path / "one.jpg", "Sunrise", MediaRefKind::Picture};
    source.media["same.jpg"] = {tmp.path / "same.jpg", "Sunrise copy", MediaRefKind::Picture};
    source.media["missing.jpg"] = {{}, "Lost picture", MediaRefKind::Picture};
    seventhslide::db::LibraryFont lato;
    lato.filename = "abc.ttf";
    lato.family = "Lato";
    lato.style = "Bold";
    lato.bold = true;
    lato.display_name = "Lato Bold";
    source.fonts.push_back({tmp.path / "Lato-Bold.ttf", lato});
    source.fonts_not_included = {"Gotham"};

    const auto file = tmp.path / "Welcome.ssfb";
    int heard = 0;
    const auto written = ex::write_bundle(source, file, [&heard](std::uint64_t, std::uint64_t) {
        ++heard;
        return true;
    });
    REQUIRE(written.ok);
    CHECK(heard > 0);
    CHECK(written.media_files == 1);
    CHECK(written.font_files == 1);
    CHECK(written.media_not_included == std::vector<std::string>{"Lost picture"});

    std::string refusal;
    const auto opened = ex::open_freeform_file(file, refusal);
    REQUIRE(opened);
    CHECK(refusal.empty());
    CHECK_FALSE(opened->package);
    const auto& m = opened->manifest;
    REQUIRE(m.items.size() == 3);
    CHECK(m.items[0].name == "Welcome");
    CHECK(m.items[0].folder == "f1");
    REQUIRE(m.media.size() == 1);
    CHECK(m.media[0].title == "Sunrise");
    CHECK(m.media[0].size == 13);
    REQUIRE(m.fonts.size() == 1);
    CHECK(m.fonts[0].family == "Lato");
    CHECK(m.fonts_not_included == std::vector<std::string>{"Gotham"});
    CHECK(ex::read_preview(*opened, 0) == "PNGDATA");
    CHECK(ex::read_preview(*opened, 1).empty());

    REQUIRE(opened->items.size() == 3);
    for (const auto& item : opened->items) {
        REQUIRE(item.document);
    }
    // Both pictures name one entry; the missing one names none.
    const auto& first = opened->items[0].document->decks[0].slides[0].objects[0].filename;
    CHECK(first == m.media[0].entry);
    CHECK(opened->items[1].document->decks[0].slides[0].objects[0].filename == first);
    CHECK(opened->items[2].document->decks[0].slides[0].objects[0].filename.empty());

    std::string error;
    const auto* entry = opened->bundle.find(first);
    REQUIRE(entry != nullptr);
    CHECK(opened->bundle.read(*entry, 100, error) == std::optional<std::string>("picture bytes"));

    // Stopped part way, nothing is left.
    const auto stopped = ex::write_bundle(source, tmp.path / "stopped.ssfb",
                                          [](std::uint64_t, std::uint64_t) { return false; });
    CHECK_FALSE(stopped.ok);
    CHECK(stopped.stopped);
    CHECK_FALSE(fs::exists(tmp.path / "stopped.ssfb"));
}

TEST_CASE("A package opens to its bundle, its rundown and its loose files", "[freeform-files]")
{
    seventhslide::test::TempDir tmp;
    write_file(tmp.path / "loop.mp4", "video bytes");
    ex::BundleSource source;
    source.outputs = {{"main", "Main", {1920, 1080}}};
    source.items.push_back({"Amazing Grace", "", {}, sample_document({"main"}, ""), ""});
    ex::Rundown rundown;
    rundown.name = "Sunday";
    ex::RundownRow song;
    song.kind = ContentKind::Song;
    song.title = "Amazing Grace";
    song.item = ex::item_entry(1);
    ex::RundownRow video;
    video.kind = ContentKind::Video;
    video.title = "Loop";
    video.files = {"Media/02 Loop.mp4"};
    rundown.rows = {song, video};
    source.rundown = rundown;
    REQUIRE(ex::write_bundle(source, tmp.path / "Sunday.ssfb", {}).ok);

    const auto package = tmp.path / "Sunday.zip";
    const auto written = ex::write_package(package, "Sunday.ssfb", tmp.path / "Sunday.ssfb",
                                           {{"Media/02 Loop.mp4", tmp.path / "loop.mp4"}}, {});
    REQUIRE(written.ok);
    CHECK(written.media_files == 1);

    std::string refusal;
    const auto opened = ex::open_freeform_file(package, refusal);
    INFO(refusal);
    REQUIRE(opened);
    REQUIRE(opened->package);
    REQUIRE(opened->rundown);
    CHECK(opened->rundown->name == "Sunday");
    CHECK(opened->rundown->rows.size() == 2);
    const auto* loose = opened->package->find("Media/02 Loop.mp4");
    REQUIRE(loose != nullptr);
    std::string error;
    CHECK(opened->package->read(*loose, 100, error) == std::optional<std::string>("video bytes"));

    // An archive that is neither.
    std::ostringstream out(std::ios::binary);
    seventhslide::util::ZipWriter zip(out, {});
    REQUIRE(zip.add("readme.txt", "hello"));
    REQUIRE(zip.finish());
    write_file(tmp.path / "other.zip", out.str());
    CHECK_FALSE(ex::open_freeform_file(tmp.path / "other.zip", refusal));
    CHECK(refusal.find("not a Freeform file") != std::string::npos);
}

TEST_CASE("A file whose documents are at another Freeform schema is refused", "[freeform-files]")
{
    seventhslide::test::TempDir tmp;
    auto m = sample_manifest();
    m.media.clear();
    m.fonts.clear();
    m.items.resize(1);
    m.items[0].preview.clear();
    m.items[0].folder.clear();
    m.has_rundown = false;
    m.freeform_schema_version = ff::k_schema_version + 1;
    std::ostringstream out(std::ios::binary);
    seventhslide::util::ZipWriter zip(out, {});
    REQUIRE(zip.add(ex::k_manifest_entry, ex::to_json(m).dump()));
    REQUIRE(zip.add("items/0001.json", ff::make_document("x", {"main"}).to_json().dump()));
    REQUIRE(zip.finish());
    write_file(tmp.path / "newer.ssff", out.str());
    std::string refusal;
    CHECK_FALSE(ex::open_freeform_file(tmp.path / "newer.ssff", refusal));
    CHECK(refusal.find("newer SeventhSlide") != std::string::npos);
}

TEST_CASE("Fonts come in only where the library has no face in the slot", "[freeform-files]")
{
    auto m = sample_manifest();
    const std::string sha_b(64, 'b');
    const std::string sha_c(64, 'c');
    m.fonts = {
        {"fonts/" + std::string(k_sha_a) + ".ttf", "Lato", "Bold", true, false, false, 1, std::string(k_sha_a)},
        {"fonts/" + sha_b + ".ttf", "Lato", "Regular", false, false, false, 1, sha_b},
        {"fonts/" + sha_c + ".ttf", "Unused", "Regular", false, false, false, 1, sha_c},
    };
    seventhslide::db::LibraryFont own;
    own.filename = "other.ttf";
    own.family = "lato";
    own.bold = true;
    std::vector<std::string> kept;
    const auto take = ex::fonts_to_take(m, {"Lato"}, {own}, kept);
    CHECK(take == std::vector<std::size_t>{1});
    CHECK(kept == std::vector<std::string>{"Lato Bold"});

    // The same file already here is said nothing of.
    seventhslide::db::LibraryFont same = own;
    same.filename = std::string(k_sha_a) + ".ttf";
    kept.clear();
    CHECK(ex::fonts_to_take(m, {"LATO"}, {same}, kept) == std::vector<std::size_t>{1});
    CHECK(kept.empty());
}

TEST_CASE("Folders that loop are refused, however long the loop", "[freeform-files]")
{
    auto m = sample_manifest();
    const auto refused = [&](std::vector<ex::BundleFolder> folders) {
        auto copy = m;
        copy.folders = std::move(folders);
        for (auto& item : copy.items) {
            item.folder.clear();
        }
        std::string refusal;
        return !ex::read_manifest(ex::to_json(copy), refusal).has_value();
    };
    CHECK(refused({{"a", "A", "a"}}));                                  // its own parent
    CHECK(refused({{"a", "A", "b"}, {"b", "B", "c"}, {"c", "C", "b"}}));  // a loop that a hangs off
    CHECK(refused({{"a", "A", "missing"}}));
    CHECK_FALSE(refused({{"a", "A", ""}, {"b", "B", "a"}, {"c", "C", "b"}}));

    // A long chain is read in one pass, not one walk per folder.
    std::vector<ex::BundleFolder> chain;
    for (int i = 0; i < 5000; ++i) {
        chain.push_back({"k" + std::to_string(i), "F", i == 0 ? std::string() : "k" + std::to_string(i - 1)});
    }
    CHECK_FALSE(refused(chain));
}

TEST_CASE("The JSON depth check reads strings as strings", "[freeform-files]")
{
    // Brackets inside strings, escaped quotes and backslashes do not count.
    const std::string deep_in_string = R"({"a": ")" + std::string(500, '[') + R"(\"\\", "b": [1]})";
    CHECK(ex::parse_json_limited(deep_in_string));
    // Closing brackets with none open, then too many opened: still refused.
    const std::string unbalanced = std::string(50, ']') + std::string(ex::k_max_json_depth + 1, '[');
    CHECK_FALSE(ex::parse_json_limited(unbalanced));
}

TEST_CASE("A file that holds nothing is refused", "[freeform-files]")
{
    auto m = sample_manifest();
    m.items.clear();
    m.has_rundown = false;
    std::string refusal;
    CHECK_FALSE(ex::read_manifest(ex::to_json(m), refusal));
    CHECK(refusal.find("nothing") != std::string::npos);
}

TEST_CASE("A rundown row keeps only what its kind holds, of the types it holds", "[freeform-files]")
{
    const auto m = sample_manifest();
    ex::RundownRow video;
    video.kind = ContentKind::Video;
    video.title = "Loop";
    video.files = {"Media/01 Loop.mp4"};
    ex::RundownRow folder;
    folder.kind = ContentKind::ImageFolder;
    folder.title = "Notices";
    ex::Rundown r;
    r.name = "S";
    r.rows = {video, folder};
    auto j = ex::to_json(r);
    j["rows"][0]["data"] = {{"loop", "yes"}, {"autoplay", false}, {"filename", "../../x"}};
    j["rows"][0]["also_play"] = {0, 1, 1};
    j["rows"][1]["data"] = {{"folder_name", 5}};
    std::string refusal;
    const auto back = ex::read_rundown(j, m.items, refusal);
    REQUIRE(back);
    CHECK(back->rows[0].data == nlohmann::json{{"autoplay", false}});
    CHECK(back->rows[0].also_play == std::vector<std::size_t>{1});  // not itself, and once
    CHECK(back->rows[1].data == nlohmann::json::object());

    // A media row may come without its file, never with two.
    j = ex::to_json(r);
    j["rows"][0]["files"] = nlohmann::json::array();
    CHECK(ex::read_rundown(j, m.items, refusal));
    j["rows"][0]["files"] = {"Media/a.mp4", "Media/b.mp4"};
    CHECK_FALSE(ex::read_rundown(j, m.items, refusal));
}

TEST_CASE("An entry's name keeps only an extension a reader takes", "[freeform-files]")
{
    CHECK(ex::media_entry(k_sha_a, ".Jpeg") == "media/" + std::string(k_sha_a) + ".jpeg");
    CHECK(ex::media_entry(k_sha_a, ".my picture") == "media/" + std::string(k_sha_a));
    CHECK(ex::media_entry(k_sha_a, ".ünï") == "media/" + std::string(k_sha_a));
    CHECK(ex::media_entry(k_sha_a, ".abcdefghijk") == "media/" + std::string(k_sha_a));
    CHECK(ex::is_entry_in(ex::media_entry(k_sha_a, ".my picture"), ex::k_media_dir));
}

TEST_CASE("File outputs of one shape each get an output of it while there are any", "[freeform-files]")
{
    // Shapes match exactly, so outputs of one shape fit the same outputs here
    // and those of different shapes fit none in common.
    const std::vector<ex::BundleOutput> file = {
        {"a", "A", {1920, 1080}}, {"b", "B", {1920, 1080}}, {"c", "C", {1920, 1080}}, {"w", "W", {1920, 1200}}};
    const std::vector<ex::LibraryOutput> library = {
        {"x", "X", {1920, 1080}}, {"y", "Y", {1280, 720}}, {"z", "Z", {2560, 1600}}};
    CHECK(ex::suggest_mapping(file, library) == ex::OutputMapping{"x", "y", "", "z"});
}

TEST_CASE("An output here takes one deck, whatever the file says", "[freeform-files]")
{
    auto doc = sample_document({"main", "main"}, "");  // two decks for one output
    const std::vector<ex::BundleOutput> file = {{"main", "Main", {1920, 1080}}};
    const std::vector<ex::LibraryOutput> library = {{"hd", "HD", {1920, 1080}}};
    const auto adapted = ex::adapt_document(doc, file, library, {"hd"}, {});
    REQUIRE(adapted.document);
    CHECK(adapted.document->decks.size() == 1);
}

TEST_CASE("A Freeform file stopped while it is packed leaves nothing", "[freeform-files]")
{
    seventhslide::test::TempDir tmp;
    write_file(tmp.path / "big.bin", std::string(3 << 20, 'z'));
    ex::BundleSource source;
    source.outputs = {{"main", "Main", {1920, 1080}}};
    source.items.push_back({"Big", "", {}, sample_document({"main"}, "big.png"), ""});
    source.media["big.png"] = {tmp.path / "big.bin", "Big", MediaRefKind::Picture};
    int calls = 0;
    // Past the first read (naming the file), into the second (packing it).
    const auto written = ex::write_bundle(source, tmp.path / "out.ssfb", [&calls](std::uint64_t done, std::uint64_t total) {
        ++calls;
        return done < total * 3 / 4;
    });
    CHECK_FALSE(written.ok);
    CHECK(written.stopped);
    CHECK(calls > 3);
    CHECK_FALSE(fs::exists(tmp.path / "out.ssfb"));
}

TEST_CASE("A file whose entries are not as its manifest says is refused", "[freeform-files]")
{
    seventhslide::test::TempDir tmp;
    auto m = sample_manifest();
    m.fonts.clear();
    m.items.resize(1);
    m.items[0].preview.clear();
    m.items[0].folder.clear();
    m.has_rundown = false;
    const auto write = [&](const fs::path& path, std::uint64_t media_size, bool with_media) {
        auto copy = m;
        copy.media[0].size = media_size;
        std::ostringstream out(std::ios::binary);
        seventhslide::util::ZipWriter zip(out, {});
        REQUIRE(zip.add(ex::k_manifest_entry, ex::to_json(copy).dump()));
        REQUIRE(zip.add("items/0001.json", ff::make_document("x", {"main"}).to_json().dump()));
        if (with_media) {
            REQUIRE(zip.add(copy.media[0].entry, "0123456789"));
        }
        REQUIRE(zip.finish());
        write_file(path, out.str());
    };
    std::string refusal;
    write(tmp.path / "good.ssfb", 10, true);
    CHECK(ex::open_freeform_file(tmp.path / "good.ssfb", refusal));
    write(tmp.path / "size.ssfb", 11, true);
    CHECK_FALSE(ex::open_freeform_file(tmp.path / "size.ssfb", refusal));
    write(tmp.path / "gone.ssfb", 10, false);
    CHECK_FALSE(ex::open_freeform_file(tmp.path / "gone.ssfb", refusal));

    // A package holding two Freeform files, each sound, is not one.
    const auto bundle = read_bytes(tmp.path / "good.ssfb");
    std::ostringstream out(std::ios::binary);
    seventhslide::util::ZipWriter zip(out, {});
    REQUIRE(zip.add("One.ssfb", bundle));
    std::ostringstream one(std::ios::binary);
    seventhslide::util::ZipWriter single(one, {});
    REQUIRE(single.add("One.ssfb", bundle));
    REQUIRE(single.finish());
    write_file(tmp.path / "one.zip", one.str());
    REQUIRE(ex::open_freeform_file(tmp.path / "one.zip", refusal));  // one is a package
    REQUIRE(zip.add("Two.ssfb", bundle));
    REQUIRE(zip.finish());
    write_file(tmp.path / "two.zip", out.str());
    CHECK_FALSE(ex::open_freeform_file(tmp.path / "two.zip", refusal));
    CHECK(refusal.find("not a Freeform file") != std::string::npos);
}

TEST_CASE("A name longer than a file keeps is cut, not refused", "[freeform-files]")
{
    auto m = sample_manifest();
    std::string name;
    for (int i = 0; i < 300; ++i) {
        name += "é";  // two bytes each: 600 in all
    }
    m.items[0].name = name;
    std::string refusal;
    const auto back = ex::read_manifest(ex::to_json(m), refusal);
    REQUIRE(back);
    CHECK(back->items[0].name.size() == 400);
    CHECK(back->items[0].name.substr(0, 2) == "é");
}
