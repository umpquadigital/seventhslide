// A library's fonts: the registry, taking fonts in, and the fonts the program
// ships.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "font_store.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/util/read_file.hpp>

#include <QFile>
#include <QRawFont>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using seventhslide::app::FontStore;
using seventhslide::db::Database;
using seventhslide::db::FontRepository;
using seventhslide::db::LibraryFont;
using seventhslide::test::fixture_font;

namespace {

Database library()
{
    seventhslide::test::ensure_qt_application();
    auto db = Database::open_memory();
    (void)seventhslide::db::ensure_schema(db);
    return db;
}

LibraryFont row(std::string filename, std::string family, bool bold, std::int64_t added_at)
{
    return {.filename = std::move(filename),
            .display_name = family + (bold ? " Bold" : " Regular"),
            .family = family,
            .style = bold ? "Bold" : "Regular",
            .bold = bold,
            .italic = false,
            .variable = false,
            .added_at = added_at};
}

std::vector<std::string> names_of(const std::vector<LibraryFont>& fonts)
{
    std::vector<std::string> out;
    std::ranges::transform(fonts, std::back_inserter(out), &LibraryFont::filename);
    return out;
}

std::vector<fs::path> files_in(const fs::path& folder)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        out.push_back(entry.path());
    }
    return out;
}

/// The regular fixture with its head table's "modified" date changed: the same
/// font to look at, other bytes, as another version of it is.
fs::path another_version(const fs::path& dir)
{
    auto bytes = seventhslide::util::read_file(fixture_font("SSFixtureSans-Regular.ttf"));
    REQUIRE(bytes.has_value());
    const auto count = (static_cast<unsigned char>((*bytes)[4]) << 8)
                       | static_cast<unsigned char>((*bytes)[5]);
    for (int i = 0; i < count; ++i) {
        const auto record = static_cast<std::size_t>(12 + i * 16);
        if (bytes->compare(record, 4, "head") == 0) {
            std::size_t offset = 0;
            for (std::size_t b = 0; b < 4; ++b) {
                offset = (offset << 8U) | static_cast<unsigned char>((*bytes)[record + 8 + b]);
            }
            (*bytes)[offset + 35] = static_cast<char>((*bytes)[offset + 35] ^ 0x55);
        }
    }
    const auto path = dir / "another-version.ttf";
    std::ofstream(path, std::ios::binary) << *bytes;
    return path;
}

}  // namespace

TEST_CASE("A slot shows the face added last, on every computer alike", "[fonts][db]")
{
    auto db = library();
    FontRepository fonts(db);
    // As two computers adding two versions apart leave it, whichever way the
    // rows arrived; a family's case does not make it another family.
    fonts.add(row("older.ttf", "Montserrat", false, 100));
    fonts.add(row("newer.ttf", "MONTSERRAT", false, 200));
    fonts.add(row("bold.ttf", "Montserrat", true, 50));
    fonts.add(row("other.ttf", "Lato", false, 10));

    CHECK(names_of(fonts.list_all())
          == std::vector<std::string>{"other.ttf", "newer.ttf", "older.ttf", "bold.ttf"});
    CHECK(names_of(fonts.list_faces())
          == std::vector<std::string>{"other.ttf", "newer.ttf", "bold.ttf"});
    CHECK(names_of(fonts.list_family("montserrat"))
          == std::vector<std::string>{"newer.ttf", "older.ttf", "bold.ttf"});

    // The same moment: the name settles it.
    fonts.add(row("aaa.ttf", "Lato", false, 10));
    CHECK(names_of(fonts.list_faces()).front() == "aaa.ttf");

    const auto got = fonts.get("bold.ttf");
    REQUIRE(got.has_value());
    CHECK(*got == row("bold.ttf", "Montserrat", true, 50));
    CHECK_FALSE(fonts.get("none.ttf").has_value());
    CHECK(fonts.remove("bold.ttf"));
    CHECK_FALSE(fonts.remove("bold.ttf"));
}

