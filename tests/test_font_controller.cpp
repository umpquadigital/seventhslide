// A session's fonts: what slides are drawn from, a library's copy of the shipped
// ones, what is missing said, and nothing moving under what is live.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "font_controller.hpp"
#include "font_fixtures.hpp"
#include "font_store.hpp"
#include "media_controller.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/layout/qt_font_faces.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QRawFont>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using seventhslide::app::FontController;
using seventhslide::app::FontStore;
using seventhslide::app::SessionFacade;
using seventhslide::db::FontRepository;
using seventhslide::layout::FontStyle;
using seventhslide::test::wait_until;

namespace {

void settle()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCoreApplication::sendPostedEvents(nullptr, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

/// The files in ``folder``.
std::size_t files_in(const fs::path& folder)
{
    std::error_code ec;
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        count += entry.is_regular_file() ? 1 : 0;
    }
    return count;
}

/// A session over a library of its own on disk, with its copy of the shipped
/// fonts.
struct Library {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    SessionFacade facade;

    /// The shipped fonts' files, the library's from when it first opened.
    std::size_t shipped_files = 0;

    Library()
    {
        REQUIRE(facade.init_libraries(dir.path / "data"));
        wait_for_shipped();
        shipped_files = files_in(font_folder());
    }

    /// Until the library has had its copy of the shipped fonts.
    void wait_for_shipped()
    {
        REQUIRE(QTest::qWaitFor([this] { return rows().bundled_taken(); }, 20000));
        settle();
    }
    /// The library as one from before the shipped fonts were copied in, with
    /// only what it had used: no Bitter.
    void as_before_shipped_copy()
    {
        seventhslide::test::forget_bundled_taken(*facade.open_library().database());
        CHECK(fonts().removeFamily(QStringLiteral("Bitter")).isEmpty());
        settle();
    }

    FontController& fonts() { return *facade.fonts(); }
    FontRepository rows() { return FontRepository(*facade.open_library().database()); }
    FontStore store() { return {*facade.open_library().database(), facade.media_root()}; }
    fs::path font_folder() { return facade.media_root() / "fonts"; }
    /// The files in the library's fonts folder beyond the shipped fonts.
    std::size_t files_held() { return files_in(font_folder()) - shipped_files; }
    auto resolved(const std::string& family)
    {
        return facade.previews().measurer().faces().resolve(
            FontStyle{.family = family, .size_px = 40.0});
    }
};

QString fixture_path(const char* file)
{
    return QString::fromStdString(seventhslide::test::fixture_font(file).string());
}

/// The row of ``rows`` for ``family``.
QVariantMap row_of(const FontController& fonts, const QString& family)
{
    for (const auto& row : fonts.rows()) {
        if (row.toMap().value(QStringLiteral("name")).toString() == family) {
            return row.toMap();
        }
    }
    return {};
}

}  // namespace

TEST_CASE("A library starts with every shipped family, and its own go beside them", "[fonts][app]")
{
    Library library;
    auto families = library.fonts().families();
    CHECK(families.contains(QStringLiteral("Open Sans")));
    CHECK(families.contains(QStringLiteral("Liberation Sans")));
    CHECK_FALSE(families.contains(QStringLiteral("SSFixture Sans")));
    CHECK(library.rows().list_family("Open Sans").size() == 4);
    const auto rows = library.fonts().rows();
    CHECK(std::ranges::all_of(rows, [](const QVariant& row) {
        return row.toMap().value(QStringLiteral("state")).toString() == QStringLiteral("library");
    }));

    QSignalSpy changed(&library.fonts(), &FontController::fontsChanged);
    auto store = library.store();
    REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))
                .error.empty());
    library.fonts().library_fonts_changed();
    CHECK(changed.count() >= 1);
    CHECK(library.fonts().families().contains(QStringLiteral("SSFixture Sans")));
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
    CHECK(library.fonts().missing().isEmpty());
}

