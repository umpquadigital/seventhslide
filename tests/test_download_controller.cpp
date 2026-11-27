// Download from the internet, end to end in a library: a link looked up,
// downloaded into a staging folder, and its files taken in. A script stands
// in for yt-dlp, so nothing here goes online.

#include <catch2/catch_test_macros.hpp>

#include "download_controller.hpp"
#include "facade_all.hpp"
#include "media_controller.hpp"
#include "test_qt_app.hpp"
#include "fake_tool.hpp"
#include "test_temp_dir.hpp"
#include "tools_controller.hpp"
#include "ytdlp_updater.hpp"

#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/platform/tools.hpp>

#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <fstream>
#include <string>

namespace MediaKindQml = seventhslide::app::MediaKindQml;
namespace fs = std::filesystem;
using seventhslide::app::DownloadController;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;

namespace {

/// A yt-dlp that answers --version, describes a video for -J, and otherwise
/// writes a file into the folder -o names and says so. ``body`` replaces the
/// download part.
std::string fake_ytdlp(const std::string& body = {})
{
    return std::string(
               "case \" $* \" in *\" --version \"*) echo 2026.10.01; exit 0;; esac\n"
               "for a; do [ \"$a\" = -J ] && { cat <<'JSON'\n"
               R"({"id": "abc", "title": "Sunday Highlights", "uploader": "Grace Church",)"
               R"( "duration": 245, "formats": [{"vcodec": "avc1", "height": 1080},)"
               R"( {"vcodec": "avc1", "height": 720}, {"vcodec": "none"}]})"
               "\nJSON\nexit 0; }; done\n"
               "prev=; out=; ext=mp4\n"
               "for a; do [ \"$prev\" = -o ] && out=$a; [ \"$a\" = -x ] && ext=m4a; prev=$a; done\n"
               "dir=$(dirname \"$out\")\n")
           + (body.empty() ? "printf media > \"$dir/abc.$ext\"\n"
                             "echo 'SSPROG 50 100 NA NA NA NA'\n"
                             "printf 'SSFILE\\tSunday Highlights\\t%s\\n' \"$dir/abc.$ext\"\n"
                           : body);
}

struct Library {
    /// First: the facade measures text as it is built.
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    SessionFacade facade;

    /// yt-dlp installed as the system's (or, with ``managed``, as the app's
    /// own copy, freshly checked so no test goes online).
    explicit Library(const std::string& script = fake_ytdlp(), bool managed = false)
    {
        seventhslide::test::ensure_qt_application();
        REQUIRE(facade.open_database(dir.path / "library.db"));
        if (!seventhslide::test::fake_tools_runnable()) {
            SKIP("no sh to run a stand-in yt-dlp with");
        }
        const fs::path where = managed ? dir.path / "tools" : dir.path / "bin";
        (void)seventhslide::test::write_fake_tool(where, "yt-dlp", script);
        seventhslide::platform::ToolSearch search;
        search.platform_id = seventhslide::test::tool_platform();
        search.path_env = (dir.path / "bin").string();
        search.tools_dir = dir.path / "tools";
        facade.tools()->set_search(search);
        auto& updater = *facade.tools()->ytdlp();
        updater.set_release_base(QUrl::fromLocalFile(
            QString::fromStdString((dir.path / "no-release").string()) + QStringLiteral("/")));
        QSettings().setValue(QStringLiteral("tools/ytdlpChecked"), QDateTime::currentDateTimeUtc());
    }

    QVariantMap lookup(const QString& url)
    {
        QSignalSpy spy(facade.download(), &DownloadController::lookupFinished);
        const qint64 id = facade.download()->lookupUrl(url);
        REQUIRE(spy.wait(seventhslide::test::k_tool_wait_ms));
        CHECK(spy[0][0].toLongLong() == id);
        return spy[0][1].toMap();
    }

    QVariantMap download(const QVariantMap& options, qint64 video_folder = -1,
                         qint64 audio_folder = -1)
    {
        QSignalSpy spy(facade.download(), &DownloadController::downloadFinished);
        const qint64 id = facade.download()->downloadUrl(QStringLiteral("https://v.example/abc"),
                                                         options, QStringLiteral("Sunday"),
                                                         video_folder, audio_folder);
        REQUIRE(spy.wait(seventhslide::test::k_tool_wait_ms));
        CHECK(spy[0][0].toLongLong() == id);
        return spy[0][1].toMap();
    }

