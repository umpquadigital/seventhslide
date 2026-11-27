// Convert, end to end in a library: a file read, written to a staging name,
// and taken in beside its source or in its place. Scripts stand in for
// ffprobe and ffmpeg; one test runs the real ones where they are installed.

#include <catch2/catch_test_macros.hpp>

#include "convert_controller.hpp"
#include "convert_text.hpp"
#include "facade_all.hpp"
#include "library_controller.hpp"
#include "media_controller.hpp"
#include "media_import_helpers.hpp"
#include "test_qt_app.hpp"
#include "fake_tool.hpp"
#include "test_temp_dir.hpp"
#include "tools_controller.hpp"

#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>

#include <QImage>
#include <QProcess>
#include <QUrl>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace MediaKindQml = seventhslide::app::MediaKindQml;
namespace fs = std::filesystem;
using seventhslide::app::ConvertController;
using seventhslide::app::ConvertText;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;
using seventhslide::test::wait_for_import;

namespace {

constexpr const char* k_probe_json = R"({"streams":[
  {"codec_type":"video","codec_name":"h264","width":1920,"height":1080,"avg_frame_rate":"30/1"},
  {"codec_type":"audio","codec_name":"aac"}],
  "format":{"duration":"2.0","size":"1000","bit_rate":"4000"}})";

void write_script(const fs::path& path, const std::string& body)
{
    if (!seventhslide::test::fake_tools_runnable()) {
        SKIP("no sh to run a stand-in tool with");
    }
    (void)seventhslide::test::write_fake_tool(path.parent_path(), path.filename().string(), body);
}

/// A fake ffprobe that prints ``k_probe_json``, and a fake ffmpeg that reports
/// its progress and writes ``converted`` to its last argument (after
/// checking it was told the format with -f). ``ffmpeg_body`` replaces the
/// fake ffmpeg's body.
seventhslide::platform::ToolSearch fake_tools(const fs::path& dir,
                                              const std::string& ffmpeg_body = {})
{
    write_script(dir / "ffprobe", std::string("cat <<'JSON'\n") + k_probe_json + "\nJSON\n");
    // A version check runs it with -version alone, as for the real one: it
    // answers and writes nothing.
    write_script(dir / "ffmpeg",
                 std::string("[ \"$1\" = -version ] && { echo 'ffmpeg version 9.9-fake'; exit 0; }\n")
                     + (ffmpeg_body.empty()
                     ? "told=no; prev=\n"
                       "for a; do [ \"$prev\" = \"-f\" ] && told=yes; prev=$a; dest=$a; done\n"
                       "[ $told = yes ] || { echo 'no -f' >&2; exit 2; }\n"
                       "echo out_time_us=1000000\necho progress=continue\n"
                       "printf converted > \"$dest\"\necho progress=end\n"
                     : ffmpeg_body));
    seventhslide::platform::ToolSearch search;
    search.platform_id = seventhslide::test::tool_platform();
    search.path_env = dir.string();
    return search;
}

struct Library {
    /// First: the facade measures text as it is built.
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    SessionFacade facade;
    std::string clip;

    explicit Library(const std::string& ffmpeg_body = {})
    {
        seventhslide::test::ensure_qt_application();
        REQUIRE(facade.open_database(dir.path / "library.db"));
        facade.tools()->set_search(fake_tools(dir.path / "bin", ffmpeg_body));
        const fs::path src = dir.path / "clip.mp4";
        std::ofstream{src} << "source bytes";
        const auto result = wait_for_import(
            *facade.media(),
            facade.media()->import_files(MediaKind::Video,
                                         {QString::fromStdString(src.string())}, -1));
        REQUIRE(result.value(QStringLiteral("ok")).toBool());
        clip = result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    }

    [[nodiscard]] fs::path videos() const { return facade.media_root() / "videos"; }

    QVariantMap convert(const QVariantMap& choice, bool replace, qint64 folder = -1,
                        const QString& name = {})
    {
        auto& convert = *facade.convert();
        QVariantMap result;
        qint64 want = 0;
        bool done = false;
        const auto c = QObject::connect(&convert, &ConvertController::convertFinished,
                                        [&](qint64 id, const QVariantMap& r) {
                                            if (id == want) {
                                                result = r;
                                                done = true;
                                            }
                                        });
        want = convert.convertMedia(MediaKindQml::Video, {QString::fromStdString(clip)}, choice,
                                    folder, replace, name);
        (void)QTest::qWaitFor([&] { return done; }, 20000);
        QObject::disconnect(c);
        REQUIRE(done);
        return result;
    }

