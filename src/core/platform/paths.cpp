#include "seventhslide/platform/paths.hpp"

#include <cstdlib>
#include <system_error>

namespace seventhslide::platform {
namespace {

namespace fs = std::filesystem;

std::optional<std::string> system_getenv(std::string_view name)
{
    const std::string key{name};
#if defined(_WIN32)
    // Prefer the narrow getenv for testability; UTF-16 can land later with Qt/Win32 APIs.
    if (const char* v = std::getenv(key.c_str())) {
        return std::string{v};
    }
    return std::nullopt;
#else
    if (const char* v = std::getenv(key.c_str())) {
        return std::string{v};
    }
    return std::nullopt;
#endif
}

fs::path system_home_dir()
{
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE")) {
        return fs::path{profile};
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path{home};
    }
    return fs::path{"C:/Users/Default"};
#else
    if (const char* home = std::getenv("HOME")) {
        return fs::path{home};
    }
    return fs::path{"/"};
#endif
}

std::string system_platform_id()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

} // namespace

PathEnvironment PathEnvironment::system()
{
    PathEnvironment env;
    env.get_env = system_getenv;
    env.home_dir = system_home_dir;
    env.platform_id = system_platform_id();
    return env;
}

Paths::Paths(PathEnvironment env)
    : env_(std::move(env))
{
    if (!env_.get_env) {
        env_.get_env = system_getenv;
    }
    if (!env_.home_dir) {
        env_.home_dir = system_home_dir;
    }
    if (env_.platform_id.empty()) {
        env_.platform_id = system_platform_id();
    }
}

std::optional<std::string> Paths::getenv(std::string_view name) const
{
    return env_.get_env(name);
}

std::filesystem::path Paths::expand_user(std::string_view raw) const
{
    if (raw.empty()) {
        return {};
    }
    // Expand "~/..." and "~" to the home directory.
    if (raw == "~") {
        return env_.home_dir();
    }
    if (raw.size() >= 2 && raw[0] == '~' && (raw[1] == '/' || raw[1] == '\\')) {
        return env_.home_dir() / fs::path{std::string{raw.substr(2)}};
    }
    return fs::path{std::string{raw}};
}

std::filesystem::path Paths::platform_data_dir() const
{
    fs::path base;
    if (env_.platform_id == "windows") {
        if (auto appdata = getenv("APPDATA")) {
            base = fs::path{*appdata};
        } else {
            base = env_.home_dir() / "AppData" / "Roaming";
        }
    } else if (env_.platform_id == "macos") {
        base = env_.home_dir() / "Library" / "Application Support";
    } else {
        // linux / BSD / other POSIX
        if (auto xdg = getenv("XDG_DATA_HOME")) {
            base = fs::path{*xdg};
        } else {
            base = env_.home_dir() / ".local" / "share";
        }
    }
    return base / std::string{k_app_name};
}

std::filesystem::path Paths::data_dir() const
{
    if (auto override = getenv(k_data_dir_env)) {
        const fs::path expanded = expand_user(*override);
        std::error_code ec;
        return fs::absolute(expanded, ec);
    }
    return platform_data_dir();
}

fs::path Paths::tools_dir() const
{
    return data_dir() / k_tools_dirname;
}

bool Paths::ensure_data_dir(std::filesystem::path* out) const
{
    const fs::path dir = data_dir();
    if (out) {
        *out = dir;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec || fs::is_directory(dir);
}

bool Paths::path_is_within(const std::filesystem::path& path,
                           const std::filesystem::path& parent)
{
    std::error_code ec;
    const fs::path resolved_parent = fs::weakly_canonical(parent, ec);
    if (ec) {
        return false;
    }
    return path_is_within_resolved(path, resolved_parent);
}

bool Paths::path_is_within_resolved(const std::filesystem::path& path,
                                    const std::filesystem::path& resolved_parent)
{
    std::error_code ec;
    const fs::path resolved_path = fs::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }

    if (resolved_path == resolved_parent) {
        return true;
    }

    const fs::path rel = resolved_path.lexically_relative(resolved_parent);
    // Empty or still-absolute => different roots / cannot express as relative.
    if (rel.empty() || rel.has_root_path()) {
        return false;
    }
    if (rel == ".") {
        return true;
    }
    for (const auto& part : rel) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

} // namespace seventhslide::platform