TEST_CASE("A shipped family taken out of a library stays out", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    CHECK(library.resolved("Merriweather").font.family() == QStringLiteral("Merriweather"));
    CHECK(fonts.removeFamily(QStringLiteral("Merriweather")).isEmpty());
    CHECK(library.rows().list_family("Merriweather").empty());
    CHECK_FALSE(fonts.families().contains(QStringLiteral("Merriweather")));

    // It is drawn in the stand-in and said to be missing, as any other would be.
    const auto face = library.resolved("Merriweather");
    CHECK(face.font.family() == QStringLiteral("Liberation Sans"));
    settle();
    CHECK(fonts.missing() == QStringList{QStringLiteral("Merriweather")});

    // Opening the library again does not bring it back.
    fonts.library_opened();
    settle();
    CHECK(library.rows().list_family("Merriweather").empty());
}

TEST_CASE("A family drawn before the library took its fonts is not left missing", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    library.as_before_shipped_copy();

    // Opening starts the copy, and the draw that follows is told on after.
    fonts.library_opened();
    CHECK(library.resolved("Bitter").font.family() == QStringLiteral("Liberation Sans"));
    library.wait_for_shipped();
    CHECK(library.rows().list_family("Bitter").size() == 4);
    CHECK(fonts.missing().isEmpty());
}

TEST_CASE("A library that had no copy of the shipped fonts takes one when it opens", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    library.as_before_shipped_copy();
    QSignalSpy reloaded(&fonts, &FontController::fontsReloaded);

    fonts.library_opened();
    library.wait_for_shipped();
    CHECK(library.rows().list_family("Bitter").size() == 4);
    CHECK(fonts.families().contains(QStringLiteral("Bitter")));
    CHECK(reloaded.count() == 1);
    CHECK(library.resolved("Bitter").font.family() == QStringLiteral("Bitter"));
}

TEST_CASE("A family nobody has shows the stand-in and is named", "[fonts][app]")
{
    Library library;
    QSignalSpy missing(&library.fonts(), &FontController::missingChanged);
    const auto face = library.resolved("Definitely Not A Font");
    CHECK(face.resolved);
    CHECK(face.font.family() == QStringLiteral("Liberation Sans"));
    settle();
    CHECK(library.fonts().missing() == QStringList{QStringLiteral("Definitely Not A Font")});
    CHECK(missing.count() >= 1);
    // Views draw it the same, and asking again names it once.
    CHECK(library.fonts().painted(QStringLiteral("Definitely Not A Font"))
          == QStringLiteral("Liberation Sans"));
    settle();
    CHECK(library.fonts().missing().size() == 1);
}

TEST_CASE("A library's damaged font shows the stand-in rather than reach the font engine",
          "[fonts][app]")
{
    Library library;
    auto store = library.store();
    const auto added = store.import_file(seventhslide::test::fixture_font("SSFixturePlain-Regular.ttf"));
    REQUIRE(added.faces.size() == 1);
    library.fonts().library_fonts_changed();
    // As a sync that went wrong, or a disk, might leave it.
    const auto file = library.facade.media_root() / "fonts" / added.faces.front().filename;
    fs::resize_file(file, 40);
    library.facade.previews().measurer().clear_cache();
    const auto face = library.resolved("SSFixture Plain");
    CHECK(face.resolved);
    CHECK(face.font.family() == QStringLiteral("Liberation Sans"));
    settle();
    CHECK(library.fonts().missing() == QStringList{QStringLiteral("SSFixture Plain")});
}

TEST_CASE("Fonts changing wait until nothing is live", "[fonts][app]")
{
    Library library;
    bool live = true;
    library.fonts().set_may_reload([&] { return !live; });
    QSignalSpy reloaded(&library.fonts(), &FontController::fontsReloaded);

    auto store = library.store();
    REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))
                .error.empty());
    library.fonts().library_fonts_changed();
    CHECK(reloaded.count() == 0);
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("Liberation Sans"));

    live = false;
    library.fonts().retry_waiting();
    CHECK(reloaded.count() == 1);
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));

    // Nothing new: nothing laid out again.
    library.fonts().library_fonts_changed();
    CHECK(reloaded.count() == 1);
}

TEST_CASE("A viewer's library is drawn from but not written to", "[fonts][app]")
{
    Library library;
    library.as_before_shipped_copy();

    library.facade.set_read_only(true);
    library.fonts().library_opened();
    settle();
    library.facade.set_read_only(false);
    CHECK(library.rows().list_family("Bitter").empty());
    CHECK_FALSE(library.rows().bundled_taken());
    CHECK(library.resolved("Bitter").font.family() == QStringLiteral("Liberation Sans"));
}