    /// Files in the videos folder, staging names included.
    [[nodiscard]] std::vector<std::string> on_disk() const
    {
        std::vector<std::string> out;
        for (const auto& e : fs::directory_iterator(videos())) {
            out.push_back(e.path().filename().string());
        }
        return out;
    }
};

bool any_staging(const std::vector<std::string>& names)
{
    for (const auto& n : names) {
        if (n.starts_with(".import-")) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("A conversion lands beside its source, titled by what changed", "[app][convert]")
{
    Library lib;
    const auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("webm")},
                                     {QStringLiteral("height"), 720}},
                                    false);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(" | ").toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 1);
    const std::string made = names.front().toStdString();
    CHECK(made.ends_with(".webm"));
    CHECK(made != lib.clip);

    auto& repo = *lib.facade.media()->media_repo();
    const auto file = repo.get_file(MediaKind::Video, made);
    REQUIRE(file.has_value());
    CHECK(file->title() == "clip (720p, WebM).webm");
    // The source is untouched, and nothing is left half made.
    REQUIRE(repo.get_file(MediaKind::Video, lib.clip).has_value());
    CHECK(fs::exists(lib.videos() / lib.clip));
    std::ifstream in{lib.videos() / made};
    std::string bytes;
    in >> bytes;
    CHECK(bytes == "converted");
    CHECK_FALSE(any_staging(lib.on_disk()));
    CHECK(lib.facade.media()->browser(MediaKind::Video)->rowCount() == 2);
    CHECK_FALSE(lib.facade.convert()->converting());
}

TEST_CASE("With Replace original the new file takes the source's place everywhere",
          "[app][convert]")
{
    Library lib;
    auto& media = *lib.facade.media();
    REQUIRE(lib.facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(media.addVideoToService(QString::fromStdString(lib.clip)));
    const qint64 folder = media.library(MediaKindQml::Video)->createFolder(QStringLiteral("Clips"));
    REQUIRE(folder > 0);
    REQUIRE(media.library(MediaKindQml::Video)->moveItems({QString::fromStdString(lib.clip)}, folder)
                .isEmpty());

    const auto result =
        lib.convert({{QStringLiteral("format"), QStringLiteral("webm")}}, true);
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const std::string made =
        result.value(QStringLiteral("filenames")).toStringList().front().toStdString();

    auto& repo = *media.media_repo();
    // The original is gone: its row and its bytes.
    CHECK_FALSE(repo.get_file(MediaKind::Video, lib.clip).has_value());
    CHECK_FALSE(fs::exists(lib.videos() / lib.clip));
    // The new one has its title (with the new extension) and its folder.
    REQUIRE(repo.get_file(MediaKind::Video, made).has_value());
    CHECK(repo.get_file(MediaKind::Video, made)->title() == "clip.webm");
    const auto items = repo.list_folder_items(MediaKind::Video, folder);
    REQUIRE(items.size() == 1);
    CHECK(items[0].filename == made);
    // The service plays it, in the database and in the open rundown.
    CHECK(repo.count_playback_refs(MediaKind::Video, made) == 1);
    CHECK(repo.count_playback_refs(MediaKind::Video, lib.clip) == 0);
    const auto& rundown = lib.facade.session().service_items();
    REQUIRE(rundown.size() == 1);
    CHECK(rundown[0].data["filename"] == made);
    CHECK_FALSE(any_staging(lib.on_disk()));
}

TEST_CASE("A video made into sound lands in Audio, in the folder given", "[app][convert]")
{
    Library lib;
    auto& media = *lib.facade.media();
    const qint64 folder = media.library(MediaKindQml::Audio)->createFolder(QStringLiteral("Sermons"));
    REQUIRE(folder > 0);
    const auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("m4a")},
                                     {QStringLiteral("codec"), QStringLiteral("copy")}},
                                    false, folder);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(" | ").toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("kind")).toInt() == MediaKindQml::Audio);
    const std::string made =
        result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    CHECK(made.ends_with(".m4a"));
    auto& repo = *media.media_repo();
    REQUIRE(repo.get_file(MediaKind::Audio, made).has_value());
    CHECK(repo.get_file(MediaKind::Audio, made)->title() == "clip (M4A).m4a");
    CHECK(fs::exists(lib.facade.media_root() / "audio" / made));
    const auto items = repo.list_folder_items(MediaKind::Audio, folder);
    REQUIRE(items.size() == 1);
    CHECK(items[0].filename == made);
    // The video is still there, and only it.
    CHECK(repo.list_files(MediaKind::Video).size() == 1);
}

