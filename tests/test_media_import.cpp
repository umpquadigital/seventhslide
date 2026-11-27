// Media import copies off the GUI thread and commits from the event loop.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "refusals.hpp"
#include "font_store.hpp"
#include "library_models.hpp"
#include "media_controller.hpp"
#include "theme_controller.hpp"
#include "media_import_helpers.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QThread>

#include <seventhslide/db/media_listing.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/db/sync_tables.hpp>
#include <seventhslide/library/library.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using seventhslide::app::MediaBrowserModel;
using seventhslide::app::MediaController;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;
using seventhslide::test::wait_for_import;
namespace fs = std::filesystem;
namespace refusal = seventhslide::app::refusal;

namespace {

void write_bytes(const fs::path& path, std::size_t size)
{
    std::ofstream out(path, std::ios::binary);
    const std::string block(64u * 1024u, 'v');
    for (std::size_t done = 0; done < size; done += block.size()) {
        out.write(block.data(),
                  static_cast<std::streamsize>(std::min(block.size(), size - done)));
    }
}

void write_png(const fs::path& path)
{
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(QColor(20, 120, 220));
    REQUIRE(img.save(QString::fromStdString(path.string())));
}

QString qpath(const fs::path& path)
{
    return QString::fromStdString(path.string());
}

std::vector<fs::path> staging_files(const fs::path& media_root)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(media_root, ec)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(".import-")) {
            out.push_back(entry.path());
        }
    }
    return out;
}

/// The files in ``dir`` an import put there for good: not staged ones.
std::vector<std::string> committed_files(const fs::path& dir)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && !name.starts_with(".import-")) {
            out.push_back(name);
        }
    }
    return out;
}

/// Wait, without running the event loop, until the worker has staged a file.
bool wait_for_staging_file(const fs::path& media_root)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        if (!staging_files(media_root).empty()) {
            return true;
        }
        QThread::msleep(2);
    }
    return false;
}

}  // namespace

TEST_CASE("Importing a video returns before the file reaches the library", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    const auto root = facade.media_root();
    const auto src = dir.path / "clip.mp4";
    constexpr std::size_t k_size = 3u * 1024u * 1024u;
    write_bytes(src, k_size);

    const auto id = media.import_files(MediaKind::Video, {qpath(src)}, -1);

    // The GUI thread is free while the copy runs: nothing is in the library
    // until the event loop commits it.
    REQUIRE(id > 0);
    REQUIRE(media.importing());
    REQUIRE(committed_files(root / "videos").empty());
    REQUIRE(media.browser(MediaKind::Video)->rowCount() == 0);

    // A browser rebuilt while the copy is staged does not list the partial file.
    REQUIRE(wait_for_staging_file(root));
    // And sync would leave it alone: it is this computer's, half copied.
    for (const auto& staged : staging_files(root)) {
        CHECK(seventhslide::library::is_local_only(
            fs::relative(staged, root.parent_path())));
    }
    media.library(MediaKindQml::Video)->setFolder(-1);
    REQUIRE(media.browser(MediaKind::Video)->rowCount() == 0);

    const auto result = wait_for_import(media, id);
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    REQUIRE(result.value(QStringLiteral("count")).toInt() == 1);
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 1);
    REQUIRE(fs::file_size(root / "videos" / names.front().toStdString()) == k_size);
    REQUIRE(media.browser(MediaKind::Video)->rowCount() == 1);
    // Listed by the name it came in with.
    const auto* videos = media.browser(MediaKind::Video);
    REQUIRE(videos->data(videos->index(0), MediaBrowserModel::NameRole) == QStringLiteral("clip.mp4"));
    REQUIRE_FALSE(media.importing());
    REQUIRE(staging_files(root).empty());
}

TEST_CASE("Imported videos get names of their own and keep their titles", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    fs::create_directories(dir.path / "a");
    fs::create_directories(dir.path / "b");
    write_bytes(dir.path / "a" / "intro.mp4", 1000);
    write_bytes(dir.path / "b" / "intro.mp4", 2000);

    const auto result = wait_for_import(
        media, media.import_files(MediaKind::Video,
                                  {qpath(dir.path / "a" / "intro.mp4"),
                                   qpath(dir.path / "b" / "intro.mp4")},
                                  -1));

    // Opaque, so two files of one name never collide, here or on another
    // computer; each is still titled as it came in.
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] != names[1]);
    const auto videos = facade.media_root() / "videos";
    REQUIRE(fs::file_size(videos / names[0].toStdString()) == 1000);
    REQUIRE(fs::file_size(videos / names[1].toStdString()) == 2000);
    for (const auto& name : names) {
        REQUIRE(name.endsWith(QStringLiteral(".mp4")));
        REQUIRE(media.title_of(MediaKind::Video, name.toStdString()) == "intro.mp4");
    }
}