TEST_CASE("Another library's fonts do not come along", "[fonts][app]")
{
    Library library;
    auto store = library.store();
    REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))
                .error.empty());
    library.fonts().library_fonts_changed();
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));

    library.facade.fonts()->library_closed();
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("Liberation Sans"));
}

TEST_CASE("A shipped family's faces are the library's own, each with its file", "[fonts][app]")
{
    Library library;
    const auto faces = library.fonts().faces(QStringLiteral("Merriweather"));
    REQUIRE(faces.size() == 4);
    const auto first = faces.front().toMap();
    CHECK(first.value(QStringLiteral("style")).toString() == QStringLiteral("Regular"));
    CHECK(first.value(QStringLiteral("loaded")).toBool());
    CHECK(first.value(QStringLiteral("qtFamily")).toString() == QStringLiteral("Merriweather"));
    CHECK_FALSE(first.value(QStringLiteral("filename")).toString().isEmpty());
    const auto bold_italic = faces.back().toMap();
    CHECK(bold_italic.value(QStringLiteral("style")).toString() == QStringLiteral("Bold Italic"));
    CHECK(bold_italic.value(QStringLiteral("italic")).toBool());
}

TEST_CASE("A library's face is shown once it has passed the checks", "[fonts][app]")
{
    Library library;
    auto store = library.store();
    const auto added = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(added.error.empty());
    library.fonts().library_fonts_changed();
    auto faces = library.fonts().faces(QStringLiteral("ssfixture sans"));
    REQUIRE(faces.size() == 1);
    CHECK(faces.front().toMap().value(QStringLiteral("loaded")).toBool());
    CHECK(faces.front().toMap().value(QStringLiteral("filename")).toString().toStdString()
          == added.faces.front().filename);
    library.fonts().releasePreviews();

    // A file damaged since says so, and can still be removed.
    const auto file = library.font_folder() / added.faces.front().filename;
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "not a font";
    faces = library.fonts().faces(QStringLiteral("SSFixture Sans"));
    REQUIRE(faces.size() == 1);
    CHECK_FALSE(faces.front().toMap().value(QStringLiteral("loaded")).toBool());
    library.fonts().releasePreviews();
}

TEST_CASE("Files added are checked on a worker, and each says what it came to", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    const seventhslide::test::TempDir files;
    const auto junk = files.path / "junk.ttf";
    std::ofstream(junk, std::ios::binary) << "not a font";

    QSignalSpy finished(&fonts, &FontController::addFinished);
    // One as a file dialog gives it: a URL, file:///C:/... on Windows.
    fonts.addFiles({QUrl::fromLocalFile(fixture_path("SSFixtureSans-Regular.ttf")).toString(),
                    QString::fromStdString(junk.string()),
                    QString::fromStdString((files.path / "gone.ttf").string()),
                    fixture_path("SSFixtureCollection.ttc")});
    CHECK(fonts.adding());
    REQUIRE(finished.wait(10000));
    CHECK_FALSE(fonts.adding());

    const auto results = finished.front().front().toList();
    REQUIRE(results.size() == 4);
    const auto regular = results[0].toMap();
    CHECK(regular.value(QStringLiteral("file")).toString() == QStringLiteral("SSFixtureSans-Regular.ttf"));
    CHECK(regular.value(QStringLiteral("ok")).toBool());
    CHECK(regular.value(QStringLiteral("text")).toString()
          == QStringLiteral("Added SSFixture Sans (Regular)."));
    CHECK_FALSE(results[1].toMap().value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(results[1].toMap().value(QStringLiteral("text")).toString().isEmpty());
    CHECK_FALSE(results[2].toMap().value(QStringLiteral("ok")).toBool());
    CHECK(results[3].toMap().value(QStringLiteral("ok")).toBool());

    // The same bytes again come to nothing new.
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
    finished.clear();
    fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
    REQUIRE(finished.wait(10000));
    CHECK(finished.front().front().toList().front().toMap().value(QStringLiteral("text")).toString()
          == QStringLiteral("Already in this library."));
}

TEST_CASE("A sync's size limit is said about files added", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    QStringList titles;
    std::int64_t bytes = 0;
    fonts.set_import_note([&](const QStringList& t, std::int64_t b) {
        titles = t;
        bytes = b;
        return QStringLiteral("over the limit");
    });
    QSignalSpy finished(&fonts, &FontController::addFinished);
    fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
    REQUIRE(finished.wait(10000));
    CHECK(finished.front().at(1).toString() == QStringLiteral("over the limit"));
    CHECK(titles == QStringList{QStringLiteral("SSFixtureSans-Regular.ttf")});
    CHECK(bytes == static_cast<std::int64_t>(
                       fs::file_size(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))));
    fonts.set_import_note({});
}