TEST_CASE("Only a file of the same kind can replace the original", "[app][convert]")
{
    Library lib;
    const auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("mp3")}}, true);
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("errors")).toStringList().front().contains(
        QStringLiteral("same kind")));
    CHECK(lib.facade.media()->media_repo()->get_file(MediaKind::Video, lib.clip).has_value());
}

TEST_CASE("A name given for one file is its title", "[app][convert]")
{
    Library lib;
    auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("webm")}}, false, -1,
                              QStringLiteral("  Sunday / Opener  "));
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    std::string made = result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    CHECK(lib.facade.media()->media_repo()->get_file(MediaKind::Video, made)->title()
          == "Sunday   Opener.webm");

    // With Replace original too: the name, not the original's.
    result = lib.convert({{QStringLiteral("format"), QStringLiteral("mkv")}}, true, -1,
                         QStringLiteral("Opener"));
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    made = result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    CHECK(lib.facade.media()->media_repo()->get_file(MediaKind::Video, made)->title()
          == "Opener.mkv");
}

TEST_CASE("A failed conversion leaves nothing behind and says why", "[app][convert]")
{
    Library lib("echo 'Unknown encoder libvpx-vp9' >&2\necho 'more detail' >&2\nexit 1\n");
    const auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("webm")}}, true);
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("count")).toInt() == 0);
    const auto errors = result.value(QStringLiteral("errors")).toStringList();
    REQUIRE(errors.size() == 1);
    CHECK(errors.front() == QStringLiteral("clip.mp4: Unknown encoder libvpx-vp9"));
    // With Replace original on, the original is still all there.
    CHECK(lib.facade.media()->media_repo()->get_file(MediaKind::Video, lib.clip).has_value());
    CHECK(fs::exists(lib.videos() / lib.clip));
    CHECK(lib.on_disk().size() == 1);
}

TEST_CASE("A cancelled conversion stops, reports, and leaves nothing behind", "[app][convert]")
{
    // Writes some of its file, then waits to be stopped.
    Library lib("for a; do dest=$a; done\nprintf partial > \"$dest\"\n"
                "echo out_time_us=500000\nsleep 30\n");
    auto& convert = *lib.facade.convert();
    QVariantMap result;
    bool done = false;
    QObject::connect(&convert, &ConvertController::convertFinished,
                     [&](qint64, const QVariantMap& r) {
                         result = r;
                         done = true;
                     });
    convert.convertMedia(MediaKindQml::Video, {QString::fromStdString(lib.clip)},
                         {{QStringLiteral("format"), QStringLiteral("mkv")}}, -1, false);
    REQUIRE(convert.converting());
    REQUIRE(QTest::qWaitFor(
        [&] {
            const auto jobs = convert.jobs();
            return !jobs.isEmpty() && jobs[0].toMap().value(QStringLiteral("progress")).toDouble() > 0;
        },
        seventhslide::test::k_tool_wait_ms));
    const auto job = convert.jobs()[0].toMap();
    CHECK(job.value(QStringLiteral("running")).toBool());
    CHECK(job.value(QStringLiteral("title")).toString() == QStringLiteral("clip (MKV).mkv"));
    CHECK(job.value(QStringLiteral("progress")).toDouble() == 0.25);
    CHECK(convert.progress() == 0.25);
    REQUIRE(any_staging(lib.on_disk()));

    convert.cancelConversion(job.value(QStringLiteral("id")).toLongLong());
    REQUIRE(QTest::qWaitFor([&] { return done; }, seventhslide::test::k_tool_wait_ms));
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("errors")).toStringList().front()
          == QStringLiteral("clip.mp4: Stopped"));
    CHECK_FALSE(convert.converting());
    CHECK(lib.on_disk().size() == 1);
}

TEST_CASE("Convert refuses what it cannot do, from the event loop", "[app][convert]")
{
    Library lib;
    auto& convert = *lib.facade.convert();
    QSignalSpy finished(&convert, &ConvertController::convertFinished);
    convert.convertMedia(MediaKindQml::Video, {QString::fromStdString(lib.clip)},
                         {{QStringLiteral("format"), QStringLiteral("avi")}}, -1, false);
    CHECK(finished.isEmpty());
    REQUIRE(finished.wait(seventhslide::test::k_tool_wait_ms));
    CHECK_FALSE(finished[0][1].toMap().value(QStringLiteral("ok")).toBool());

    // Without the tools, it says where to look.
    seventhslide::platform::ToolSearch nowhere;
    nowhere.platform_id = "linux";
    lib.facade.tools()->set_search(nowhere);
    convert.convertMedia(MediaKindQml::Video, {QString::fromStdString(lib.clip)},
                         {{QStringLiteral("format"), QStringLiteral("mkv")}}, -1, false);
    REQUIRE(finished.wait(seventhslide::test::k_tool_wait_ms));
    const auto errors = finished[1][1].toMap().value(QStringLiteral("errors")).toStringList();
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().contains(QStringLiteral("Settings")));
}

