// Finding ffmpeg, ffprobe and yt-dlp: the order looked, what counts as
// found, and the version in what they print.

#include <catch2/catch_test_macros.hpp>

#include "fake_tool.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using seventhslide::platform::default_extra_dirs;
using seventhslide::platform::find_tool;
using seventhslide::platform::is_executable_file;
using seventhslide::platform::k_tool_ffmpeg;
using seventhslide::platform::k_tool_ytdlp;
using seventhslide::platform::k_tools_dirname;
using seventhslide::platform::parse_tool_version;
using seventhslide::platform::PathEnvironment;
using seventhslide::platform::Paths;
using seventhslide::platform::split_path_env;
using seventhslide::platform::tool_file_name;
using seventhslide::platform::ToolSearch;
using seventhslide::platform::ToolSource;

namespace {

/// A file at ``path``, executable unless ``exec`` is false.
fs::path write_tool(const fs::path& path, bool exec = true)
{
    fs::create_directories(path.parent_path());
    {
        std::ofstream out{path};
        out << "#!/bin/sh\nexit 0\n";
    }
    if (exec) {
        fs::permissions(path, fs::perms::owner_exec | fs::perms::owner_read
                                  | fs::perms::owner_write,
                        fs::perm_options::replace);
    } else {
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
    }
    return path;
}

/// ``name`` in ``dir`` as this system names a program (``ffmpeg.exe`` on
/// Windows).
fs::path tool_in(const fs::path& dir, std::string_view name)
{
    return dir / tool_file_name(name, seventhslide::test::tool_platform());
}

/// A search over ``tmp`` as this system makes one: its tools and app
/// folders, and two PATH entries.
ToolSearch local_search(const fs::path& tmp)
{
    ToolSearch search;
    search.tools_dir = tmp / "tools";
    search.app_dir = tmp / "app";
    search.path_env = seventhslide::test::tool_path_env({tmp / "bin1", tmp / "bin2"});
    search.platform_id = seventhslide::test::tool_platform();
    return search;
}

} // namespace

TEST_CASE("A tool nowhere is not found", "[tools]")
{
    const seventhslide::test::TempDir tmp;
    CHECK_FALSE(find_tool(k_tool_ffmpeg, local_search(tmp.path)).has_value());
}

TEST_CASE("PATH is searched in order, after the app's own places", "[tools]")
{
    const seventhslide::test::TempDir tmp;
    const auto search = local_search(tmp.path);

    const auto second = write_tool(tool_in(tmp.path / "bin2", "ffmpeg"));
    auto found = find_tool(k_tool_ffmpeg, search);
    REQUIRE(found.has_value());
    CHECK(found->path == second);
    CHECK(found->source == ToolSource::System);

    const auto first = write_tool(tool_in(tmp.path / "bin1", "ffmpeg"));
    found = find_tool(k_tool_ffmpeg, search);
    REQUIRE(found.has_value());
    CHECK(found->path == first);

    const auto bundled = write_tool(tool_in(tmp.path / "app", "ffmpeg"));
    found = find_tool(k_tool_ffmpeg, search);
    REQUIRE(found.has_value());
    CHECK(found->path == bundled);
    CHECK(found->source == ToolSource::Bundled);

    const auto managed = write_tool(tool_in(tmp.path / "tools", "ffmpeg"));
    found = find_tool(k_tool_ffmpeg, search);
    REQUIRE(found.has_value());
    CHECK(found->path == managed);
    CHECK(found->source == ToolSource::Managed);
}

TEST_CASE("A chosen path wins when it is executable, and is ignored when it is not", "[tools]")
{
    const seventhslide::test::TempDir tmp;
    const auto search = local_search(tmp.path);
    const auto managed = write_tool(tool_in(tmp.path / "tools", "yt-dlp"));

    auto found = find_tool(k_tool_ytdlp, search, tmp.path / "nowhere" / "yt-dlp");
    REQUIRE(found.has_value());
    CHECK(found->path == managed);

    const auto chosen = write_tool(tmp.path / "mine" / "yt-dlp-nightly");
    found = find_tool(k_tool_ytdlp, search, chosen);
    REQUIRE(found.has_value());
    CHECK(found->path == chosen);
    CHECK(found->source == ToolSource::Chosen);
}

TEST_CASE("A file that cannot run, or a folder, is not a tool", "[tools]")
{
#if defined(_WIN32)
    SKIP("Windows has no execute bits: \"On Windows a tool is an .exe\" is its case");
#endif
    const seventhslide::test::TempDir tmp;
    const auto search = local_search(tmp.path);
    write_tool(tmp.path / "bin1" / "ffmpeg", false);
    fs::create_directories(tmp.path / "bin2" / "ffmpeg");
    CHECK_FALSE(find_tool(k_tool_ffmpeg, search).has_value());

    CHECK_FALSE(is_executable_file(tmp.path / "bin1" / "ffmpeg", "linux"));
    CHECK(is_executable_file(tmp.path / "bin1" / "ffmpeg", "windows"));
    CHECK_FALSE(is_executable_file(tmp.path / "bin2" / "ffmpeg", "windows"));
    CHECK_FALSE(is_executable_file(tmp.path / "missing", "linux"));
}