TEST_CASE("Files that finish while something is live wait for it to stop", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    bool live = false;
    // Asked while live: the files are checked, and wait.
    bool asked_live = false;
    fonts.set_may_reload([&] {
        asked_live = asked_live || live;
        return !live;
    });
    QSignalSpy finished(&fonts, &FontController::addFinished);
    fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
    live = true;
    REQUIRE(wait_until([&] { return asked_live; }));
    CHECK(finished.isEmpty());
    CHECK(fonts.adding());
    CHECK(library.rows().list_family("SSFixture Sans").empty());
    live = false;
    fonts.retry_waiting();
    CHECK(finished.size() == 1);
    CHECK_FALSE(fonts.adding());
    CHECK(library.rows().list_family("SSFixture Sans").size() == 1);
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
    fonts.set_may_reload({});
}

TEST_CASE("Files on their way to a library that has closed are taken back", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    SECTION("still being checked")
    {
        QSignalSpy finished(&fonts, &FontController::addFinished);
        fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
        fonts.library_opened();
        REQUIRE(finished.wait(10000));
        CHECK(finished.front().front().toList().front().toMap().value(QStringLiteral("text")).toString()
              == QStringLiteral("The library was closed."));
    }
    SECTION("waiting for something live")
    {
        bool live = false;
        // Asked while live: the files are checked, and wait.
        bool asked_live = false;
        fonts.set_may_reload([&] {
            asked_live = asked_live || live;
            return !live;
        });
        fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
        live = true;
        REQUIRE(wait_until([&] { return asked_live; }));
        CHECK(library.files_held() == 1);
        fonts.library_opened();
        fonts.set_may_reload({});
    }
    CHECK_FALSE(fonts.adding());
    CHECK(library.files_held() == 0);
    CHECK(library.rows().list_family("SSFixture Sans").empty());
}

TEST_CASE("Fonts come out of a library by the face and by the family", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto store = library.store();
    const auto regular = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    const auto bold = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(regular.error.empty());
    REQUIRE(bold.error.empty());
    fonts.library_fonts_changed();
    CHECK(row_of(fonts, QStringLiteral("SSFixture Sans")).value(QStringLiteral("faces")).toInt() == 2);
    CHECK(row_of(fonts, QStringLiteral("SSFixture Sans")).value(QStringLiteral("state")).toString()
          == QStringLiteral("library"));

    QSignalSpy reloaded(&fonts, &FontController::fontsReloaded);
    CHECK(fonts.removeFaces({QString::fromStdString(bold.faces.front().filename)}).isEmpty());
    CHECK(reloaded.count() == 1);
    CHECK(library.rows().list_family("SSFixture Sans").size() == 1);
    CHECK(library.files_held() == 1);

    CHECK(fonts.removeFamily(QStringLiteral("ssfixture sans")).isEmpty());
    CHECK(reloaded.count() == 2);
    CHECK(library.rows().list_family("SSFixture Sans").empty());
    CHECK(library.files_held() == 0);
    CHECK(row_of(fonts, QStringLiteral("SSFixture Sans")).isEmpty());
}

TEST_CASE("Nothing is added or removed while something is live, or in a viewer's library",
          "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto store = library.store();
    REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf")).error.empty());
    fonts.library_fonts_changed();

    bool live = false;
    bool writes = true;
    fonts.set_may_reload([&live] { return !live; });
    fonts.set_may_write([&writes] { return writes; });
    const auto refused = [&] {
        CHECK_FALSE(fonts.removeFamily(QStringLiteral("SSFixture Sans")).isEmpty());
        QSignalSpy finished(&fonts, &FontController::addFinished);
        fonts.addFiles({fixture_path("SSFixtureSans-Bold.ttf")});
        CHECK_FALSE(fonts.adding());
        REQUIRE(finished.size() == 1);
        CHECK_FALSE(finished.front().front().toList().front().toMap().value(QStringLiteral("ok")).toBool());
        CHECK(library.rows().list_family("SSFixture Sans").size() == 1);
    };
    SECTION("live")
    {
        QSignalSpy changed(&fonts, &FontController::liveChanged);
        live = true;
        fonts.retry_waiting();
        CHECK(fonts.live());
        CHECK(changed.count() == 1);
        refused();
        live = false;
        fonts.retry_waiting();
        CHECK(changed.count() == 2);
    }
    SECTION("a viewer's")
    {
        writes = false;
        refused();
    }
    fonts.set_may_reload({});
    fonts.set_may_write({});
}

