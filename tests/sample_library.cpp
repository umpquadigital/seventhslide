#include "sample_library.hpp"

#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "media_controller.hpp"
#include "web_library_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>

#include <QColor>
#include <QImage>
#include <QString>

#include <filesystem>
#include <fstream>
#include <vector>

namespace seventhslide::test {
namespace {

namespace fs = std::filesystem;

void write_png(const fs::path& path, const QColor& color, int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    REQUIRE(image.save(QString::fromStdString(path.string()), "PNG"));
}

model::Song song(const char* title, const char* lyrics, const char* order)
{
    model::Song s;
    s.title = title;
    s.lyrics = lyrics;
    s.verse_order = order;
    return s;
}

}  // namespace

SampleLibrary fill_sample_library(app::SessionFacade& facade)
{
    SampleLibrary lib;
    const fs::path root = facade.media_root();
    REQUIRE_FALSE(root.empty());

    auto* songs = facade.song_repo();
    auto amazing = song("Amazing Grace",
                        "---[Verse:1]---\nAmazing grace how sweet the sound\nThat saved a wretch like me\n"
                        "I once was lost but now am found\nWas blind but now I see\n\n"
                        "---[Verse:2]---\nTwas grace that taught my heart to fear\nAnd grace my fears relieved\n"
                        "How precious did that grace appear\nThe hour I first believed",
                        "v1 v2");
    amazing.authors = {"John Newton"};
    amazing.copyright = "Public domain";
    amazing.topics = {"Grace", "Testimony"};
    amazing.comments = "Verse 4 is often left out.";
    lib.first_song = songs->insert(amazing);
    auto holy = song("Holy Holy Holy",
                     "---[Verse:1]---\nHoly holy holy\nLord God Almighty\nEarly in the morning\n"
                     "Our song shall rise to Thee\n\n---[Chorus:1]---\nHoly holy holy\nMerciful and mighty",
                     "v1 c1 v1");
    holy.authors = {"Reginald Heber", "John B. Dykes"};
    (void)songs->insert(holy);
    for (const char* title : {"Be Thou My Vision", "Come Thou Fount", "It Is Well",
                              "Rock of Ages"}) {
        auto s = song(title, "---[Verse:1]---\nFirst line\nSecond line\n\n---[Verse:2]---\nThird line\nFourth line",
                      "v1 v2");
        (void)songs->insert(s);
    }

    std::vector<model::BibleVerse> verses;
    const auto verse = [&verses](const char* book, int chapter, int num, const char* text) {
        model::BibleVerse v;
        v.book = book;
        v.chapter = chapter;
        v.verse_num = num;
        v.text = text;
        verses.push_back(v);
    };
    verse("Psalms", 23, 1, "The Lord is my shepherd; I shall not want.");
    verse("Psalms", 23, 2, "He makes me lie down in green pastures.");
    verse("John", 3, 16, "For God so loved the world, that he gave his only Son.");
    verse("John", 3, 17, "For God did not send his Son into the world to condemn the world.");
    verse("John", 3, 18, "Whoever believes in him is not condemned.");
    lib.bible = facade.bible_repo()->import_bible("Sample Bible", "Public domain", verses);

    using db::MediaKind;
    const fs::path images = root / "images";
    const fs::path videos = root / "videos";
    fs::create_directories(images);
    fs::create_directories(videos);
    auto* media = facade.media_repo();
    const char* names[] = {"sunrise.png", "ocean.png", "forest.png", "city.png", "snow.png"};
    const QColor colors[] = {QColor(230, 140, 60), QColor(40, 90, 160), QColor(40, 120, 60),
                             QColor(90, 90, 100), QColor(220, 225, 235)};
    for (int i = 0; i < 5; ++i) {
        write_png(images / names[i], colors[i], 320, 180);
        media->upsert_file(MediaKind::Image, names[i], names[i]);
    }
    lib.image_folder = media->folders(MediaKind::Image).create("Backgrounds");
    (void)media->add_folder_item(MediaKind::Image, lib.image_folder, "ocean.png");
    (void)media->add_folder_item(MediaKind::Image, lib.image_folder, "forest.png");
    (void)media->folders(MediaKind::Image).create("Seasons");
    // One real video, which has a thumbnail, and one that cannot be read,
    // which shows the glyph in its place.
    fs::copy_file(fs::path(SEVENTHSLIDE_FIXTURES_DIR) / "video" / "tone_3366cc_64x36_8s.mp4",
                  videos / "welcome loop.mp4");
    std::ofstream(videos / "countdown.mp4") << "not a real video";
    for (const char* name : {"countdown.mp4", "welcome loop.mp4"}) {
        media->upsert_file(MediaKind::Video, name, name);
    }
    const fs::path audio = root / "audio";
    fs::create_directories(audio);
    for (const char* name : {"Gathering pad.wav", "Postlude.flac", "Prelude in G.mp3"}) {
        std::ofstream(audio / name) << "not real audio";
        media->upsert_file(MediaKind::Audio, name, name);
    }
    const auto preludes = media->folders(MediaKind::Audio).create("Preludes");
    (void)media->add_folder_item(MediaKind::Audio, preludes, "Prelude in G.mp3");

    // The Browser tab: a folder and two pages.
    auto* web = facade.webLibrary();
    (void)web->createFolder(QStringLiteral("Announcements"));
    for (const auto& [name, url] : {std::pair{"Countdown", "https://example.com/countdown"},
                                    std::pair{"Welcome slides", "https://example.com/welcome"}}) {
        (void)web->savePage(-1, {{QStringLiteral("name"), QString::fromUtf8(name)},
                                 {QStringLiteral("url"), QString::fromUtf8(url)}});
    }

    auto* freeform = facade.freeformLibrary();
    lib.freeform = freeform->createFreeform(QStringLiteral("Welcome"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(lib.freeform));
    REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(edit->objects()
                ->setText(edit->objects()->selection().value(QStringLiteral("id")).toString(),
                          QStringLiteral("Welcome to worship"), false, false, false)
                .isEmpty());
    REQUIRE(edit->objects()
                ->insert(QStringLiteral("shape"), {{QStringLiteral("shapeKind"), QStringLiteral("ellipse")}})
                .isEmpty());
    REQUIRE(edit->save().isEmpty());
    edit->cancel();
    (void)freeform->createFreeform(QStringLiteral("Offering"));
    lib.freeform_folder = freeform->createFolder(QStringLiteral("Announcements"));
    facade.refresh_library();

    auto* library = facade.library();
    lib.other_service = library->createService(QStringLiteral("Good Friday"));
    REQUIRE(library->setServiceTags(lib.other_service, {QStringLiteral("Easter")}).isEmpty());
    REQUIRE(library->setServiceDateTime(lib.other_service, QStringLiteral("2026-04-03T19:00:00"))
                .isEmpty());
    lib.service = library->createService(QStringLiteral("Sunday Service"));
    REQUIRE(library->setServiceDateTime(lib.service, QStringLiteral("2026-03-01T10:30:00"))
                .isEmpty());
    REQUIRE(library->addSongToService(lib.first_song));
    REQUIRE(library->addSongToService(lib.first_song + 1));
    REQUIRE(library->addDividerToService(QStringLiteral("Scripture")));
    REQUIRE(library->add_bible_ref_to_service(lib.bible, QStringLiteral("John"), 3, 16, 17));
    REQUIRE(facade.media()->addImageToService(QStringLiteral("sunrise.png")));
    REQUIRE(facade.media()->addImageFolderToService(lib.image_folder));
    REQUIRE(freeform->addFreeformToService(lib.freeform));
    return lib;
}

}  // namespace seventhslide::test
