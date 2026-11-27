#pragma once

// The one rule for a media file name handed in from outside (QML, the web
// remote, an image URL): only its last path element counts.
//
// Plain C++20 (no Qt).

#include <filesystem>
#include <string>
#include <string_view>

namespace seventhslide::util {

/// ``raw``'s last path element, or empty when that is no file name (``.``,
/// ``..``, nothing). A name that went through this cannot reach outside the
/// folder it is joined to.
[[nodiscard]] inline std::string leaf_filename(std::string_view raw)
{
    auto fn = std::filesystem::path(raw).filename().string();
    return (fn == "." || fn == "..") ? std::string{} : fn;
}

}  // namespace seventhslide::util
