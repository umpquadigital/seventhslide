// Export: the ZIP writer, export file names, a content's stills, and
// session.exporter end to end (images, song XML, media files).

#include "session_facade_fixture.hpp"
#include "test_temp_dir.hpp"

#include "export_controller.hpp"
#include "media_controller.hpp"

#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/parse/song_xml.hpp>
#include <seventhslide/session/still_frames.hpp>
#include <seventhslide/util/file_name.hpp>
#include <seventhslide/util/zip_writer.hpp>

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using seventhslide::app::ExportController;
namespace MediaKindQml = seventhslide::app::MediaKindQml;
using seventhslide::app::SessionFacade;
using seventhslide::util::portable_file_name;
using seventhslide::util::unique_file_name;
using seventhslide::util::ZipWriter;

namespace {

[[nodiscard]] std::uint32_t le32(const std::string& s, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at]))
           | static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8
           | static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16
           | static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24;
}

[[nodiscard]] std::uint16_t le16(const std::string& s, std::size_t at)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(s[at])
                                      | static_cast<unsigned char>(s[at + 1]) << 8);
}

/// CRC-32 (IEEE 802.3) a bit at a time, to check the writer's by.
[[nodiscard]] std::uint32_t crc32_bitwise(const std::string& s)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char c : s) {
        crc ^= static_cast<unsigned char>(c);
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/// A stored ZIP read the way an unzip reads one: from the directory at its
/// end, each entry's bytes found through its local header and checked
/// against its CRC. Fails the test on anything malformed.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> read_zip(const std::string& zip)
{
    REQUIRE(zip.size() >= 22);
    const std::size_t end = zip.size() - 22;
    REQUIRE(le32(zip, end) == 0x06054b50u);
    const std::uint16_t count = le16(zip, end + 10);
    REQUIRE(le16(zip, end + 8) == count);
    const std::uint32_t directory_size = le32(zip, end + 12);
    std::size_t at = le32(zip, end + 16);
    REQUIRE(at + directory_size == end);

    std::vector<std::pair<std::string, std::string>> entries;
    for (std::uint16_t i = 0; i < count; ++i) {
        REQUIRE(le32(zip, at) == 0x02014b50u);
        CHECK((le16(zip, at + 8) & 0x0800u) != 0);  // names are UTF-8
        REQUIRE(le16(zip, at + 10) == 0);           // stored
        const std::uint32_t crc = le32(zip, at + 16);
        const std::uint32_t size = le32(zip, at + 20);
        REQUIRE(le32(zip, at + 24) == size);
        const std::uint16_t name_len = le16(zip, at + 28);
        const std::uint16_t extra_len = le16(zip, at + 30);
        const std::uint16_t comment_len = le16(zip, at + 32);
        const std::uint32_t local = le32(zip, at + 42);
        const std::string name = zip.substr(at + 46, name_len);
        at += 46 + name_len + extra_len + comment_len;

        REQUIRE(le32(zip, local) == 0x04034b50u);
        REQUIRE(le32(zip, local + 14) == crc);
        REQUIRE(zip.substr(local + 30, le16(zip, local + 26)) == name);
        const std::size_t data = local + 30 + le16(zip, local + 26) + le16(zip, local + 28);
        std::string bytes = zip.substr(data, size);
        REQUIRE(crc32_bitwise(bytes) == crc);
        entries.emplace_back(name, std::move(bytes));
    }
    return entries;
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Run the loop until export ``id`` reports; its result.
[[nodiscard]] QVariantMap wait_for_export(ExportController& exporter, qint64 id)
{
    QVariantMap result;
    bool done = false;
    const auto connection = QObject::connect(&exporter, &ExportController::exportFinished,
                                             [&](qint64 request, const QVariantMap& r) {
                                                 if (request == id) {
                                                     result = r;
                                                     done = true;
                                                 }
                                             });
    (void)QTest::qWaitFor([&] { return done; }, 60000);
    QObject::disconnect(connection);
    REQUIRE(done);
    return result;
}

[[nodiscard]] QString url_of(const fs::path& path)
{
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString();
}

[[nodiscard]] std::vector<std::string> names_of(
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::vector<std::string> names;
    for (const auto& [name, bytes] : entries) {
        names.push_back(name);
    }
    return names;
}

/// A song of two short verses: two slides on any output.
[[nodiscard]] seventhslide::model::Song two_verse_song()
{
    seventhslide::model::Song song;
    song.title = "Test Song";
    song.lyrics = "---[Verse:1]---\nPraise to the Lord\nThe Almighty\n\n"
                  "---[Verse:2]---\nO my soul praise Him\nFor He is thy health";
    song.verse_order = "v1 v2";
    return song;
}

constexpr std::string_view k_png_signature = "\x89PNG\r\n\x1a\n";

[[nodiscard]] bool is_png(const std::string& bytes)
{
    return bytes.compare(0, k_png_signature.size(), k_png_signature) == 0;
}

}  // namespace

TEST_CASE("A ZIP entry carries the IEEE CRC-32 of its bytes", "[export][zip]")
{
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    REQUIRE(zip.add("check.txt", "123456789"));
    REQUIRE(zip.finish());
    CHECK(le32(out.str(), 14) == 0xCBF43926u);
}

TEST_CASE("ZipWriter stores entries that read back whole", "[export][zip]")
{
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {2026, 10, 27, 9, 30, 15});
    REQUIRE(zip.add("Main/01-01 Amazing Grace.png", "first"));
    REQUIRE(zip.add("Main/01-02 Amazing Grace.png", std::string(3, '\0') + "second"));
    REQUIRE(zip.add("Hall/01-01 Café.png", ""));
    REQUIRE(zip.finish());

    const auto entries = read_zip(out.str());
    REQUIRE(entries.size() == 3);
    CHECK(entries[0] == std::pair<std::string, std::string>{"Main/01-01 Amazing Grace.png", "first"});
    CHECK(entries[1].second == std::string(3, '\0') + "second");
    CHECK(entries[2].first == "Hall/01-01 Café.png");
    CHECK(entries[2].second.empty());

    // The date is the one given, in DOS form.
    const std::string bytes = out.str();
    const std::uint16_t date = le16(bytes, 12);
    CHECK((date >> 9) + 1980 == 2026);
    CHECK(((date >> 5) & 0xF) == 10);
    CHECK((date & 0x1F) == 27);
}

