#pragma once

// Platform path resolution.
// Seam: plain C++20 + std::filesystem only - no Qt.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::platform {

inline constexpr std::string_view k_app_name = "SeventhSlide";

/// Override writable data directory.
inline constexpr std::string_view k_data_dir_env = "SEVENTHSLIDE_DATA_DIR";

/// Under ``data_dir()``: the copies of ffmpeg and yt-dlp the app manages.
inline constexpr std::string_view k_tools_dirname = "tools";


/// Injectable OS face for unit tests. Defaults come from ``PathEnvironment::system()``.
struct PathEnvironment {
    /// Look up an environment variable; empty optional means unset.
    std::function<std::optional<std::string>(std::string_view name)> get_env;

    /// User home directory (``~`` expansion target).
    std::function<std::filesystem::path()> home_dir;

    /// Host platform tag used by ``platform_data_dir``: ``windows``, ``macos``, or ``linux``.
    std::string platform_id;

    [[nodiscard]] static PathEnvironment system();
};

/**
 * Resolves SeventhSlide's data directories.
 *
 * Writable state belongs under ``data_dir()``; bundled assets ship as Qt resources.
 * Callers (app / logging) own any "Data directory: ..." messages - this type stays quiet.
 */
class Paths {
public:
    explicit Paths(PathEnvironment env = PathEnvironment::system());

    [[nodiscard]] const PathEnvironment& env() const noexcept { return env_; }

    /// OS-standard per-user application-data directory for this app (no override, no mkdir).
    [[nodiscard]] std::filesystem::path platform_data_dir() const;

    /// Writable user-data directory. Honors ``SEVENTHSLIDE_DATA_DIR`` (``~`` expanded).
    /// Does not create the directory.
    [[nodiscard]] std::filesystem::path data_dir() const;

    /// Like ``data_dir()``, then ``create_directories``. Returns false on failure.
    [[nodiscard]] bool ensure_data_dir(std::filesystem::path* out = nullptr) const;

    /// ``data_dir() / k_tools_dirname``. Does not create it.
    [[nodiscard]] std::filesystem::path tools_dir() const;

    /// True if ``path`` is ``parent`` or lives inside it (symlinks / ``.`` / ``..`` resolved
    /// via ``weakly_canonical``). False on error or different roots (e.g. Windows drives).
    [[nodiscard]] static bool path_is_within(const std::filesystem::path& path,
                                             const std::filesystem::path& parent);
    /// ``path_is_within`` for a ``parent`` already passed through
    /// ``weakly_canonical``, so only ``path`` is resolved.
    [[nodiscard]] static bool path_is_within_resolved(const std::filesystem::path& path,
                                                      const std::filesystem::path& resolved_parent);

private:
    PathEnvironment env_;

    [[nodiscard]] std::optional<std::string> getenv(std::string_view name) const;
    [[nodiscard]] std::filesystem::path expand_user(std::string_view raw) const;
};

} // namespace seventhslide::platform
