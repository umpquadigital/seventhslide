#include "seventhslide/platform/tools.hpp"

#include "seventhslide/util/strings.hpp"

#include <system_error>
#include <utility>

namespace seventhslide::platform {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view k_windows = "windows";

/// ``dir / file`` when the file there is executable.
std::optional<fs::path> executable_in(const fs::path& dir,
                                      const std::string& file,
                                      std::string_view platform_id)
{
    if (dir.empty()) {
        return std::nullopt;
    }
    fs::path candidate = dir / file;
    if (!is_executable_file(candidate, platform_id)) {
        return std::nullopt;
    }
    return candidate;
}

std::string_view first_word(std::string_view s)
{
    s = util::trim_view(s);
    const auto end = s.find_first_of(" \t");
    return end == std::string_view::npos ? s : s.substr(0, end);
}

} // namespace

std::string_view to_string(ToolSource source) noexcept
{
    switch (source) {
    case ToolSource::Chosen:
        return "chosen";
    case ToolSource::Managed:
        return "managed";
    case ToolSource::Bundled:
        return "bundled";
    case ToolSource::System:
        break;
    }
    return "system";
}

ToolSearch ToolSearch::from(const Paths& paths, fs::path app_dir)
{
    ToolSearch search;
    search.tools_dir = paths.tools_dir();
    search.app_dir = std::move(app_dir);
    search.platform_id = paths.env().platform_id;
    if (paths.env().get_env) {
        search.path_env = paths.env().get_env("PATH");
    }
    if (paths.env().home_dir) {
        search.extra_dirs = default_extra_dirs(search.platform_id, paths.env().home_dir());
    }
    return search;
}

std::optional<ToolLocation> find_tool(std::string_view name,
                                      const ToolSearch& search,
                                      const fs::path& chosen)
{
    if (!chosen.empty() && is_executable_file(chosen, search.platform_id)) {
        return ToolLocation{chosen, ToolSource::Chosen};
    }
    const std::string file = tool_file_name(name, search.platform_id);
    if (auto found = executable_in(search.tools_dir, file, search.platform_id)) {
        return ToolLocation{std::move(*found), ToolSource::Managed};
    }
    if (auto found = executable_in(search.app_dir, file, search.platform_id)) {
        return ToolLocation{std::move(*found), ToolSource::Bundled};
    }
    if (search.path_env) {
        for (const auto& dir : split_path_env(*search.path_env, search.platform_id)) {
            if (auto found = executable_in(dir, file, search.platform_id)) {
                return ToolLocation{std::move(*found), ToolSource::System};
            }
        }
    }
    for (const auto& dir : search.extra_dirs) {
        if (auto found = executable_in(dir, file, search.platform_id)) {
            return ToolLocation{std::move(*found), ToolSource::System};
        }
    }
    return std::nullopt;
}

std::string tool_file_name(std::string_view name, std::string_view platform_id)
{
    std::string file{name};
    if (platform_id == k_windows) {
        file += ".exe";
    }
    return file;
}

std::vector<fs::path> split_path_env(std::string_view value, std::string_view platform_id)
{
    const char separator = platform_id == k_windows ? ';' : ':';
    std::vector<fs::path> dirs;
    while (!value.empty()) {
        const auto end = value.find(separator);
        const std::string_view entry = value.substr(0, end);
        if (!entry.empty()) {
            dirs.emplace_back(std::string{entry});
        }
        if (end == std::string_view::npos) {
            break;
        }
        value.remove_prefix(end + 1);
    }
    return dirs;
}

std::vector<fs::path> default_extra_dirs(std::string_view platform_id, const fs::path& home)
{
    if (platform_id == k_windows) {
        return {};
    }
    if (platform_id == "macos") {
        return {fs::path{"/opt/homebrew/bin"}, fs::path{"/usr/local/bin"}};
    }
    std::vector<fs::path> dirs;
    if (!home.empty()) {
        dirs.push_back(home / ".local" / "bin");
    }
    dirs.emplace_back("/usr/local/bin");
    return dirs;
}

bool is_executable_file(const fs::path& path, std::string_view platform_id)
{
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::is_regular_file(status)) {
        return false;
    }
    if (platform_id == k_windows) {
        return true;
    }
    constexpr auto any_exec = fs::perms::owner_exec | fs::perms::group_exec
                              | fs::perms::others_exec;
    return (status.permissions() & any_exec) != fs::perms::none;
}

std::string parse_tool_version(std::string_view first_line)
{
    constexpr std::string_view k_marker = " version";
    const std::string_view line = util::trim_view(first_line);
    const auto at = line.find(k_marker);
    if (at != std::string_view::npos) {
        const std::string_view rest = line.substr(at + k_marker.size());
        // "version" the word, not the start of a longer one.
        if (rest.empty() || rest.front() == ' ' || rest.front() == '\t') {
            return std::string{first_word(rest)};
        }
    }
    return std::string{first_word(line)};
}

} // namespace seventhslide::platform