TEST_CASE("ZipWriter refuses what would make a bad archive", "[export][zip]")
{
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    CHECK_FALSE(zip.add("", "x"));
    CHECK_FALSE(zip.add("/absolute.png", "x"));
    REQUIRE(zip.add("a.png", "x"));
    CHECK_FALSE(zip.add("a.png", "again"));
    CHECK_FALSE(zip.error().empty());
    REQUIRE(zip.finish());
    CHECK_FALSE(zip.add("b.png", "late"));
    CHECK(read_zip(out.str()).size() == 1);
}

TEST_CASE("portable_file_name keeps a title readable and safe", "[export][file_name]")
{
    CHECK(portable_file_name("Amazing Grace", "x") == "Amazing Grace");
    CHECK(portable_file_name("John 3:16-18", "x") == "John 3.16-18");
    CHECK(portable_file_name("AC/DC \\ Live | Loud", "x") == "AC-DC - Live - Loud");
    CHECK(portable_file_name("What \"Love\" <is>?*", "x") == "What 'Love' is");
    CHECK(portable_file_name("  ..Trailing dots..  ", "x") == "Trailing dots");
    CHECK(portable_file_name("Tab\there\nnewline", "x") == "Tab here newline");
    CHECK(portable_file_name("Ave María", "x") == "Ave María");
    CHECK(portable_file_name("CON", "x") == "CON_");
    CHECK(portable_file_name("nul.txt", "x") == "nul.txt_");
    CHECK(portable_file_name("Console", "x") == "Console");
    CHECK(portable_file_name("???", "Song") == "Song");
    CHECK(portable_file_name("", "") == "Untitled");

    // Cut on a character boundary, never inside one.
    std::string long_title;
    for (int i = 0; i < 100; ++i) {
        long_title += "é";
    }
    const auto cut = portable_file_name(long_title, "x");
    CHECK(cut.size() <= seventhslide::util::k_portable_name_max_bytes);
    CHECK(cut.size() % 2 == 0);
}