TEST_CASE("A font added is a file named for its content, and a row", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto source = fixture_font("SSFixtureSans-Regular.ttf");
    const auto bytes = seventhslide::util::read_file(source);
    REQUIRE(bytes.has_value());

    const auto added = store.import_file(source);
    CHECK(added.error.empty());
    CHECK(added.replaced == 0);
    REQUIRE(added.faces.size() == 1);
    const auto name = seventhslide::crypto::sha256_hex(*bytes) + ".ttf";
    CHECK(added.faces.front().filename == name);
    CHECK(added.faces.front().display_name == "SSFixture Sans Regular");
    CHECK(seventhslide::util::read_file(dir.path / "fonts" / name) == bytes);
    FontRepository fonts(db);
    CHECK(names_of(fonts.list_all()) == std::vector<std::string>{name});

    // The same bytes again, from anywhere, are the same record.
    const auto copy = dir.path / "copy with another name.ttf";
    fs::copy_file(source, copy);
    const auto again = store.import_file(copy);
    CHECK(again.error.empty());
    CHECK(again.replaced == 0);
    CHECK(fonts.list_all().size() == 1);
    CHECK(files_in(dir.path / "fonts").size() == 1);

    // Another slot of the family stays beside it.
    const auto bold = store.import_file(fixture_font("SSFixtureSans-Bold.ttf"));
    CHECK(bold.error.empty());
    CHECK(bold.replaced == 0);
    CHECK(fonts.list_faces().size() == 2);
    CHECK(files_in(dir.path / "fonts").size() == 2);

    // Removing takes the row and the file.
    CHECK(store.remove(name).empty());
    CHECK(fonts.list_all().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / "fonts" / name));
}