TEST_CASE("A weight shipped as a family of its own is drawn from its own file", "[fonts][app]")
{
    Library library;
    const auto text = QStringLiteral("Hamburgefonstiv");
    const auto width = [&](const QFont& font) { return QFontMetricsF(font).horizontalAdvance(text); };
    for (const auto& [family, base, style] :
         {std::tuple{"Open Sans Light", "Open Sans", "Light"},
          std::tuple{"Open Sans SemiBold", "Open Sans", "SemiBold"},
          std::tuple{"Montserrat Light", "Montserrat", "Light"},
          std::tuple{"Montserrat Medium", "Montserrat", "Medium"}}) {
        INFO(family);
        const auto regular = library.resolved(base);
        const auto own = library.resolved(family);
        CHECK(own.resolved);
        CHECK_FALSE(own.needs_synthetic_embolden());
        // Its own file, which Qt files under the family it shares and its style.
        CHECK(QFontInfo(own.font).styleName() == QString::fromLatin1(style));
        const auto match = library.facade.previews().fonts().find(family, false, false);
        REQUIRE(match);
        const QRawFont file(QString::fromStdString(match->path.string()), 40.0);
        REQUIRE(file.isValid());
        qreal file_w = 0;
        for (const auto& advance : file.advancesForGlyphIndexes(file.glyphIndexesForString(text))) {
            file_w += advance.x();
        }
        CHECK(width(own.font) == Catch::Approx(file_w).epsilon(0.01));
        CHECK(width(own.font) != Catch::Approx(width(regular.font)).epsilon(0.005));
    }
}

TEST_CASE("Files that finish after a library turned view only are taken back", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    bool live = false;
    bool writes = true;
    // Asked while live: the files are checked, and wait.
    bool asked_live = false;
    fonts.set_may_reload([&] {
        asked_live = asked_live || live;
        return !live;
    });
    fonts.set_may_write([&writes] { return writes; });
    QSignalSpy finished(&fonts, &FontController::addFinished);
    fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
    live = true;
    REQUIRE(wait_until([&] { return asked_live; }));
    REQUIRE(fonts.adding());
    REQUIRE(library.files_held() == 1);
    writes = false;
    live = false;
    fonts.retry_waiting();
    REQUIRE(finished.size() == 1);
    const auto result = finished.front().front().toList().front().toMap();
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("text")).toString() == QStringLiteral("This library is view only here."));
    CHECK_FALSE(fonts.adding());
    CHECK(library.rows().list_family("SSFixture Sans").empty());
    CHECK(library.files_held() == 0);
    fonts.set_may_reload({});
    fonts.set_may_write({});
}

TEST_CASE("Files waiting for nothing to be live go when the program does", "[fonts][app]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    fs::path folder;
    std::size_t shipped = 0;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(dir.path / "data"));
        REQUIRE(QTest::qWaitFor(
            [&] { return FontRepository(*facade.open_library().database()).bundled_taken(); }, 20000));
        settle();
        folder = facade.media_root() / "fonts";
        shipped = files_in(folder);
        auto& fonts = *facade.fonts();
        bool live = false;
        // Asked while live: the files are checked, and wait.
        bool asked_live = false;
        fonts.set_may_reload([&] {
            asked_live = asked_live || live;
            return !live;
        });
        fonts.addFiles({fixture_path("SSFixtureSans-Regular.ttf")});
        live = true;
        REQUIRE(wait_until([&] { return asked_live; }));
        REQUIRE(fonts.adding());
        REQUIRE(files_in(folder) == shipped + 1);
        fonts.set_may_reload({});
    }
    CHECK(files_in(folder) == shipped);
}

