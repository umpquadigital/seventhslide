#pragma once

// Copy a file in chunks the caller can watch and stop.
//
// std::filesystem::copy_file is one blocking call: it reports nothing until it
// is done and a multi-gigabyte copy cannot be stopped part way. This copies in
// chunks and asks the caller after each one. On Linux the bytes move inside the
// kernel (copy_file_range), so it is as fast as copy_file. On macOS a copy
// within one APFS volume is a clone (clonefile), done at once.
//
// Plain C++20 (no Qt).

#include <cstdint>
#include <filesystem>
#include <functional>

namespace seventhslide::util {

enum class CopyOutcome {
    Copied,
    /// The progress callback asked to stop.
    Stopped,
    /// The source could not be read or the destination written.
    Failed,
};

/// Called after each chunk with the number of bytes copied so far. Return
/// false to stop the copy.
using CopyProgress = std::function<bool(std::uintmax_t copied)>;

/// Copy ``from`` to ``to``, replacing ``to`` if it exists. The parent
/// directory of ``to`` must exist. Unless the result is Copied, ``to`` is
/// removed, so a stopped or failed copy leaves nothing behind.
[[nodiscard]] CopyOutcome copy_file_chunked(const std::filesystem::path& from,
                                            const std::filesystem::path& to,
                                            const CopyProgress& progress);

}  // namespace seventhslide::util