TEST_CASE("Another version of a font takes its place, and its file goes", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto first = store.import_file(fixture_font("SSFixtureSans-Regular.ttf"));
    REQUIRE(first.faces.size() == 1);
    const auto kept = store.import_file(fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(kept.faces.size() == 1);

    const auto second = store.import_file(another_version(dir.path));
    CHECK(second.error.empty());
    CHECK(second.replaced == 1);
    REQUIRE(second.faces.size() == 1);
    CHECK(second.faces.front().filename != first.faces.front().filename);
    CHECK_FALSE(fs::exists(dir.path / "fonts" / first.faces.front().filename));
    CHECK(fs::exists(dir.path / "fonts" / second.faces.front().filename));
    CHECK(fs::exists(dir.path / "fonts" / kept.faces.front().filename));
    CHECK(FontRepository(db).list_all().size() == 2);
}

TEST_CASE("A face or a family comes out with every file in its slots", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    FontRepository rows(db);
    const auto folder = dir.path / "fonts";
    const auto regular = store.import_file(fixture_font("SSFixtureSans-Regular.ttf"));
    const auto bold = store.import_file(fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(regular.error.empty());
    REQUIRE(bold.error.empty());
    // Two computers adding the Regular apart hold both once synced.
    auto older = regular.faces.front();
    older.filename = std::string(64, 'a') + ".ttf";
    older.added_at -= 1000;
    rows.add(older);
    std::ofstream(folder / older.filename) << "older";

    REQUIRE(store.library_faces().size() == 2);
    CHECK(store.remove_faces({regular.faces.front().filename}).empty());
    // Not the older one showing in its place.
    CHECK(names_of(rows.list_all()) == std::vector{bold.faces.front().filename});
    CHECK_FALSE(fs::exists(folder / older.filename));
    CHECK_FALSE(fs::exists(folder / regular.faces.front().filename));
    CHECK(store.remove_faces({"not-in-the-library.ttf"}).empty());

    CHECK(store.remove_family("ssfixture SANS").empty());
    CHECK(rows.list_all().empty());
    CHECK_FALSE(fs::exists(folder / bold.faces.front().filename));
}

TEST_CASE("A collection comes in as a file per face the font engine opens", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto added = store.import_file(fixture_font("SSFixtureCollection.ttc"));
    CHECK(added.error.empty());
    REQUIRE(added.faces.size() == 3);
    for (const auto& face : added.faces) {
        INFO(face.display_name);
        const QRawFont raw(QString::fromStdString((dir.path / "fonts" / face.filename).string()), 12.0);
        CHECK(raw.isValid());
        CHECK(raw.familyName().startsWith(QStringLiteral("SSFixture Collection")));
    }
}

TEST_CASE("A font refused leaves nothing behind", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto text = dir.path / "not-a-font.ttf";
    std::ofstream(text) << "Dear friends";
    CHECK(store.import_file(text).error == "This is not a font file.");
    CHECK(store.import_file(dir.path / "not-there.ttf").error == "This font file could not be read.");
    CHECK(FontRepository(db).list_all().empty());
    CHECK(files_in(dir.path / "fonts").empty());

    // The database refusing it takes back the file just written.
    db.exec("DROP TABLE font_files");
    const auto failed = store.import_file(fixture_font("SSFixtureSans-Regular.ttf"));
    CHECK_FALSE(failed.error.empty());
    CHECK(failed.faces.empty());
    CHECK(files_in(dir.path / "fonts").empty());
}

TEST_CASE("A library takes a copy of what is shipped once, and its own stay", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    FontRepository fonts(db);

    // The library's own font of a shipped name is not touched.
    fonts.add(row("mine.ttf", "Liberation Serif", false, 1));

    const auto added = store.take_bundled();
    CHECK(added.error.empty());
    CHECK(added.replaced == 0);
    CHECK(names_of(fonts.list_family("Liberation Sans")).size() == 4);
    CHECK(files_in(dir.path / "fonts").size() == added.faces.size());
    CHECK(fonts.list_family("Liberation Serif").size() == 1);
    const auto faces = fonts.list_faces();
    CHECK(std::ranges::any_of(faces, [](const auto& f) { return f.bold && f.italic; }));

    // A second time, and after a family has been taken out, nothing comes back.
    const auto before = fonts.list_all();
    CHECK(store.take_bundled().faces.empty());
    CHECK(fonts.list_all() == before);
    CHECK(store.remove_family("Carlito").empty());
    const auto again = store.take_bundled();
    CHECK(again.error.empty());
    CHECK(again.faces.empty());
    CHECK(fonts.list_family("Carlito").empty());
}

TEST_CASE("A library that could not take its copy tries again", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontRepository fonts(db);
    // Its folder's name is taken by a file.
    { std::ofstream(dir.path / "fonts") << "in the way"; }
    FontStore store(db, dir.path);
    CHECK_FALSE(store.take_bundled().error.empty());
    CHECK(fonts.list_all().empty());
    CHECK_FALSE(fonts.bundled_taken());

    std::filesystem::remove(dir.path / "fonts");
    CHECK(store.take_bundled().error.empty());
    CHECK_FALSE(fonts.list_all().empty());
    CHECK(fonts.bundled_taken());
}

TEST_CASE("Every shipped font is what its folder says, and a library takes it", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto families = FontStore::bundled_families();
    for (const auto* family :
         {"Liberation Sans", "Liberation Serif", "Liberation Mono", "Carlito", "Caladea",
          "Gelasio", "Open Sans", "Open Sans Light", "Open Sans SemiBold", "Montserrat",
          "Montserrat Light", "Montserrat Medium", "Roboto", "EB Garamond", "Comic Neue",
          "Dancing Script", "Oswald"}) {
        INFO(family);
        CHECK(std::ranges::find(families, family) != families.end());
    }
    REQUIRE(store.take_bundled().error.empty());
    FontRepository fonts(db);
    for (const auto& family : families) {
        INFO(family);
        CHECK(QFile::exists(QStringLiteral(":/bundled_fonts/%1/OFL.txt")
                                .arg(QString::fromStdString(family))));
        const auto faces = fonts.list_family(family);
        REQUIRE_FALSE(faces.empty());
        // Regular is there, and nothing here is a variable font, whose other
        // weights the slots would not know of.
        CHECK(std::ranges::any_of(faces, [](const auto& f) { return !f.bold && !f.italic; }));
        for (const auto& face : faces) {
            INFO(face.display_name);
            CHECK(face.family == family);
            CHECK_FALSE(face.variable);
            CHECK(QRawFont(QString::fromStdString((dir.path / "fonts" / face.filename).string()), 12.0)
                      .isValid());
        }
    }
}

TEST_CASE("EB Garamond ships as EB Garamond 08's Regular and Italic, by the folder's name",
          "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    // The files call themselves "EB Garamond 08". These bytes are what a
    // library that added them itself already holds, so the two are one record.
    const auto shipped = FontStore::shipped_index();
    const auto* regular = seventhslide::fonts::index_match(shipped, "EB Garamond", false, false);
    REQUIRE(regular != nullptr);
    CHECK(regular->path.string() == ":/bundled_fonts/EB Garamond/EBGaramond08-Regular.otf");

    REQUIRE(store.take_bundled().error.empty());
    FontRepository fonts(db);
    CHECK(fonts.list_family("EB Garamond 08").empty());
    std::map<std::pair<bool, bool>, std::string> faces_by_slot;
    for (const auto& face : fonts.list_family("EB Garamond")) {
        CHECK(face.family == "EB Garamond");
        CHECK(face.display_name == "EB Garamond " + face.style);
        faces_by_slot[{face.bold, face.italic}] = face.filename;
    }
    REQUIRE(faces_by_slot.size() == 4);
    CHECK(faces_by_slot[{false, false}]
          == "66b2f6a92bc84e53c0701e9363e722f8e347ca15d25319bd8008ab2618fe3299.otf");
    CHECK(faces_by_slot[{false, true}]
          == "da569801af7b6cd6dde1f04bdc5616d6a01f821805d05473ec4f03429b172f60.otf");
    // It has no bold: those are the newer design's.
    CHECK(faces_by_slot[{true, false}].ends_with(".ttf"));
    CHECK(faces_by_slot[{true, true}].ends_with(".ttf"));
}

TEST_CASE("A font's name from a row never reaches outside the fonts folder", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    const auto outside = dir.path / "outside.txt";
    std::ofstream(outside) << "keep me";
    FontRepository fonts(db);

    // Asked to directly.
    fonts.add(row("../outside.txt", "Bad", false, 1));
    CHECK(store.remove("../outside.txt").empty());
    CHECK(fs::exists(outside));
    CHECK(fonts.list_all().empty());
    CHECK(store.remove(outside.string()).empty());
    CHECK(fs::exists(outside));

    // A row sync brought, replaced by a font added here.
    fonts.add(row("../outside.txt", "SSFixture Sans", false, 1));
    const auto added = store.import_file(fixture_font("SSFixtureSans-Regular.ttf"));
    CHECK(added.error.empty());
    CHECK(added.replaced == 1);
    CHECK(fs::exists(outside));
}

TEST_CASE("What is shipped is known by its folders and file names alone", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const auto index = FontStore::shipped_index();
    CHECK(index.size() == FontStore::bundled_families().size());
    const auto* bold_italic = seventhslide::fonts::index_match(index, "Open Sans", true, true);
    REQUIRE(bold_italic != nullptr);
    CHECK(bold_italic->path.string() == ":/bundled_fonts/Open Sans/OpenSans-BoldItalic.ttf");
    CHECK(QFile::exists(QString::fromStdString(bold_italic->path.string())));
    const auto* regular = seventhslide::fonts::index_match(index, "open sans", false, false);
    REQUIRE(regular != nullptr);
    CHECK(regular->subfamily == "Regular");
}

TEST_CASE("A library's faces are what text can show, and no name reaches outside", "[fonts][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    auto db = library();
    FontStore store(db, dir.path);
    FontRepository fonts(db);
    fonts.add(row("older.ttf", "Lato", false, 1));
    fonts.add(row("newer.ttf", "Lato", false, 2));
    fonts.add(row("bold.ttf", "Lato", true, 1));
    fonts.add(row("../outside.ttf", "Bad", false, 1));
    const auto index = store.library_index();
    REQUIRE(index.contains("lato"));
    CHECK(index.at("lato").size() == 2);
    CHECK(seventhslide::fonts::index_match(index, "Lato", false, false)->path
          == dir.path / "fonts" / "newer.ttf");
    CHECK_FALSE(index.contains("bad"));
    // Nothing could draw it, so it is not offered either.
    CHECK(store.library_families() == std::vector<std::string>{"Lato"});
}