TEST_CASE("The places a GUI app's PATH misses are looked last", "[tools]")
{
    const seventhslide::test::TempDir tmp;
    auto search = local_search(tmp.path);
    search.extra_dirs = default_extra_dirs("linux", tmp.path / "home");
    REQUIRE(search.extra_dirs.size() == 2);
    CHECK(search.extra_dirs[0] == tmp.path / "home" / ".local" / "bin");
    CHECK(search.extra_dirs[1] == fs::path{"/usr/local/bin"});

    const auto local = write_tool(tool_in(tmp.path / "home" / ".local" / "bin", "yt-dlp"));
    auto found = find_tool(k_tool_ytdlp, search);
    REQUIRE(found.has_value());
    CHECK(found->path == local);
    CHECK(found->source == ToolSource::System);

    const auto on_path = write_tool(tool_in(tmp.path / "bin2", "yt-dlp"));
    found = find_tool(k_tool_ytdlp, search);
    REQUIRE(found.has_value());
    CHECK(found->path == on_path);

    const auto mac = default_extra_dirs("macos", tmp.path / "home");
    REQUIRE(mac.size() == 2);
    CHECK(mac[0] == fs::path{"/opt/homebrew/bin"});
    CHECK(mac[1] == fs::path{"/usr/local/bin"});
    CHECK(default_extra_dirs("windows", tmp.path / "home").empty());
    CHECK(default_extra_dirs("linux", {}).size() == 1);
}

TEST_CASE("On Windows a tool is an .exe, and PATH splits on semicolons", "[tools]")
{
    const seventhslide::test::TempDir tmp;
    ToolSearch search;
    search.platform_id = "windows";
    search.path_env = (tmp.path / "a").string() + ";;" + (tmp.path / "b").string() + ";";

    CHECK(tool_file_name("ffmpeg", "windows") == "ffmpeg.exe");
    CHECK(tool_file_name("ffmpeg", "linux") == "ffmpeg");
    CHECK(tool_file_name("yt-dlp", "macos") == "yt-dlp");

    const auto dirs = split_path_env(*search.path_env, "windows");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == tmp.path / "a");
    CHECK(dirs[1] == tmp.path / "b");
    CHECK(split_path_env("", "linux").empty());
    CHECK(split_path_env("/x::/y:", "linux").size() == 2);

    // Not executable by its bits, which Windows does not have.
    const auto exe = write_tool(tmp.path / "b" / "ffmpeg.exe", false);
    write_tool(tmp.path / "a" / "ffmpeg");
    const auto found = find_tool(k_tool_ffmpeg, search);
    REQUIRE(found.has_value());
    CHECK(found->path == exe);
}

TEST_CASE("ToolSearch::from takes PATH, the platform and the tools dir from the app's paths",
          "[tools]")
{
    const seventhslide::test::TempDir tmp;
    PathEnvironment env;
    env.platform_id = "linux";
    env.home_dir = [home = tmp.path / "home"] { return home; };
    const std::map<std::string, std::string> vars = {
        {"SEVENTHSLIDE_DATA_DIR", (tmp.path / "data").string()},
        {"PATH", "/usr/bin:/bin"},
    };
    env.get_env = [vars](std::string_view name) -> std::optional<std::string> {
        const auto it = vars.find(std::string{name});
        return it == vars.end() ? std::nullopt : std::optional{it->second};
    };
    const Paths paths{env};
    CHECK(paths.tools_dir() == tmp.path / "data" / k_tools_dirname);

    const auto search = ToolSearch::from(paths, tmp.path / "app");
    CHECK(search.tools_dir == paths.tools_dir());
    CHECK(search.app_dir == tmp.path / "app");
    CHECK(search.platform_id == "linux");
    REQUIRE(search.path_env.has_value());
    CHECK(*search.path_env == "/usr/bin:/bin");
    REQUIRE(search.extra_dirs.size() == 2);
    CHECK(search.extra_dirs[0] == tmp.path / "home" / ".local" / "bin");
}

TEST_CASE("The version is the word after 'version', or the first word", "[tools]")
{
    CHECK(parse_tool_version("ffmpeg version 6.1.1-3ubuntu5 Copyright (c) 2000-2023 the FFmpeg "
                             "developers")
          == "6.1.1-3ubuntu5");
    CHECK(parse_tool_version("ffprobe version n7.0.2-7-gd3ef3fa8a1 Copyright (c)")
          == "n7.0.2-7-gd3ef3fa8a1");
    CHECK(parse_tool_version("2025.01.15\n") == "2025.01.15");
    CHECK(parse_tool_version("  2025.01.15 \r\n") == "2025.01.15");
    CHECK(parse_tool_version("").empty());
    CHECK(parse_tool_version("   ").empty());
    CHECK(parse_tool_version("ffmpeg version ").empty());
}