TEST_CASE("Imported audio is registered, lands in its folder and refuses other files",
          "[app][media][import][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    write_bytes(dir.path / "Prelude in G.mp3", 1000);
    write_bytes(dir.path / "clip.mp4", 1000);
    const auto folder = media.library(MediaKindQml::Audio)->createFolder(QStringLiteral("Preludes"));
    REQUIRE(folder >= 0);

    const auto result = wait_for_import(
        media, media.importMedia(MediaKindQml::Audio,
                                 {qpath(dir.path / "Prelude in G.mp3"), qpath(dir.path / "clip.mp4")},
                                 folder));

    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 1);
    const auto filename = names.front();
    REQUIRE(result.value(QStringLiteral("errors")).toStringList()
            == QStringList{QStringLiteral("clip.mp4: not an audio file SeventhSlide can use")});
    REQUIRE(fs::file_size(facade.media_root() / "audio" / filename.toStdString()) == 1000);
    REQUIRE(committed_files(facade.media_root() / "audio").size() == 1);
    // Registered as audio, titled as it came in; not as an image.
    const auto registered = facade.media_repo()->get_file(MediaKind::Audio, filename.toStdString());
    REQUIRE(registered);
    REQUIRE(registered->title() == "Prelude in G.mp3");
    REQUIRE(facade.media_repo()->list_files(MediaKind::Image, false).empty());

    // Filed in the folder it was imported into, so the root shows only the folder.
    REQUIRE(media.browser(MediaKind::Audio)->rowCount() == 1);
    media.library(MediaKindQml::Audio)->setFolder(folder);
    const auto* rows = media.browser(MediaKind::Audio);
    bool found = false;
    for (int r = 0; r < rows->rowCount(); ++r) {
        found = found
                || rows->data(rows->index(r), MediaBrowserModel::FilenameRole).toString()
                       == filename;
    }
    REQUIRE(found);
    // The other kinds' browsers are untouched.
    REQUIRE(media.browser(MediaKind::Video)->rowCount() == 0);
}

TEST_CASE("An import for an unknown media kind still reports", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    const auto result =
        wait_for_import(media, media.importMedia(static_cast<MediaKindQml::Kind>(7), {}, -1));
    REQUIRE(result.value(QStringLiteral("errors")).toStringList()
            == QStringList{refusal::unknown_media_kind()});
}

TEST_CASE("Switching library stops an import and leaves nothing behind", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Other")).isEmpty());
    auto& media = *facade.media();
    const auto old_root = facade.media_root();
    const auto src = dir.path / "service.mp4";
    write_bytes(src, 32u * 1024u * 1024u);

    const auto id = media.import_files(MediaKind::Video, {qpath(src)}, -1);
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("other")).isEmpty());

    // Whether the copy was cut short or had just finished, the file is gone
    // from the library it was meant for and never reaches the new one.
    const auto result = wait_for_import(media, id);
    REQUIRE_FALSE(result.value(QStringLiteral("ok")).toBool());
    REQUIRE(result.value(QStringLiteral("count")).toInt() == 0);
    const auto errors = result.value(QStringLiteral("errors")).toStringList();
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front()
            == QStringLiteral("service.mp4: the library was closed before the file was copied in"));
    REQUIRE(committed_files(old_root / "videos").empty());
    REQUIRE(staging_files(old_root).empty());
    REQUIRE(facade.media_root() != old_root);
    REQUIRE(committed_files(facade.media_root() / "videos").empty());
    REQUIRE(media.browser(MediaKind::Video)->rowCount() == 0);
    REQUIRE_FALSE(media.importing());
}

TEST_CASE("An image imported into a folder deleted meanwhile lands loose", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    QSignalSpy errors(&media, &MediaController::errorOccurred);
    const auto folder = media.library(MediaKindQml::Image)->createFolder(QStringLiteral("Gone"));
    REQUIRE(folder > 0);
    const auto src = dir.path / "slide.png";
    write_png(src);

    const auto id = media.import_files(MediaKind::Image, {qpath(src)}, folder);
    REQUIRE(media.library(MediaKindQml::Image)->deleteFolder(folder).isEmpty());

    const auto result = wait_for_import(media, id);
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    REQUIRE(result.value(QStringLiteral("count")).toInt() == 1);
    REQUIRE(media.images()->rowCount() == 1);
    REQUIRE(errors.isEmpty());
}

