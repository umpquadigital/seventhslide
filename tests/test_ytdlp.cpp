// Downloading with yt-dlp: what a link holds, the command for each choice,
// its progress and files, and the release the app keeps. Pure, but for one
// run of the real yt-dlp on a file:// link where it is installed.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/media/ytdlp.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>

#include <QProcess>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace seventhslide::media;

namespace {

bool has(const std::vector<std::string>& args, std::string_view a, std::string_view b)
{
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == a && args[i + 1] == b) {
            return true;
        }
    }
    return false;
}

bool has(const std::vector<std::string>& args, std::string_view a)
{
    return std::find(args.begin(), args.end(), a) != args.end();
}

} // namespace

TEST_CASE("A video's description reads its title and the heights it comes in", "[ytdlp]")
{
    const auto info = parse_ytdlp_info(R"({
        "id": "abc", "title": "Sunday Highlights", "uploader": "Grace Church",
        "duration": 245.0, "thumbnail": "https://i.example/abc.jpg",
        "formats": [
            {"format_id": "140", "vcodec": "none", "acodec": "mp4a.40.2"},
            {"format_id": "137", "vcodec": "avc1", "acodec": "none", "height": 1080},
            {"format_id": "136", "vcodec": "avc1", "acodec": "none", "height": 720},
            {"format_id": "248", "vcodec": "vp9", "acodec": "none", "height": 1080},
            {"format_id": "sb0", "vcodec": "none", "height": 90}
        ]})");
    REQUIRE(info.has_value());
    CHECK_FALSE(info->playlist);
    CHECK(info->title == "Sunday Highlights");
    CHECK(info->uploader == "Grace Church");
    CHECK(info->duration_s == 245.0);
    CHECK(info->thumbnail == "https://i.example/abc.jpg");
    CHECK(info->heights == std::vector<int>{1080, 720});
    CHECK(info->has_video);

    // Sound alone: no picture to choose.
    const auto song = parse_ytdlp_info(
        R"({"id": "s", "title": "Hymn", "channel": "Choir",
            "formats": [{"vcodec": "none", "acodec": "opus"}]})");
    REQUIRE(song.has_value());
    CHECK(song->uploader == "Choir");
    CHECK(song->heights.empty());
    CHECK_FALSE(song->has_video);

    // A site that gives one file, with its height on the top level.
    const auto direct = parse_ytdlp_info(R"({"id": "d", "title": "Clip", "height": 480})");
    REQUIRE(direct.has_value());
    CHECK(direct->heights == std::vector<int>{480});

    CHECK_FALSE(parse_ytdlp_info("not json").has_value());
    CHECK_FALSE(parse_ytdlp_info("[]").has_value());
    CHECK_FALSE(parse_ytdlp_info(R"({"other": 1})").has_value());
}

TEST_CASE("A playlist's description lists its items", "[ytdlp]")
{
    const auto info = parse_ytdlp_info(R"({
        "_type": "playlist", "id": "PL1", "title": "Easter 2026",
        "entries": [{"title": "Prelude", "duration": 60}, {"title": "Sermon"}, 7]})");
    REQUIRE(info.has_value());
    CHECK(info->playlist);
    CHECK(info->title == "Easter 2026");
    REQUIRE(info->entries.size() == 2);
    CHECK(info->entries[0] == YtdlpInfo::Entry{"Prelude", 60.0});
    CHECK(info->entries[1].title == "Sermon");
    CHECK(info->heights.empty());
}

TEST_CASE("The link always follows --, and no config file is read", "[ytdlp]")
{
    const auto lookup = ytdlp_lookup_args("--exec rm -rf ~");
    REQUIRE(lookup.size() >= 2);
    CHECK(lookup[lookup.size() - 2] == "--");
    CHECK(lookup.back() == "--exec rm -rf ~");
    CHECK(has(lookup, "--ignore-config"));
    CHECK(has(lookup, "-J"));
    CHECK(has(lookup, "--flat-playlist"));

    const auto download = ytdlp_download_args("-o /etc/passwd", DownloadChoice{}, "/m/.x", {});
    CHECK(download[download.size() - 2] == "--");
    CHECK(download.back() == "-o /etc/passwd");
    CHECK(has(download, "--ignore-config"));
    // The only -o is the app's own.
    CHECK(std::count(download.begin(), download.end(), std::string{"-o"}) == 1);
}

