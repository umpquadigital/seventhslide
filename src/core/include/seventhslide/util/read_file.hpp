#pragma once

// Read a whole file into memory, in one read of the size it has.
//
// Plain C++20 (no Qt).

#include <filesystem>
#include <optional>
#include <string>

namespace seventhslide::util {

/// The bytes of ``path``; nullopt when it is not there or cannot be read.
[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path);

}  // namespace seventhslide::util
