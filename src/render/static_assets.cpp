#include <seventhslide/render/static_assets.hpp>

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/util/strings.hpp>

#include <algorithm>

namespace seventhslide::render {
namespace {

[[nodiscard]] bool is_safe_static_char(char c) noexcept
{
    // Allow spaces and () - common in imported media filenames. Still reject
    // quotes / path tricks that could break out of a URL.
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '.' || c == '_' || c == '~' || c == '/' || c == '-' || c == ' ' || c == '(' || c == ')' || c == '[' || c == ']'
        || c == '+' || c == ',' || c == '=' || c == '@' || c == '!';
}

}  // namespace

std::optional<std::string> safe_static_url(std::string_view url)
{
    url = util::trim_view(url);
    if (url.empty()) {
        return std::nullopt;
    }
    constexpr std::string_view k_prefix = "/static/";
    if (url.size() <= k_prefix.size() || url.substr(0, k_prefix.size()) != k_prefix) {
        return std::nullopt;
    }
    if (!std::all_of(url.begin(), url.end(), is_safe_static_char)) {
        return std::nullopt;
    }
    if (url.find("/../") != std::string_view::npos || url.ends_with("/..")) {
        return std::nullopt;
    }
    return std::string(url);
}

std::optional<std::filesystem::path> resolve_static_asset(
    std::string_view url,
    const std::filesystem::path& static_root)
{
    namespace fs = std::filesystem;
    if (static_root.empty()) {
        return std::nullopt;
    }
    const auto safe = safe_static_url(url);
    if (!safe) {
        return std::nullopt;
    }
    // "/static/theme_backgrounds/a.png" -> "theme_backgrounds/a.png"
    const fs::path rel = fs::path(safe->substr(std::string("/static/").size()));
    if (rel.empty() || rel.is_absolute()) {
        return std::nullopt;
    }
    const fs::path candidate = static_root / rel;
    if (!platform::Paths::path_is_within(candidate, static_root)) {
        return std::nullopt;
    }
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec) || ec) {
        return std::nullopt;
    }
    return candidate;
}

}  // namespace seventhslide::render
