#pragma once

// Helpers the rich text sources share: the markup's entities, the formatting
// tag names and how a colour is written in its tag. Not part of the public API.
// Seam: plain C++20 (no Qt).

#include <seventhslide/util/strings.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::model {

/// The markup entity ``s`` starts with (&lt; &gt; &amp;): its character and
/// length, or nullopt.
[[nodiscard]] inline std::optional<std::pair<char, std::size_t>> markup_entity(std::string_view s)
{
    if (s.starts_with("&lt;")) {
        return std::pair{'<', std::size_t{4}};
    }
    if (s.starts_with("&gt;")) {
        return std::pair{'>', std::size_t{4}};
    }
    if (s.starts_with("&amp;")) {
        return std::pair{'&', std::size_t{5}};
    }
    return std::nullopt;
}

/// Canonical name for a bare formatting tag name (``strong`` -> b), or empty.
[[nodiscard]] inline std::string canonical_style_name(std::string_view name)
{
    const std::string lower = util::to_lower(name);
    if (lower == "b" || lower == "strong") {
        return "b";
    }
    if (lower == "i" || lower == "em") {
        return "i";
    }
    if (lower == "u") {
        return "u";
    }
    if (lower == "super" || lower == "sup") {
        return "super";
    }
    return {};
}

/// ``raw`` as the formatter writes it into a ``<color=...>`` tag: lower case,
/// with a leading ``#``. Empty stays empty.
[[nodiscard]] inline std::string canonical_color_tag(std::string_view raw)
{
    std::string hex = util::to_lower(raw);
    if (hex.empty()) {
        return {};
    }
    if (hex.front() != '#') {
        hex.insert(hex.begin(), '#');
    }
    return hex;
}

} // namespace seventhslide::model
