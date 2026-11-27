#pragma once

// Finding the programs the app runs beside itself: ffmpeg and ffprobe (for
// converting media) and yt-dlp (for downloading it).
//
// Each is looked for in a fixed order: a path the user chose for it, the
// app's own tools folder in its data dir (the copies the app manages), beside
// the app's executable (a bundled build), each directory on PATH, then the
// places a GUI app's PATH tends to miss (``default_extra_dirs``). The first
// executable file wins. ``ToolSearch::from`` fills a search from the app's
// paths; tests fill one by hand.
//
// Seam: plain C++20 + std::filesystem only - no Qt.

#include "seventhslide/platform/paths.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::platform {

inline constexpr std::string_view k_tool_ffmpeg = "ffmpeg";
inline constexpr std::string_view k_tool_ffprobe = "ffprobe";
inline constexpr std::string_view k_tool_ytdlp = "yt-dlp";

/// Every tool the app knows, in the order Settings lists them.
inline constexpr std::array<std::string_view, 3> k_tool_names = {
    k_tool_ffmpeg, k_tool_ffprobe, k_tool_ytdlp};

/// Where a tool was found, in the order looked.
enum class ToolSource {
    /// A path the user chose in Settings.
    Chosen,
    /// The app's own tools folder: a copy the app put there.
    Managed,
    /// Beside the app's executable: shipped with it.
    Bundled,
    /// On PATH, or in one of the usual places beside it.
    System,
};

[[nodiscard]] std::string_view to_string(ToolSource source) noexcept;

struct ToolLocation {
    std::filesystem::path path;
    ToolSource source = ToolSource::System;

    [[nodiscard]] bool operator==(const ToolLocation&) const = default;
};

/// Where to look. Any directory may be empty, and is then skipped.
struct ToolSearch {
    /// The app's own tools folder (``Paths::tools_dir``).
    std::filesystem::path tools_dir;
    /// The directory the app's executable is in.
    std::filesystem::path app_dir;
    /// The PATH variable, as the process has it; unset means none.
    std::optional<std::string> path_env;
    /// Looked after PATH: ``default_extra_dirs`` for the platform.
    std::vector<std::filesystem::path> extra_dirs;
    /// ``windows``, ``macos`` or ``linux``: the file name and PATH's separator.
    std::string platform_id;

    /// A search from the app's paths: PATH and the platform from
    /// ``paths.env()``, the tools folder under its data dir, and the usual
    /// extra places for the platform.
    [[nodiscard]] static ToolSearch from(const Paths& paths, std::filesystem::path app_dir);
};

/// The first executable ``name`` in the search's order, with where it was.
/// ``chosen``, when not empty, is looked first and wins if it is executable.
[[nodiscard]] std::optional<ToolLocation> find_tool(std::string_view name,
                                                    const ToolSearch& search,
                                                    const std::filesystem::path& chosen = {});

/// ``name`` as a file: ``name.exe`` on Windows, ``name`` elsewhere.
[[nodiscard]] std::string tool_file_name(std::string_view name, std::string_view platform_id);

/// PATH's directories, in order; empty entries dropped. ``;`` separates them
/// on Windows, ``:`` elsewhere.
[[nodiscard]] std::vector<std::filesystem::path> split_path_env(std::string_view value,
                                                                 std::string_view platform_id);

/// The directories a GUI app's PATH tends to miss, after PATH: pip's and
/// Homebrew's, where yt-dlp and ffmpeg are usually installed by hand.
[[nodiscard]] std::vector<std::filesystem::path> default_extra_dirs(
    std::string_view platform_id, const std::filesystem::path& home);

/// A regular file the platform would run: on Windows any; elsewhere one with
/// an execute bit.
[[nodiscard]] bool is_executable_file(const std::filesystem::path& path,
                                      std::string_view platform_id);

/// The version in the first line a tool prints for ``-version`` /
/// ``--version``: the word after ``version`` when there is one (``ffmpeg
/// version 6.1.1-3ubuntu5 Copyright ...`` gives ``6.1.1-3ubuntu5``), else the
/// line's first word (yt-dlp prints only ``2025.01.15``). Empty for an empty
/// line.
[[nodiscard]] std::string parse_tool_version(std::string_view first_line);

} // namespace seventhslide::platform
