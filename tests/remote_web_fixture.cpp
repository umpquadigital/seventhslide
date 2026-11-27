// The library a web remote browser test runs the app against
// (tests/remote_web). Not a test: each browser test runs it to get a fresh
// library of its own.
//
//     SEVENTHSLIDE_REMOTE_FIXTURE_DIR=out SEVENTHSLIDE_REMOTE_FIXTURE_PORT=18731
//     ./seventhslide_tests_remote "[.remote_web]"
//
// SEVENTHSLIDE_REMOTE_FIXTURE_SONGS=N adds N songs ("Hymn 001" ...), for a
// test of a list longer than a page.
//
// out/data is a data directory for the app (SEVENTHSLIDE_DATA_DIR): its
// default library holds the sample library (sample_library.hpp) plus what
// only the remote needs, and serves the remote on the port with PIN 2468.
// The app opens no service when it starts, so a test opens the one it wants.
// out/fixture.json says what the library holds, for the test to read. The
// library registry names libraries by absolute path, so out/data cannot be
// copied or moved: build another.

#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "live_library_controller.hpp"
#include "media_controller.hpp"
#include "profile_controller.hpp"
#include "sample_library.hpp"
#include "stack_controller.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "wav_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>

#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using seventhslide::app::SessionFacade;

namespace {

namespace fs = std::filesystem;

constexpr const char* k_pin = "2468";
constexpr const char* k_second_stack = "Lower thirds";
constexpr const char* k_preview_stack = "Stage";

/// Build the library in ``data_dir`` and answer what the tests need to know
/// of it.
nlohmann::json build(const fs::path& data_dir, int port, int extra_songs)
{
    SessionFacade facade;
    REQUIRE(facade.init_libraries(data_dir));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    // The app the tests run is the program, which copies its shipped Bibles
    // into a library the first time it opens one: noted as done, the library
    // keeps its one Bible, and the app is not busy writing three others while
    // the tests run.
    seventhslide::db::BibleRepository(*facade.open_library().database()).note_bundled_taken();

    for (int i = 1; i <= extra_songs; ++i) {
        seventhslide::model::Song song;
        char title[16];
        std::snprintf(title, sizeof title, "Hymn %03d", i);
        song.title = title;
        song.lyrics = "[V1]\nOne line";
        song.verse_order = "v1";
        REQUIRE(facade.song_repo()->insert(song) > 0);
    }

    // Media the app can actually play: the sample's videos and audio are
    // empty files.
    const fs::path root = facade.media_root();
    fs::copy_file(fs::path(SEVENTHSLIDE_FIXTURES_DIR) / "video" / "solid_3366cc_64x36_20s.mp4",
                  root / "videos" / "clip.mp4");
    seventhslide::test::write_tone_wav(root / "audio" / "Tone.wav", 4.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, "clip.mp4", "clip.mp4");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "Tone.wav", "Tone.wav");

    // A Freeform deck with more than one slide, to move through.
    const auto deck = facade.freeformLibrary()->createFreeform(QStringLiteral("Three slides"));
    auto* editor = facade.slideEdit();
    REQUIRE(editor->openFreeform(deck));
    for (const char* text : {"One", "Two", "Three"}) {
        if (std::string_view(text) != "One") {
            REQUIRE(editor->slides()->insert().isEmpty());
        }
        auto* objects = editor->objects();
        REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(objects->setText(objects->selection().value(QStringLiteral("id")).toString(),
                                 QString::fromUtf8(text), false, false, false)
                    .isEmpty());
    }
    REQUIRE(editor->save().isEmpty());
    editor->cancel();

    // A second stack, so two things can be live at once and the tabs show.
    REQUIRE(facade.stacks()->addStack(QString::fromUtf8(k_second_stack)).isEmpty());
    std::string second_stack;
    for (const auto& def : facade.open_library().settings().stacks) {
        if (def.name == k_second_stack) {
            second_stack = def.id;
        }
    }
    REQUIRE_FALSE(second_stack.empty());