TEST_CASE("unique_file_name numbers a name already taken", "[export][file_name]")
{
    const std::unordered_set<std::string> taken{"song.xml", "song 2.xml", "folder"};
    CHECK(unique_file_name("Other", ".xml", taken) == "Other.xml");
    CHECK(unique_file_name("Song", ".xml", taken) == "Song 3.xml");
    CHECK(unique_file_name("Folder", "", taken) == "Folder 2");
}

TEST_CASE("still_frames gives a frame per slide, per image, and none for media",
          "[export][session]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    std::vector<seventhslide::model::OutputConfig> outputs{seventhslide::test::facade::make_main()};
    auto second = outputs.front();
    second.id = "out-hall";
    second.name = "Hall";
    outputs.push_back(second);
    const std::vector<std::string> priority;
    const std::string ccli;
    const seventhslide::theme::OutputThemeMap service_themes;
    const seventhslide::session::StackBuildContext ctx{measurer, outputs, priority, ccli,
                                                       service_themes};

    const auto song = seventhslide::session::still_frames(two_verse_song(), ctx);
    REQUIRE(song.size() == 2);
    CHECK(song[0].output_id == "out-main");
    CHECK(song[1].output_id == "out-hall");
    CHECK(song[0].frames.size() == 2);
    CHECK(song[1].frames.size() == 2);

    seventhslide::model::ServiceItem folder;
    folder.item_type = seventhslide::model::k_item_type_image_folder;
    folder.data = {{"folder_name", "Pack"}, {"images", {"a.png", "b.png", "c.png"}}};
    const auto images = seventhslide::session::still_frames(folder, ctx);
    REQUIRE(images.size() == 2);
    REQUIRE(images[0].frames.size() == 3);
    REQUIRE(images[0].frames[2].image.has_value());
    CHECK(images[0].frames[2].image->url == "/static/images/c.png");

    seventhslide::model::ServiceItem video;
    video.item_type = seventhslide::model::k_item_type_video;
    video.data = seventhslide::model::playback_item_data("clip.mp4", "Clip");
    for (const auto& output : seventhslide::session::still_frames(video, ctx)) {
        CHECK(output.frames.empty());
    }
}

TEST_CASE("first_still_frame gives the first of still_frames on one output",
          "[export][session]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    std::vector<seventhslide::model::OutputConfig> outputs{seventhslide::test::facade::make_main()};
    auto second = outputs.front();
    second.id = "out-hall";
    second.name = "Hall";
    outputs.push_back(second);
    const std::vector<std::string> priority;
    const std::string ccli;
    const seventhslide::theme::OutputThemeMap service_themes;
    const seventhslide::session::StackBuildContext ctx{measurer, outputs, priority, ccli,
                                                       service_themes};
    using seventhslide::session::first_still_frame;

    const auto all = seventhslide::session::still_frames(two_verse_song(), ctx);
    REQUIRE(all.size() == 2);
    REQUIRE_FALSE(all[1].frames.empty());
    const auto song = first_still_frame(two_verse_song(), ctx, "out-hall");
    REQUIRE(song.has_value());
    CHECK(song->slide_index == 0);
    CHECK(song->text == all[1].frames[0].text);

    seventhslide::model::ServiceItem folder;
    folder.item_type = seventhslide::model::k_item_type_image_folder;
    folder.data = {{"folder_name", "Pack"}, {"images", {"a.png", "b.png"}}};
    const auto image = first_still_frame(folder, ctx, "out-main");
    REQUIRE(image.has_value());
    REQUIRE(image->image.has_value());
    CHECK(image->image->url == "/static/images/a.png");

    seventhslide::model::ServiceItem video;
    video.item_type = seventhslide::model::k_item_type_video;
    video.data = seventhslide::model::playback_item_data("clip.mp4", "Clip");
    CHECK_FALSE(first_still_frame(video, ctx, "out-main").has_value());
    // An output it was not laid out for has none.
    CHECK_FALSE(first_still_frame(two_verse_song(), ctx, "out-gone").has_value());
}