TEST_CASE("The download command follows the choice", "[ytdlp]")
{
    DownloadChoice video;
    video.max_height = 720;
    auto args = ytdlp_download_args("https://v.example/a", video, "/m/.import-x.part",
                                    "/usr/bin/ffmpeg");
    CHECK(has(args, "-o", (fs::path{"/m/.import-x.part"} / "%(id)s.%(ext)s").string()));
    CHECK(has(args, "-f", "bv*+ba/b"));
    CHECK(has(args, "-S", "res:720,vcodec:h264,acodec:aac"));
    CHECK(has(args, "--merge-output-format", "mp4"));
    CHECK(has(args, "--ffmpeg-location", "/usr/bin/ffmpeg"));
    CHECK(has(args, "--no-playlist"));
    CHECK(has(args, "--no-simulate"));
    CHECK(has(args, "--progress"));
    CHECK_FALSE(has(args, "-x"));

    video.max_height = 0;
    video.playlist = true;
    args = ytdlp_download_args("https://v.example/a", video, "/m/x", {});
    CHECK(has(args, "-S", "res,vcodec:h264,acodec:aac"));
    CHECK(has(args, "--yes-playlist"));
    CHECK_FALSE(has(args, "--ffmpeg-location"));

    DownloadChoice audio;
    audio.audio_only = true;
    args = ytdlp_download_args("https://v.example/a", audio, "/m/x", {});
    CHECK(has(args, "-f", "ba[ext=m4a]/ba/b"));
    CHECK(has(args, "--audio-format", "m4a"));
    CHECK(has(args, "-x"));
    CHECK_FALSE(has(args, "--merge-output-format"));
}

TEST_CASE("The container and the sound's format are the ones asked for, and only those",
          "[ytdlp]")
{
    DownloadChoice video;
    video.container = "webm";
    video.max_height = 1080;
    auto args = ytdlp_download_args("https://v.example/a", video, "/m/x", {});
    CHECK(has(args, "--merge-output-format", "webm"));
    CHECK(has(args, "-S", "res:1080,vcodec:vp9,acodec:opus"));
    video.container = "mkv";
    args = ytdlp_download_args("https://v.example/a", video, "/m/x", {});
    CHECK(has(args, "--merge-output-format", "mkv"));
    CHECK(has(args, "-S", "res:1080"));
    video.container = "avi; rm -rf ~";
    args = ytdlp_download_args("https://v.example/a", video, "/m/x", {});
    CHECK(has(args, "--merge-output-format", "mp4"));

    DownloadChoice audio;
    audio.audio_only = true;
    audio.audio_format = "opus";
    args = ytdlp_download_args("https://v.example/a", audio, "/m/x", {});
    CHECK(has(args, "-f", "ba[acodec=opus]/ba/b"));
    CHECK(has(args, "--audio-format", "opus"));
    audio.audio_format = "flac";
    args = ytdlp_download_args("https://v.example/a", audio, "/m/x", {});
    CHECK(has(args, "-f", "ba/b"));
    CHECK(has(args, "--audio-format", "flac"));
    audio.audio_format = "aiff";
    CHECK(has(ytdlp_download_args("https://v.example/a", audio, "/m/x", {}), "--audio-format",
              "m4a"));
}

TEST_CASE("Progress moves forward through picture, sound and a playlist's items", "[ytdlp]")
{
    YtdlpProgress progress;
    CHECK_FALSE(progress.read("[download] Destination: x.mp4").has_value());
    CHECK_FALSE(progress.read("SSPROG NA NA NA NA NA NA").has_value());

    // One video: 800 bytes of picture then 200 of sound, 1000 in all.
    CHECK(progress.read("SSPROG 400 800 NA 1000 NA NA") == 0.4);
    CHECK(progress.read("SSPROG 800 800 NA 1000 NA NA") == 0.8);
    CHECK(progress.read("SSPROG 100 200 NA 1000 NA NA") == 0.9);
    CHECK(progress.read("SSPROG 200 200 NA 1000 NA NA") == 1.0);

    // A playlist of 4, the second item half done.
    YtdlpProgress list;
    CHECK(list.read("SSPROG 50 100 NA NA 1 4") == 0.125);
    CHECK(list.read("SSPROG 50 100 NA NA 2 4") == 0.375);

    // No total for the item: each stream on its own, but never backwards.
    YtdlpProgress unsure;
    CHECK(unsure.read("SSPROG 90 NA 100 NA NA NA") == 0.9);
    CHECK(unsure.read("SSPROG 10 NA 100 NA NA NA") == 0.9);
}

TEST_CASE("A finished file's line gives its title and path", "[ytdlp]")
{
    const auto file = parse_ytdlp_file("SSFILE\tSunday\tHighlights\t/m/.x/abc.mp4");
    REQUIRE(file.has_value());
    CHECK(file->first == "Sunday\tHighlights");
    CHECK(file->second == fs::path{"/m/.x/abc.mp4"});
    CHECK_FALSE(parse_ytdlp_file("SSFILE\tno path").has_value());
    CHECK_FALSE(parse_ytdlp_file("SSFILE\tTitle\t").has_value());
    CHECK_FALSE(parse_ytdlp_file("[download] 100%").has_value());
}