TEST_CASE("A face the page could not show is shown once its file is here", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto store = library.store();
    const auto added = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(added.error.empty());
    fonts.library_fonts_changed();
    const auto file = library.font_folder() / added.faces.front().filename;
    const auto bytes = library.dir.path / "held";
    fs::rename(file, bytes);

    const auto shown = [&] {
        return fonts.faces(QStringLiteral("SSFixture Sans")).front().toMap().value(QStringLiteral("loaded")).toBool();
    };
    CHECK_FALSE(shown());
    QSignalSpy changed(&fonts, &FontController::fontsChanged);
    fonts.files_may_have_arrived();
    CHECK(changed.isEmpty());

    fs::rename(bytes, file);
    const auto revision = fonts.revision();
    fonts.files_may_have_arrived();
    CHECK(changed.count() == 1);
    CHECK(fonts.revision() == revision + 1);
    CHECK(shown());
    fonts.files_may_have_arrived();
    CHECK(changed.count() == 1);
    fonts.releasePreviews();
}

TEST_CASE("What a library names and has no font for is missing before anything draws it",
          "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto& db = *library.facade.open_library().database();
    db.exec(R"(INSERT INTO freeform_items (name, document) VALUES
        ('one', '{"objects":[{"text":{"font_family":"Gotham"}},{"font_family":"carlito"}]}'),
        ('two', '{"objects":[{"html":"a <font=\"Tahoma\">b</font> <font=Calibri>c</font>"}]}'),
        ('damaged', '{"font_family":"Unreadable"'))");
    db.exec(R"(INSERT INTO style_profiles (name, data) VALUES
        ('p', '{"bible":{"verse_font_family":"Rightside","font_family":""}}'))");
    db.exec(R"(INSERT INTO songs (title, lyrics) VALUES
        ('s', '<font=Freckle Face>Amazing</font> grace'))");

    const auto named = FontRepository(db).families_named();
    for (const char* family : {"Gotham", "carlito", "Tahoma", "Calibri", "Rightside", "Freckle Face"}) {
        CAPTURE(family);
        CHECK(named.contains(family));
    }
    CHECK_FALSE(named.contains("Unreadable"));
    CHECK_FALSE(named.contains(""));

    SECTION("a tag counts just when a slide would read it as one")
    {
        db.exec(R"(INSERT INTO songs (title, lyrics) VALUES
            ('t', '<FONT=Upper>a</FONT> <font=''Single''>b</font> <font= Spaced >c</font> <font=>d'),
            ('u', '<font=Has&Amp>e</font> <font=No Close'))");
        const auto tags = FontRepository(db).families_named();
        for (const char* family : {"Upper", "Single", "Spaced"}) {
            CAPTURE(family);
            CHECK(tags.contains(family));
        }
        for (const char* family : {"'Single'", " Spaced ", "Has&Amp", "No Close"}) {
            CAPTURE(family);
            CHECK_FALSE(tags.contains(family));
        }
        db.exec("DELETE FROM songs WHERE title IN ('t', 'u')");
    }

    QSignalSpy missing(&fonts, &FontController::missingChanged);
    fonts.refresh();
    CHECK(missing.count() == 1);
    const QStringList expected{QStringLiteral("Calibri"), QStringLiteral("Freckle Face"),
                               QStringLiteral("Gotham"), QStringLiteral("Rightside"),
                               QStringLiteral("Tahoma")};
    CHECK(fonts.missing() == expected);

    // The missing lead, and what is named says so.
    const auto rows = fonts.rows();
    REQUIRE(rows.size() > expected.size());
    CHECK(rows.front().toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Calibri"));
    CHECK(rows.front().toMap().value(QStringLiteral("state")).toString() == QStringLiteral("missing"));
    CHECK(rows.front().toMap().value(QStringLiteral("named")).toBool());
    CHECK(row_of(fonts, QStringLiteral("Carlito")).value(QStringLiteral("named")).toBool());
    CHECK_FALSE(row_of(fonts, QStringLiteral("Lobster")).value(QStringLiteral("named")).toBool());

    // A theme off the font, and it is no longer missing once read again.
    db.exec("DELETE FROM songs");
    fonts.refresh();
    CHECK_FALSE(fonts.missing().contains(QStringLiteral("Freckle Face")));
}

TEST_CASE("What names a missing font is found by what the library calls it", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto& db = *library.facade.open_library().database();
    // An output's themes each on their own, then the output for what it names
    // outside them.
    db.exec(R"(INSERT INTO outputs (order_num, data) VALUES
        (90, '{"id":"out-stage","name":"Stage","category_defaults":{"font_family":"Gotham"},
               "song_themes":[{"id":"t1","name":"Lyrics","style":{"font_family":"gotham"}},
                              {"id":"t2","name":"Plain","style":{"font_family":"Tahoma"}}],
               "bible_themes":[{"id":"v1","name":"Passage","style":{"font_family":"Gotham"}}],
               "media_themes":[{"id":"m1","name":"Captions","style":{"font_family":"Tahoma"}}],
               "bg_themes":[{"id":"b1","name":"Sky","style":{"objects":[{"font_family":"Gotham"}]}}]}'))");
    db.exec(R"(INSERT INTO style_profiles (id, name, data) VALUES
        (40, 'Evening', '{"bible":{"verse_font_family":"Gotham"}}'),
        (41, 'Plain', '{"bible":{"font_family":"Tahoma"}}'))");
    db.exec(R"(INSERT INTO songs (id, title, lyrics) VALUES
        (50, 'Amazing Grace', '<font=Gotham>Amazing</font> <font=GOTHAM>grace</font>'),
        (51, 'Other', '<font=Tahoma>words</font>'))");
    db.exec(R"(INSERT INTO freeform_items (id, name, document) VALUES
        (60, '<b>Welcome</b> ', '{"objects":[{"font_family":"Gotham"}]}'),
        (61, 'damaged', '{"font_family":"Gotham"'))");
    db.exec("INSERT INTO services (id, name) VALUES (7, 'Easter')");
    db.exec(R"(INSERT INTO service_items (id, service_id, order_num, item_type, data) VALUES
        (70, 7, 1, 'song', '{"title":"Amazing Grace","lyrics":"<font=Gotham>Amazing</font>"}'),
        (71, 7, 2, 'song', '{"title":"Other","lyrics":"<font=Tahoma>words</font>"}'),
        (72, 7, 3, 'freeform', '{"name":"Welcome","document":{"objects":[{"font_family":"Gotham"}]}}'))");

    using Kind = seventhslide::db::FontUse::Kind;
    const std::vector<seventhslide::db::FontUse> expected{
        {Kind::Theme, "Lyrics", "Stage", "t1", "song"},
        {Kind::Theme, "Passage", "Stage", "v1", "bible"},
        {Kind::Theme, "Sky", "Stage", "b1", "bg"},
        {Kind::Output, "Stage", "", "out-stage", ""},
        {Kind::Profile, "Evening", "", "40", ""},
        {Kind::Song, "Amazing Grace", "", "50", ""},
        {Kind::Freeform, "Welcome", "", "60", ""},
        {Kind::ServiceItem, "Amazing Grace", "Easter", "70", "song"},
        {Kind::ServiceItem, "Welcome", "Easter", "72", "freeform"},
    };
    // Case aside, each once, a damaged document passed over.
    CHECK(FontRepository(db).uses_of("Gotham") == expected);
    CHECK(FontRepository(db).uses_of("Nothing").empty());

    // As the page reads it, with what it takes to open each.
    const auto uses = fonts.uses(QStringLiteral("GOTHAM"));
    REQUIRE(uses.size() == static_cast<qsizetype>(expected.size()));
    const auto field = [&](qsizetype at, const char* key) {
        return uses.at(at).toMap().value(QString::fromLatin1(key)).toString();
    };
    CHECK(field(0, "kind") == QStringLiteral("theme"));
    CHECK(field(0, "within") == QStringLiteral("Stage"));
    CHECK(field(0, "id") == QStringLiteral("t1"));
    // The kind the theme designer opens it as.
    CHECK(field(0, "type") == QStringLiteral("song"));
    CHECK(field(1, "type") == QStringLiteral("bible"));
    CHECK(field(3, "kind") == QStringLiteral("output"));
    CHECK(field(4, "kind") == QStringLiteral("profile"));
    CHECK(field(7, "kind") == QStringLiteral("service"));
    CHECK(field(7, "id") == QStringLiteral("70"));
    CHECK(field(7, "within") == QStringLiteral("Easter"));
}

TEST_CASE("A library's fonts leave Qt with it", "[fonts][app]")
{
    {
        Library library;
        auto store = library.store();
        REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))
                    .error.empty());
        library.fonts().library_fonts_changed();
        CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
        CHECK(QFontDatabase::families().contains(QStringLiteral("SSFixture Sans")));
    }
    CHECK_FALSE(QFontDatabase::families().contains(QStringLiteral("SSFixture Sans")));
}