TEST_CASE("Export songs as images writes a PNG per slide in a ZIP", "[export][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto song = two_verse_song();
    const auto id = facade.song_repo()->insert(song);

    auto& exporter = *facade.exporter();
    const auto request = exporter.exportSongImages(QVariantList{QVariant::fromValue<qint64>(id)}, {},
                                                   url_of(tmp.path / "songs"));
    const auto result = wait_for_export(exporter, request);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("count")).toInt() == 2);
    // ".zip" is added to a name without an extension.
    const auto file = tmp.path / "songs.zip";
    CHECK(result.value(QStringLiteral("path")).toString().toStdString() == file.string());

    const auto entries = read_zip(read_file(file));
    REQUIRE(names_of(entries)
            == std::vector<std::string>{"01-01 Test Song.png", "01-02 Test Song.png"});
    CHECK(is_png(entries[0].second));
    // Nothing is left beside it.
    CHECK(std::distance(fs::directory_iterator(tmp.path), fs::directory_iterator{}) == 1);
    CHECK_FALSE(exporter.exporting());
}

TEST_CASE("Export a service as images: a folder per output, media left out", "[export][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto hall = facade.session().outputs().front();
    hall.id = "out-hall";
    hall.name = "Hall: West";
    hall.canvas_width = 1280;
    hall.canvas_height = 720;
    auto outputs = facade.session().outputs();
    outputs.push_back(hall);
    facade.set_outputs(outputs);

    const auto service_id = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(service_id > 0);
    auto* repo = facade.service_repo();
    (void)repo->add_item(service_id, seventhslide::model::k_item_type_song,
                         two_verse_song().to_json());
    (void)repo->add_item(service_id, seventhslide::model::k_item_type_divider,
                         nlohmann::json{{"title", "Sermon"}});
    (void)repo->add_item(service_id, seventhslide::model::k_item_type_video,
                         seventhslide::model::playback_item_data("clip.mp4", "Clip"));

    auto& exporter = *facade.exporter();
    const auto file = tmp.path / "Sunday.zip";
    const auto result = wait_for_export(
        exporter, exporter.exportServiceImages(service_id, {}, {}, url_of(file)));
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("count")).toInt() == 4);
    // The video; a divider is no item to leave out.
    CHECK(result.value(QStringLiteral("skipped")).toInt() == 1);

    const auto entries = read_zip(read_file(file));
    const auto main = facade.session().outputs().front().name;
    REQUIRE(names_of(entries)
            == std::vector<std::string>{main + "/01-01 Test Song.png", main + "/01-02 Test Song.png",
                                        "Hall. West/01-01 Test Song.png",
                                        "Hall. West/01-02 Test Song.png"});
    QImage hall_slide;
    REQUIRE(hall_slide.loadFromData(QByteArray::fromStdString(entries[2].second), "PNG"));
    CHECK(hall_slide.size() == QSize(1280, 720));

    // One output: no folder.
    const auto one = tmp.path / "one.zip";
    REQUIRE(wait_for_export(exporter, exporter.exportServiceImages(service_id, {},
                                                                    QStringLiteral("out-hall"),
                                                                    url_of(one)))
                .value(QStringLiteral("ok"))
                .toBool());
    CHECK(names_of(read_zip(read_file(one)))
          == std::vector<std::string>{"01-01 Test Song.png", "01-02 Test Song.png"});
}