TEST_CASE("Each import reports once, in the order it was started", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    write_bytes(dir.path / "notes.txt", 10);
    write_bytes(dir.path / "clip.mp4", 10);
    std::vector<qint64> reported;
    std::vector<QVariantMap> results;
    QObject::connect(&media, &MediaController::importFinished,
                     [&](qint64 id, const QVariantMap& result) {
                         reported.push_back(id);
                         results.push_back(result);
                     });

    const auto a = media.import_files(MediaKind::Image, {qpath(dir.path / "notes.txt")}, -1);
    const auto b = media.import_files(MediaKind::Video, {qpath(dir.path / "clip.mp4")}, -1);
    const auto c = media.import_files(MediaKind::Image, {qpath(dir.path / "missing.png")}, -1);
    // Not from inside the call, even when there is nothing to copy.
    REQUIRE(reported.empty());

    REQUIRE(QTest::qWaitFor([&] { return reported.size() >= 3; }, 30000));
    QTest::qWait(50);
    REQUIRE(reported == std::vector<qint64>{a, b, c});
    REQUIRE(results[0].value(QStringLiteral("errors")).toStringList()
            == QStringList{QStringLiteral("notes.txt: not a picture SeventhSlide can use")});
    REQUIRE(results[1].value(QStringLiteral("ok")).toBool());
    REQUIRE(results[2].value(QStringLiteral("errors")).toStringList()
            == QStringList{QStringLiteral("missing.png: ")
                           + QString::fromStdString(seventhslide::app::import_not_found())});
}

TEST_CASE("An import with no library open still reports", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto& media = *facade.media();
    QSignalSpy finished(&media, &MediaController::importFinished);

    const auto id = media.import_files(MediaKind::Image, {QStringLiteral("/tmp/x.png")}, -1);
    REQUIRE(finished.isEmpty());

    const auto result = wait_for_import(media, id);
    REQUIRE_FALSE(result.value(QStringLiteral("ok")).toBool());
    REQUIRE(result.value(QStringLiteral("errors")).toStringList()
            == QStringList{refusal::no_library()});
}

TEST_CASE("A library opens without the staging and thumbnail temp files a crash left",
          "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    fs::path root;
    {
        SessionFacade facade;
        REQUIRE(facade.open_database(dir.path / "library.db"));
        root = facade.media_root();
    }
    fs::create_directories(root / "videos");
    fs::create_directories(root / "images");
    write_bytes(root / "videos" / ".import-0123abcd.part", 100);
    write_bytes(root / "images" / ".import-4567ef01.part", 100);
    write_bytes(root / "videos" / "keep.mp4", 100);
    write_bytes(root / "videos" / ".import-notes.txt", 100);
    // A download's folder, with yt-dlp's half-made file in it.
    fs::create_directories(root / "videos" / ".import-dl000000.part");
    write_bytes(root / "videos" / ".import-dl000000.part" / "abc.mp4.part", 100);
    fs::create_directories(root / "theme_backgrounds");
    write_bytes(root / "theme_backgrounds" / ".import-89abcdef.part", 100);
    fs::create_directories(root / "thumbs" / "160");
    fs::create_directories(root / "thumbs" / "freeform");
    write_bytes(root / "thumbs" / "160" / "a.jpg.999999999-0.tmp", 100);
    write_bytes(root / "thumbs" / "freeform" / "3.stamp.999999999-1.tmp", 100);
    write_bytes(root / "thumbs" / "160" / "a.jpg", 100);

    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));

    REQUIRE_FALSE(fs::exists(root / "videos" / ".import-0123abcd.part"));
    REQUIRE_FALSE(fs::exists(root / "images" / ".import-4567ef01.part"));
    REQUIRE_FALSE(fs::exists(root / "theme_backgrounds" / ".import-89abcdef.part"));
    REQUIRE(fs::exists(root / "videos" / "keep.mp4"));
    REQUIRE(fs::exists(root / "videos" / ".import-notes.txt"));
    REQUIRE_FALSE(fs::exists(root / "videos" / ".import-dl000000.part"));
    REQUIRE_FALSE(fs::exists(root / "thumbs" / "160" / "a.jpg.999999999-0.tmp"));
    REQUIRE_FALSE(fs::exists(root / "thumbs" / "freeform" / "3.stamp.999999999-1.tmp"));
    REQUIRE(fs::exists(root / "thumbs" / "160" / "a.jpg"));
}

TEST_CASE("Tearing down mid-import leaves no file the library would list", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto src = dir.path / "long.mp4";
    write_bytes(src, 64u * 1024u * 1024u);
    fs::path root;
    {
        SessionFacade facade;
        REQUIRE(facade.open_database(dir.path / "library.db"));
        root = facade.media_root();
        (void)facade.media()->import_files(MediaKind::Video, {qpath(src)}, -1);
        REQUIRE(wait_for_staging_file(root));
    }  // the copy stops; nothing commits it

    REQUIRE(committed_files(root / "videos").empty());
    // A copy that finished before teardown is still only staged; the next
    // open clears it.
    SessionFacade reopened;
    REQUIRE(reopened.open_database(dir.path / "library.db"));
    REQUIRE(staging_files(root).empty());
    REQUIRE(reopened.media()->browser(MediaKind::Video)->rowCount() == 0);
}

