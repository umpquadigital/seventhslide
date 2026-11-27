#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/util/leaf_filename.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using seventhslide::platform::PathEnvironment;
using seventhslide::platform::Paths;
using seventhslide::platform::k_app_name;
using seventhslide::platform::k_data_dir_env;

namespace {

PathEnvironment make_env(std::map<std::string, std::string> vars,
                         fs::path home,
                         std::string platform)
{
    PathEnvironment env;
    env.platform_id = std::move(platform);
    env.home_dir = [home] { return home; };
    env.get_env = [vars = std::move(vars)](std::string_view name) -> std::optional<std::string> {
        const auto it = vars.find(std::string{name});
        if (it == vars.end()) {
            return std::nullopt;
        }
        return it->second;
    };
    return env;
}

} // namespace

TEST_CASE("path_is_within accepts parent and descendants", "[paths]")
{
    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    fs::create_directories(tmp / "child" / "deep");
    const auto parent = tmp;
    const auto child = tmp / "child" / "deep" / "file.txt";
    {
        std::ofstream out{child};
        out << "x";
    }

    REQUIRE(Paths::path_is_within(parent, parent));
    REQUIRE(Paths::path_is_within(child, parent));
    REQUIRE(Paths::path_is_within(tmp / "child", parent));
}

TEST_CASE("path_is_within rejects escape via ..", "[paths]")
{
    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    fs::create_directories(tmp / "sandbox");
    fs::create_directories(tmp / "outside");

    REQUIRE_FALSE(Paths::path_is_within(tmp / "outside" / "x", tmp / "sandbox"));
    REQUIRE_FALSE(Paths::path_is_within(tmp / "sandbox" / ".." / "outside", tmp / "sandbox"));
}

TEST_CASE("linux platform_data_dir uses XDG_DATA_HOME when set", "[paths]")
{
    Paths paths{make_env({{"XDG_DATA_HOME", "/xdg/data"}},
                         "/home/test",
                         "linux")};
    REQUIRE(paths.platform_data_dir() == fs::path{"/xdg/data"} / std::string{k_app_name});
}

TEST_CASE("linux platform_data_dir falls back to ~/.local/share", "[paths]")
{
    Paths paths{make_env({}, "/home/test", "linux")};
    REQUIRE(paths.platform_data_dir()
            == fs::path{"/home/test/.local/share"} / std::string{k_app_name});
}

TEST_CASE("windows platform_data_dir uses APPDATA", "[paths]")
{
    Paths paths{make_env({{"APPDATA", "C:/Users/test/AppData/Roaming"}},
                         "C:/Users/test",
                         "windows")};
    REQUIRE(paths.platform_data_dir()
            == fs::path{"C:/Users/test/AppData/Roaming"} / std::string{k_app_name});
}

TEST_CASE("macos platform_data_dir uses Application Support", "[paths]")
{
    Paths paths{make_env({}, "/Users/test", "macos")};
    REQUIRE(paths.platform_data_dir()
            == fs::path{"/Users/test/Library/Application Support"} / std::string{k_app_name});
}

TEST_CASE("SEVENTHSLIDE_DATA_DIR override wins and expands ~", "[paths]")
{
    Paths paths{make_env({{std::string{k_data_dir_env}, "~/CustomData"}},
                         "/home/test",
                         "linux")};
    const auto got = paths.data_dir();
    REQUIRE(got == fs::absolute(fs::path{"/home/test/CustomData"}));
}

TEST_CASE("ensure_data_dir creates override directory", "[paths]")
{
    const seventhslide::test::TempDir tmp;
    const auto target = tmp.path / "data";  // not there yet

    Paths paths{make_env({{std::string{k_data_dir_env}, target.string()}},
                         "/home/test",
                         "linux")};

    fs::path out;
    REQUIRE(paths.ensure_data_dir(&out));
    REQUIRE(out == fs::absolute(target));
    REQUIRE(fs::is_directory(out));
}

TEST_CASE("system Paths resolves a real data dir path", "[paths][system]")
{
    Paths paths;
    const auto dir = paths.data_dir();
    REQUIRE_FALSE(dir.empty());
    const bool looks_like_app_data =
        dir.filename() == std::string{k_app_name}
        || dir.string().find(std::string{k_app_name}) != std::string::npos
        || std::getenv(std::string{k_data_dir_env}.c_str()) != nullptr;
    REQUIRE(looks_like_app_data);
}

TEST_CASE("leaf_filename keeps the last path element and refuses non-names", "[paths]")
{
    using seventhslide::util::leaf_filename;
    REQUIRE(leaf_filename("Café Intro (1).mp4") == "Café Intro (1).mp4");
    REQUIRE(leaf_filename("../../etc/passwd") == "passwd");
    REQUIRE(leaf_filename("/abs/dir/a.png") == "a.png");
    REQUIRE(leaf_filename("").empty());
    REQUIRE(leaf_filename(".").empty());
    REQUIRE(leaf_filename("..").empty());
    REQUIRE(leaf_filename("dir/..").empty());
    REQUIRE(leaf_filename("dir/").empty());
}