    // A third with a Preview (this computer's setting): what is sent to it
    // is cued, for Transition to take. Nothing is sent to it unless a test
    // names it.
    REQUIRE(facade.stacks()->addStack(QString::fromUtf8(k_preview_stack)).isEmpty());
    std::string preview_stack;
    for (const auto& def : facade.open_library().settings().stacks) {
        if (def.name == k_preview_stack) {
            preview_stack = def.id;
        }
    }
    REQUIRE_FALSE(preview_stack.empty());
    REQUIRE(facade.stacks()->setStackPreview(QString::fromStdString(preview_stack), true).isEmpty());

    // Scripture on the second stack, as a lower third: playing the song and
    // then the reading puts something on both.
    auto* library = facade.library();
    qint64 reading = -1;
    for (const auto& item : facade.service_repo()->list_items(lib.service)) {
        if (item.item_type == seventhslide::model::k_item_type_bible) {
            reading = *item.item_id;
        }
    }
    REQUIRE(reading > 0);
    REQUIRE(library->set_service_item_stack(reading, QString::fromStdString(second_stack))
                .isEmpty());

    // Audio and video in a service of their own, so the sample's rundown stays
    // as it is.
    const auto media_service = library->createService(QStringLiteral("Media"));
    REQUIRE(media_service > 0);
    REQUIRE(facade.media()->addAudioToService(QStringLiteral("Tone.wav")));
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));

    // A stream on the Live tab: kept by the library, so it is listed with no
    // network to look on. (The sample library has the Browser tab's pages.)
    const QVariantMap stream = facade.liveLibrary()->saveStream(
        {}, QStringLiteral("Overflow room"), QStringLiteral("srt://127.0.0.1:9"));
    REQUIRE(stream.contains(QStringLiteral("key")));

    QVariantMap remote;
    remote.insert(QStringLiteral("remoteEnabled"), true);
    remote.insert(QStringLiteral("remotePin"), QString::fromUtf8(k_pin));
    remote.insert(QStringLiteral("remotePort"), port);
    REQUIRE(facade.updateAppSettings(remote).isEmpty());

    // The active style profile mirrors the live tables; the app flushes it
    // on exit, and opening the library applies it.
    facade.profiles()->capture_active_profile();

    const auto& outputs = facade.session().outputs();
    REQUIRE_FALSE(outputs.empty());
    return {
        {"pin", k_pin},
        {"secondStack", {{"id", second_stack}, {"name", k_second_stack}}},
        {"previewStack", {{"id", preview_stack}, {"name", k_preview_stack}}},
        {"primaryOutputId", outputs.front().id},
        {"firstSongId", lib.first_song},
        {"serviceId", lib.service},
        {"mediaServiceId", media_service},
        {"readingItemId", reading},
        {"otherServiceId", lib.other_service},
        {"bibleId", lib.bible},
        {"imageFolderId", lib.image_folder},
        {"freeformId", lib.freeform},
        {"threeSlidesId", deck},
        {"freeformFolderId", lib.freeform_folder},
    };
}

}  // namespace

TEST_CASE("Web remote fixture library", "[.remote_web]")
{
    const char* out = std::getenv("SEVENTHSLIDE_REMOTE_FIXTURE_DIR");
    const char* port = std::getenv("SEVENTHSLIDE_REMOTE_FIXTURE_PORT");
    REQUIRE(out != nullptr);
    REQUIRE(port != nullptr);
    seventhslide::test::ensure_qt_application();

    const fs::path dir(out);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const char* songs = std::getenv("SEVENTHSLIDE_REMOTE_FIXTURE_SONGS");
    const auto about = build(dir / "data", std::stoi(port), songs ? std::stoi(songs) : 0);
    std::ofstream(dir / "fixture.json") << about.dump(2) << '\n';
}