TEST_CASE("Export refuses what it cannot do, and says why", "[export][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto& exporter = *facade.exporter();

    const auto no_service = wait_for_export(exporter, exporter.exportServiceImages(
                                                          4242, {}, {}, url_of(tmp.path / "x.zip")));
    CHECK_FALSE(no_service.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(no_service.value(QStringLiteral("errors")).toStringList().isEmpty());

    auto song = two_verse_song();
    const auto id = facade.song_repo()->insert(song);
    const auto gone = wait_for_export(
        exporter, exporter.exportSongImages(QVariantList{QVariant::fromValue<qint64>(id)}, QStringLiteral("nope"),
                                            url_of(tmp.path / "x.zip")));
    CHECK_FALSE(gone.value(QStringLiteral("ok")).toBool());

    const auto no_folder = wait_for_export(
        exporter, exporter.exportSongXml(QVariantList{QVariant::fromValue<qint64>(id)},
                                         url_of(tmp.path / "missing")));
    CHECK_FALSE(no_folder.value(QStringLiteral("ok")).toBool());
    CHECK(std::distance(fs::directory_iterator(tmp.path), fs::directory_iterator{}) == 0);
}

TEST_CASE("Export songs as OpenLyrics files that import as the same songs", "[export][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto song = two_verse_song();
    song.title = "Is It: Well?";
    song.copyright = "Public Domain";
    const auto id = facade.song_repo()->insert(song);
    auto& exporter = *facade.exporter();
    const QVariantList ids{QVariant::fromValue<qint64>(id)};

    REQUIRE(wait_for_export(exporter, exporter.exportSongXml(ids, url_of(tmp.path)))
                .value(QStringLiteral("ok"))
                .toBool());
    // A file already there is never replaced.
    const auto again = wait_for_export(exporter, exporter.exportSongXml(ids, url_of(tmp.path)));
    REQUIRE(again.value(QStringLiteral("ok")).toBool());
    CHECK(again.value(QStringLiteral("count")).toInt() == 1);
    REQUIRE(fs::exists(tmp.path / "Is It. Well.xml"));
    REQUIRE(fs::exists(tmp.path / "Is It. Well 2.xml"));

    const auto back = seventhslide::parse::parse_song_file(tmp.path / "Is It. Well.xml");
    REQUIRE(back.size() == 1);
    CHECK(back[0].title == song.title);
    CHECK(back[0].copyright == song.copyright);
    CHECK(back[0].verse_order == song.verse_order);
    CHECK(seventhslide::parse::labelled_verses(back[0].lyrics)
          == seventhslide::parse::labelled_verses(song.lyrics));
}

TEST_CASE("Export media files under their titles, a folder as a folder", "[export][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir library;
    const seventhslide::test::TempDir out;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    fs::create_directories(library.path / "videos");
    std::ofstream(library.path / "videos" / "a1b2.mp4") << "intro bytes";
    std::ofstream(library.path / "videos" / "c3d4.mp4") << "nested bytes";
    std::ofstream(library.path / "videos" / "e5f6.mp4") << "deep bytes";
    facade.set_media_root(library.path);

    using seventhslide::db::MediaKind;
    auto* media = facade.media_repo();
    media->upsert_file(MediaKind::Video, "a1b2.mp4", "Intro.mp4");
    media->upsert_file(MediaKind::Video, "c3d4.mp4", "Walk in");  // a title without its extension
    media->upsert_file(MediaKind::Video, "e5f6.mp4", "Deep.mp4");
    const auto folder = media->folders(MediaKind::Video).create("Clips");
    const auto inner = media->folders(MediaKind::Video).create("More: Inner", folder);
    (void)media->add_folder_item(MediaKind::Video, folder, "c3d4.mp4");
    (void)media->add_folder_item(MediaKind::Video, inner, "e5f6.mp4");
    // Already in the folder exported to: kept, and the export is numbered.
    std::ofstream(out.path / "Intro.mp4") << "theirs";

    auto& exporter = *facade.exporter();
    const auto result = wait_for_export(
        exporter, exporter.exportMediaFiles(MediaKindQml::Video, {QStringLiteral("a1b2.mp4")},
                                            QVariantList{QVariant::fromValue<qint64>(folder)}, url_of(out.path)));
    INFO(result.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("count")).toInt() == 3);
    CHECK(read_file(out.path / "Intro.mp4") == "theirs");
    CHECK(read_file(out.path / "Intro 2.mp4") == "intro bytes");
    CHECK(read_file(out.path / "Clips" / "Walk in.mp4") == "nested bytes");
    CHECK(read_file(out.path / "Clips" / "More. Inner" / "Deep.mp4") == "deep bytes");
    // No staging file is left.
    for (const auto& entry : fs::recursive_directory_iterator(out.path)) {
        CHECK(entry.path().filename().string().front() != '.');
    }
}