TEST_CASE("A font's file coming after its row is drawn once it is here", "[fonts][app]")
{
    Library library;
    auto& fonts = library.fonts();
    auto store = library.store();
    const auto source = seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf");
    const auto added = store.import_file(source);
    REQUIRE(added.error.empty());
    const auto file = library.font_folder() / added.faces.front().filename;
    fs::remove(file);
    fonts.library_fonts_changed();

    // The row is here and the file is not: the stand-in, and it says so.
    CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("Liberation Sans"));
    settle();
    CHECK(fonts.missing() == QStringList{QStringLiteral("SSFixture Sans")});
    QSignalSpy reloaded(&fonts, &FontController::fontsReloaded);
    fonts.files_may_have_arrived();
    CHECK(reloaded.isEmpty());

    SECTION("by sync")
    {
        fs::copy_file(source, file);
        fonts.files_may_have_arrived();
        CHECK(reloaded.count() == 1);
        CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
        settle();
        CHECK(fonts.missing().isEmpty());
    }
    SECTION("by adding it again")
    {
        QSignalSpy finished(&fonts, &FontController::addFinished);
        fonts.addFiles({QString::fromStdString(source.string())});
        REQUIRE(finished.wait(10000));
        CHECK(fs::exists(file));
        CHECK(finished.front().front().toList().front().toMap().value(QStringLiteral("text")).toString()
              == QStringLiteral("Added SSFixture Sans (Regular)."));
        CHECK(reloaded.count() == 1);
        CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
    }
    SECTION("damaged: tried once, and put right by adding it again")
    {
        std::ofstream(file, std::ios::binary) << "not a font";
        fonts.files_may_have_arrived();
        CHECK(reloaded.count() == 1);
        CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("Liberation Sans"));
        settle();
        CHECK(fonts.missing() == QStringList{QStringLiteral("SSFixture Sans")});
        fonts.files_may_have_arrived();
        CHECK(reloaded.count() == 1);

        QSignalSpy finished(&fonts, &FontController::addFinished);
        fonts.addFiles({QString::fromStdString(source.string())});
        REQUIRE(finished.wait(10000));
        CHECK(reloaded.count() == 2);
        CHECK(library.resolved("SSFixture Sans").font.family() == QStringLiteral("SSFixture Sans"));
    }
}


