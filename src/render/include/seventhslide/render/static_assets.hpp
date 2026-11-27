#pragma once

// Resolve `/static/...` theme/media URLs to filesystem paths.
// Seam: plain C++20 + filesystem (no Qt).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::render {

/**
 * Return ``url`` if it is a safe app-served ``/static/...`` path, else nullopt.
 *
 * Rejects quotes, backslashes, angle brackets, control characters, path
 * traversal, anything but ``/static/...``, and any character outside a
 * filename-safe set (which does include spaces and brackets, common in
 * imported media names).
 */
[[nodiscard]] std::optional<std::string> safe_static_url(std::string_view url);

/**
 * Map a safe ``/static/rel`` URL to ``static_root / rel``.
 *
 * Requires ``resolved`` to stay within ``static_root`` (symlink-aware via
 * ``weakly_canonical`` when possible). Empty root or unsafe URL -> nullopt.
 */
[[nodiscard]] std::optional<std::filesystem::path> resolve_static_asset(
    std::string_view url,
    const std::filesystem::path& static_root);

}  // namespace seventhslide::render