TEST_CASE("probeMedia reads a library file for the dialog", "[app][convert]")
{
    Library lib;
    auto& convert = *lib.facade.convert();
    QSignalSpy probed(&convert, &ConvertController::probeFinished);
    const qint64 id = convert.probeMedia(MediaKindQml::Video, QString::fromStdString(lib.clip));
    REQUIRE(probed.wait(seventhslide::test::k_tool_wait_ms));
    CHECK(probed[0][0].toLongLong() == id);
    const auto map = probed[0][1].toMap();
    CHECK(map.value(QStringLiteral("ok")).toBool());
    CHECK(map.value(QStringLiteral("height")).toInt() == 1080);
    CHECK(map.value(QStringLiteral("fps")).toDouble() == 30.0);
    CHECK(map.value(QStringLiteral("duration")).toDouble() == 2.0);
    CHECK(map.value(QStringLiteral("hasAudio")).toBool());
    // As people call them, for the dialog's card.
    CHECK(map.value(QStringLiteral("videoCodec")).toString() == QStringLiteral("H.264"));
    CHECK(map.value(QStringLiteral("audioCodec")).toString() == QStringLiteral("AAC"));
    // A video's frame, saved for the card.
    const QUrl frame(map.value(QStringLiteral("frame")).toString());
    REQUIRE(frame.isLocalFile());
    CHECK(fs::exists(frame.toLocalFile().toStdString()));
}

TEST_CASE("The dialog's formats are ones a choice reads", "[app][convert]")
{
    for (const QString& kind :
         {QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("image")}) {
        const QVariantList formats = ConvertText::formats(kind);
        REQUIRE_FALSE(formats.isEmpty());
        for (const QVariant& format : formats) {
            const QString value = format.toMap().value(QStringLiteral("value")).toString();
            INFO(value.toStdString());
            const QString extension = QStringLiteral(".") + value;
            CHECK(ConvertText::extension(kind, {{QStringLiteral("format"), value}}) == extension);
        }
    }
    const QVariantMap jpeg = ConvertText::formats(QStringLiteral("image")).front().toMap();
    CHECK(jpeg.value(QStringLiteral("value")).toString() == QStringLiteral("jpg"));
    CHECK(jpeg.value(QStringLiteral("label")).toString() == QStringLiteral("JPEG"));
    CHECK(ConvertText::formats(QStringLiteral("text")).isEmpty());
}

TEST_CASE("The real ffmpeg converts a library video", "[app][convert]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::platform::Paths paths;
    const auto search = seventhslide::platform::ToolSearch::from(paths, {});
    const auto ffmpeg = seventhslide::platform::find_tool("ffmpeg", search);
    if (!ffmpeg || !seventhslide::platform::find_tool("ffprobe", search)) {
        SKIP("no ffmpeg and ffprobe here");
    }
    Library lib;
    lib.facade.tools()->set_search(search);
    // Put a real second of video where the fake bytes were.
    QProcess make;
    make.start(QString::fromStdString(ffmpeg->path.string()),
               {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                QStringLiteral("testsrc=duration=1:size=320x240:rate=10"),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-f"),
                QStringLiteral("mp4"), QString::fromStdString((lib.videos() / lib.clip).string())});
    REQUIRE(make.waitForFinished(30000));
    REQUIRE(make.exitCode() == 0);

    // Read for the dialog: its codec, and a real frame of it.
    QSignalSpy probed(lib.facade.convert(), &ConvertController::probeFinished);
    lib.facade.convert()->probeMedia(MediaKindQml::Video, QString::fromStdString(lib.clip));
    REQUIRE(probed.wait(30000));
    const auto probe = probed[0][1].toMap();
    CHECK(probe.value(QStringLiteral("videoCodec")).toString() == QStringLiteral("H.264"));
    const QUrl frame(probe.value(QStringLiteral("frame")).toString());
    REQUIRE(frame.isLocalFile());
    const QImage picture(frame.toLocalFile());
    CHECK(picture.height() == 180);

    const auto result = lib.convert({{QStringLiteral("format"), QStringLiteral("mkv")},
                                     {QStringLiteral("height"), 120}},
                                    false);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(" | ").toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const std::string made =
        result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    CHECK(fs::file_size(lib.videos() / made) > 0);
    CHECK(lib.facade.media()->media_repo()->get_file(MediaKind::Video, made)->title()
          == "clip (120p, MKV).mkv");
}