TEST_CASE("Errors that a newer yt-dlp usually fixes are told apart", "[ytdlp]")
{
    CHECK(ytdlp_error_suggests_update("ERROR: [youtube] abc: Unable to extract uploader id"));
    CHECK(ytdlp_error_suggests_update("ERROR: nsig extraction failed: some formats may be missing"));
    CHECK(ytdlp_error_suggests_update(
        "please report this issue on https://github.com/yt-dlp/yt-dlp/issues"));
    CHECK_FALSE(ytdlp_error_suggests_update("ERROR: [Errno -2] Name or service not known"));
    CHECK_FALSE(ytdlp_error_suggests_update("ERROR: Private video. Sign in if you've been granted access"));
}

TEST_CASE("The release file and its checksum are picked for the platform", "[ytdlp]")
{
    CHECK(ytdlp_release_asset("linux", "x86_64") == "yt-dlp_linux");
    CHECK(ytdlp_release_asset("linux", "aarch64") == "yt-dlp_linux_aarch64");
    CHECK(ytdlp_release_asset("linux", "arm64") == "yt-dlp_linux_aarch64");
    CHECK(ytdlp_release_asset("macos", "arm64") == "yt-dlp_macos");
    CHECK(ytdlp_release_asset("windows", "x86_64") == "yt-dlp.exe");
    CHECK(ytdlp_release_asset("windows", "arm64") == "yt-dlp_arm64.exe");
    CHECK(ytdlp_release_asset("plan9", "x86_64").empty());

    const std::string a(64, 'a');
    const std::string b = std::string(63, 'B') + "c";
    const auto sums = parse_sha256sums(a + "  yt-dlp\n" + b + " *yt-dlp_linux\r\n"
                                       + "short  yt-dlp_macos\n\n");
    REQUIRE(sums.size() == 2);
    CHECK(sums.at("yt-dlp") == a);
    CHECK(sums.at("yt-dlp_linux") == std::string(63, 'b') + "c");
    CHECK_FALSE(sums.contains("yt-dlp_macos"));
}

TEST_CASE("The real yt-dlp accepts the commands", "[ytdlp]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::platform::Paths paths;
    const auto search = seventhslide::platform::ToolSearch::from(paths, {});
    const auto ytdlp = seventhslide::platform::find_tool("yt-dlp", search);
    const auto ffmpeg = seventhslide::platform::find_tool("ffmpeg", search);
    if (!ytdlp || !ffmpeg) {
        SKIP("no yt-dlp and ffmpeg here");
    }
    const auto run = [](const fs::path& program, const std::vector<std::string>& args,
                        std::string* out) {
        QProcess p;
        QStringList list;
        for (const auto& a : args) {
            list << QString::fromStdString(a);
        }
        p.start(QString::fromStdString(program.string()), list);
        REQUIRE(p.waitForFinished(120000));
        *out = p.readAllStandardOutput().toStdString();
        INFO(p.readAllStandardError().toStdString());
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    };
    const seventhslide::test::TempDir tmp;
    const fs::path clip = tmp.path / "clip.mp4";
    std::string ignored;
    REQUIRE(run(ffmpeg->path,
                {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=duration=1:size=160x120:rate=10",
                 "-pix_fmt", "yuv420p", clip.string()},
                &ignored));
    const std::string url = "file://" + clip.string();

    // A file:// link needs yt-dlp's own permission, given only here.
    auto lookup = ytdlp_lookup_args(url);
    lookup.insert(lookup.begin(), "--enable-file-urls");
    std::string json;
    REQUIRE(run(ytdlp->path, lookup, &json));
    const auto info = parse_ytdlp_info(json);
    REQUIRE(info.has_value());
    CHECK_FALSE(info->playlist);
    CHECK(info->title == "clip");

    const fs::path out = tmp.path / ".import-dl.part";
    auto download = ytdlp_download_args(url, DownloadChoice{}, out, ffmpeg->path);
    download.insert(download.begin(), "--enable-file-urls");
    std::string printed;
    REQUIRE(run(ytdlp->path, download, &printed));
    std::optional<std::pair<std::string, fs::path>> file;
    YtdlpProgress progress;
    std::optional<double> last;
    std::istringstream lines{printed};
    for (std::string line; std::getline(lines, line);) {
        if (auto f = parse_ytdlp_file(line)) {
            file = f;
        }
        if (auto p = progress.read(line)) {
            last = p;
        }
    }
    INFO(printed);
    REQUIRE(file.has_value());
    CHECK(file->first == "clip");
    CHECK(file->second.parent_path() == out);
    CHECK(fs::file_size(file->second) > 0);
    REQUIRE(last.has_value());
    CHECK(*last == 1.0);
}