TEST_CASE("Removing a font still on its way says the computers that have it lose it",
          "[fonts][app][sync]")
{
    Library library;
    auto store = library.store();
    const auto regular = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    const auto bold = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf"));
    REQUIRE(regular.faces.size() == 1);
    REQUIRE(bold.faces.size() == 1);
    library.fonts().library_fonts_changed();
    // The bold's row has come from another computer; its file has not.
    const auto bold_file = QString::fromStdString(bold.faces.front().filename);
    const auto regular_file = QString::fromStdString(regular.faces.front().filename);
    fs::remove(library.font_folder() / bold.faces.front().filename);
    const auto family = QStringLiteral("SSFixture Sans");
    const auto note = [&](const QStringList& faces) { return library.fonts().arrivingRemoveNote(family, faces); };
    CHECK(note({}).isEmpty());

    using seventhslide::app::FileArrival;
    const auto path = "fonts/" + bold.faces.front().filename;
    library.facade.media()->set_arrivals({{path, {FileArrival::State::Downloading, 10, 100}}});
    CHECK(note({}).contains(QStringLiteral("lose them too")));
    CHECK(note({bold_file}).contains(QStringLiteral("lose them too")));
    // A face that is here, removed alone.
    CHECK(note({regular_file}).isEmpty());

    // On no computer connected: nobody loses it.
    library.facade.media()->set_arrivals({{path, {FileArrival::State::Nowhere, 0, 100}}});
    CHECK(note({}).isEmpty());
}