    [[nodiscard]] bool any_staging(MediaKind kind) const
    {
        const fs::path media = facade.media_root() / seventhslide::db::media_subdir(kind);
        if (!fs::exists(media)) {
            return false;
        }
        for (const auto& e : fs::directory_iterator(media)) {
            if (e.path().filename().string().starts_with(".import-")) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("A link is looked up for its title and the heights it comes in", "[app][download]")
{
    Library lib;
    const auto info = lib.lookup(QStringLiteral("  https://v.example/abc  "));
    CHECK(info.value(QStringLiteral("ok")).toBool());
    CHECK(info.value(QStringLiteral("title")).toString() == QStringLiteral("Sunday Highlights"));
    CHECK(info.value(QStringLiteral("uploader")).toString() == QStringLiteral("Grace Church"));
    CHECK(info.value(QStringLiteral("duration")).toDouble() == 245.0);
    CHECK(info.value(QStringLiteral("heights")).toList() == QVariantList{1080, 720});
    CHECK(info.value(QStringLiteral("hasVideo")).toBool());
    CHECK_FALSE(info.value(QStringLiteral("playlist")).toBool());
}

TEST_CASE("A video lands in Videos, in the folder showing, titled as the site titles it",
          "[app][download]")
{
    Library lib;
    auto& media = *lib.facade.media();
    const qint64 folder = media.library(MediaKindQml::Video)->createFolder(QStringLiteral("Clips"));
    REQUIRE(folder > 0);
    const auto result = lib.download({{QStringLiteral("maxHeight"), 720}}, folder);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(" | ").toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 1);
    const std::string made = names.front().toStdString();
    CHECK(made.ends_with(".mp4"));
    const auto file = media.media_repo()->get_file(MediaKind::Video, made);
    REQUIRE(file.has_value());
    CHECK(file->title() == "Sunday Highlights.mp4");
    const auto items = media.media_repo()->list_folder_items(MediaKind::Video, folder);
    REQUIRE(items.size() == 1);
    CHECK(items[0].filename == made);
    CHECK_FALSE(lib.any_staging(MediaKind::Video));
    CHECK_FALSE(lib.facade.download()->downloading());
}

TEST_CASE("Sound alone lands in Audio", "[app][download]")
{
    Library lib;
    const auto result = lib.download({{QStringLiteral("audioOnly"), true}});
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const std::string made =
        result.value(QStringLiteral("filenames")).toStringList().front().toStdString();
    CHECK(made.ends_with(".m4a"));
    auto& repo = *lib.facade.media()->media_repo();
    REQUIRE(repo.get_file(MediaKind::Audio, made).has_value());
    CHECK(repo.get_file(MediaKind::Audio, made)->title() == "Sunday Highlights.m4a");
    CHECK(repo.list_files(MediaKind::Video).empty());
    CHECK_FALSE(lib.any_staging(MediaKind::Audio));
}

TEST_CASE("Only files in the download's own folder are taken in", "[app][download]")
{
    const seventhslide::test::TempDir elsewhere;
    const fs::path outside = elsewhere.path / "secret.mp4";
    std::ofstream{outside} << "not yours";
    Library lib(fake_ytdlp("printf 'SSFILE\\tSneaky\\t%s\\n' '" + outside.string() + "'\n"));
    const auto result = lib.download({});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("errors")).toStringList()
          == QStringList{QStringLiteral("yt-dlp finished without a file")});
    CHECK(lib.facade.media()->media_repo()->list_files(MediaKind::Video).empty());
    // Left where it was.
    CHECK(fs::exists(outside));
}

TEST_CASE("A failure says what yt-dlp said, and suggests an update only for the app's own copy",
          "[app][download]")
{
    const std::string fail =
        "echo 'WARNING: something minor' >&2\n"
        "echo 'ERROR: [youtube] abc: Unable to extract uploader id' >&2\nexit 1\n";
    {
        Library lib(fake_ytdlp(fail));
        const auto result = lib.download({});
        CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
        CHECK(result.value(QStringLiteral("errors")).toStringList()
              == QStringList{QStringLiteral("[youtube] abc: Unable to extract uploader id")});
        CHECK_FALSE(result.value(QStringLiteral("updateSuggested")).toBool());
        CHECK_FALSE(lib.any_staging(MediaKind::Video));
    }
    {
        Library lib(fake_ytdlp(fail), true);
        REQUIRE(lib.facade.tools()->ytdlp()->installed());
        const auto result = lib.download({});
        CHECK(result.value(QStringLiteral("updateSuggested")).toBool());
    }
}

TEST_CASE("Without yt-dlp, a lookup says so", "[app][download]")
{
    Library lib;
    seventhslide::platform::ToolSearch nowhere;
    nowhere.platform_id = "linux";
    lib.facade.tools()->set_search(nowhere);
    const auto info = lib.lookup(QStringLiteral("https://v.example/abc"));
    CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
    CHECK(info.value(QStringLiteral("missing")).toBool());
    CHECK(info.value(QStringLiteral("error")).toString().contains(QStringLiteral("Settings")));
}

TEST_CASE("A download can be stopped, and leaves nothing behind", "[app][download]")
{
    Library lib(fake_ytdlp("printf part > \"$dir/abc.mp4.part\"\n"
                           "echo 'SSPROG 25 100 NA NA NA NA'\nsleep 30\n"));
    auto& download = *lib.facade.download();
    QSignalSpy spy(&download, &DownloadController::downloadFinished);
    download.downloadUrl(QStringLiteral("https://v.example/abc"), {}, QStringLiteral("Sunday"),
                         -1, -1);
    REQUIRE(QTest::qWaitFor([&download] { return download.progress() > 0.0; }, seventhslide::test::k_tool_wait_ms));
    const auto job = download.jobs()[0].toMap();
    CHECK(job.value(QStringLiteral("title")).toString() == QStringLiteral("Sunday"));
    CHECK(job.value(QStringLiteral("running")).toBool());
    CHECK(job.value(QStringLiteral("progress")).toDouble() == 0.25);
    REQUIRE(lib.any_staging(MediaKind::Video));

    download.cancelDownload(job.value(QStringLiteral("id")).toLongLong());
    REQUIRE(spy.wait(seventhslide::test::k_tool_wait_ms));
    CHECK(spy[0][1].toMap().value(QStringLiteral("errors")).toStringList()
          == QStringList{QStringLiteral("Stopped")});
    CHECK_FALSE(lib.any_staging(MediaKind::Video));
    CHECK_FALSE(download.downloading());
}