TEST_CASE("Import progress is published while files copy", "[app][media][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& media = *facade.media();
    write_bytes(dir.path / "one.mp4", 1000);
    write_bytes(dir.path / "two.mp4", 1000);
    QSignalSpy progress(&media, &MediaController::importProgressChanged);

    const auto id = media.import_files(
        MediaKind::Video, {qpath(dir.path / "one.mp4"), qpath(dir.path / "two.mp4")}, -1);
    REQUIRE(media.importing());
    REQUIRE(media.importProgress() == 0.0);
    REQUIRE_FALSE(progress.isEmpty());

    std::vector<double> seen;
    QObject::connect(&media, &MediaController::importProgressChanged,
                     [&] { seen.push_back(media.importProgress()); });
    (void)wait_for_import(media, id);

    REQUIRE_FALSE(media.importing());
    REQUIRE(media.importProgress() == 0.0);
    // One file done of two is reported on the way.
    REQUIRE(std::find(seen.begin(), seen.end(), 0.5) != seen.end());
}

TEST_CASE("A library folder holds its database, its media and what is local-only",
          "[app][media][import][sync]")
{
    // What sync finds in a library folder: the database, each file a record
    // owns (db::k_sync_files), and what library::is_local_only names. Anything
    // else would be a file the app writes without saying how it syncs.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    auto& media = *facade.media();
    write_png(dir.path / "slide.png");
    write_bytes(dir.path / "clip.mp4", 1000);
    write_bytes(dir.path / "hymn.mp3", 1000);
    write_png(dir.path / "dawn.png");
    (void)wait_for_import(media, media.import_files(MediaKind::Image,
                                                    {qpath(dir.path / "slide.png")}, -1));
    (void)wait_for_import(media, media.import_files(MediaKind::Video,
                                                    {qpath(dir.path / "clip.mp4")}, -1));
    (void)wait_for_import(media, media.import_files(MediaKind::Audio,
                                                    {qpath(dir.path / "hymn.mp3")}, -1));
    // A theme's background.
    auto& themes = *facade.themes();
    const auto bg_themes = themes.list(QStringLiteral("Main"), QStringLiteral("bg"));
    REQUIRE_FALSE(bg_themes.isEmpty());
    bool uploaded = false;
    QObject::connect(&themes, &seventhslide::app::ThemeController::assetUploadFinished,
                     [&](qint64, const QString& error) {
                         CHECK(error.isEmpty());
                         uploaded = true;
                     });
    REQUIRE(themes.upload_asset(QStringLiteral("Main"),
                               bg_themes.front().toMap().value(QStringLiteral("id")).toString(),
                               QStringLiteral("bg"),
                               QUrl::fromLocalFile(qpath(dir.path / "dawn.png")).toString())
            > 0);
    REQUIRE(QTest::qWaitFor([&] { return uploaded; }, 10000));
    // Thumbnails are written in the background; let them land.
    REQUIRE(QTest::qWaitFor(
        [&] { return fs::exists(facade.media_root() / seventhslide::library::k_thumbs_dirname); },
        10000));

    auto& db = *facade.open_library().database();
    // And the shipped fonts' copy, written in the background.
    REQUIRE(QTest::qWaitFor([&] { return seventhslide::db::FontRepository(db).bundled_taken(); }, 20000));
    seventhslide::app::FontStore fonts(db, facade.media_root());
    CHECK(fonts.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf")).error.empty());
    const auto library_root = facade.media_root().parent_path();
    int owned_files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(library_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto relative = fs::relative(entry.path(), library_root);
        INFO(relative.generic_string());
        if (relative == fs::path(seventhslide::library::k_database_filename)
            || seventhslide::library::is_local_only(relative)) {
            continue;
        }
        // Otherwise a file a record owns, in the folder its kind keeps.
        const auto name = relative.filename().string();
        bool owned = false;
        for (const auto& f : seventhslide::db::k_sync_files) {
            if (relative.parent_path()
                != fs::path(seventhslide::library::k_media_dirname) / f.folder) {
                continue;
            }
            if (f.in == seventhslide::db::FileIn::Key) {
                owned = owned || seventhslide::db::read_record(db, f.table, name).has_value();
                continue;
            }
            const auto url = "/static/" + std::string(f.folder) + "/" + name;
            for (const auto& key : seventhslide::db::record_keys(db, f.table)) {
                const auto doc = seventhslide::db::read_record(db, f.table, key);
                owned = owned || (doc && doc->dump().find(url) != std::string::npos);
            }
        }
        CHECK(owned);
        owned_files += owned ? 1 : 0;
    }
    // Four files, a font added, and the fonts every library starts with.
    CHECK(owned_files == 4 + static_cast<int>(seventhslide::db::FontRepository(db).list_all().size()));
}

