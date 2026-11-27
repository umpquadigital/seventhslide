#pragma once

// Reading a ZIP archive of stored entries, as ZipWriter writes them.
//
// The directory at the archive's end is read once, ZIP64 included, and each
// entry is checked against its local header before anything is read from
// it. An entry's bytes are then read whole (up to a limit the caller sets)
// or copied to a file in pieces, and its CRC is checked at the end. Only
// stored, unencrypted entries are read: what the app writes never has any
// other, and a deflated entry would need zlib.
//
// An archive may be a byte range of a larger file: a stored entry is the
// archive's bytes as they are, so an archive packed inside another is read
// in place.
//
// Files read are untrusted. Nothing here makes a path from an entry's name;
// the caller says where an entry goes.
//
// Plain C++20 (no Qt).

#include "seventhslide/util/file_copy.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace seventhslide::util {

struct ZipEntry {
    std::string name;
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
    /// Where its bytes start, from the archive's own start.
    std::uint64_t data_offset = 0;
};

class ZipReader {
public:
    /// The archive that is all of ``path``, or nullopt with ``error`` saying
    /// why it cannot be read, in words for people.
    [[nodiscard]] static std::optional<ZipReader> open(const std::filesystem::path& path,
                                                       std::string& error);
    /// The archive that is ``size`` bytes of ``path`` from ``offset``.
    [[nodiscard]] static std::optional<ZipReader> open_range(const std::filesystem::path& path,
                                                             std::uint64_t offset,
                                                             std::uint64_t size,
                                                             std::string& error);

    /// In the order the directory lists them.
    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const ZipEntry* find(std::string_view name) const;

    /// ``entry``'s bytes, or nullopt with ``error`` set when it is larger than
    /// ``max_bytes``, cannot be read, or does not match its CRC.
    [[nodiscard]] std::optional<std::string> read(const ZipEntry& entry, std::uint64_t max_bytes,
                                                  std::string& error) const;

    /// Copy ``entry``'s bytes to ``to`` (replaced if there), in pieces.
    /// ``progress`` hears the bytes copied so far and may stop it. Unless the
    /// result is Copied, ``to`` is removed and, when Failed, ``error`` says
    /// why.
    [[nodiscard]] CopyOutcome extract(const ZipEntry& entry, const std::filesystem::path& to,
                                      const CopyProgress& progress, std::string& error) const;

    /// The file the archive is in, and where it starts there: an entry's
    /// bytes are at ``base() + data_offset``, which is how an archive stored
    /// inside this one is opened (``open_range``).
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t base() const noexcept { return base_; }

private:
    ZipReader() = default;

    std::filesystem::path path_;
    std::uint64_t base_ = 0;
    std::vector<ZipEntry> entries_;
    std::unordered_map<std::string, std::size_t> by_name_;
};

}  // namespace seventhslide::util
