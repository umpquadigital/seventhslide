#pragma once

// Writing a ZIP archive, one entry after another, to a stream.
//
// What the app packs is compressed already (PNG images, pictures, videos,
// fonts), so entries are stored, not deflated: deflating them again gains
// nothing and costs time, and a stored entry can be read in place.
//
// An archive is classic ZIP until something in it passes what 32 bits hold
// (an entry or the archive past 4 GiB): only then are ZIP64's fields written,
// for that entry and for the end of the archive, so a small archive is the
// same bytes it always was. At most ``k_zip_max_entries`` entries. Names are
// UTF-8, flagged as such and, when not plain ASCII, repeated in Info-ZIP's
// Unicode Path field, so every unzip reads them alike.
//
// Plain C++20 (no Qt).

#include "seventhslide/util/file_copy.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace seventhslide::util {

/// The most entries an archive holds (65535 means "see ZIP64" in the
/// classic directory, and nothing the app writes comes near).
inline constexpr std::size_t k_zip_max_entries = 0xFFFE;

/// From this size or offset on, a field is written as ZIP64's: 32 bits of
/// ones say "see ZIP64", so the largest classic value is one less.
inline constexpr std::uint64_t k_zip64_from = 0xFFFFFFFFu;

/// When an entry was last changed, as the archive keeps it: local time, to
/// the even second, in 1980 to 2107 (a time outside is clamped to it).
struct ZipTime {
    int year = 1980;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

class ZipWriter {
public:
    /// Write to ``out`` (opened in binary mode) from where it stands. Every
    /// entry is dated ``time``. ``zip64_from`` is where ZIP64 begins: the
    /// tests set it low to write ZIP64 without gigabytes.
    ZipWriter(std::ostream& out, ZipTime time, std::uint64_t zip64_from = k_zip64_from);

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
    ZipWriter(ZipWriter&&) = delete;
    ZipWriter& operator=(ZipWriter&&) = delete;
    ~ZipWriter() = default;

    /// Store ``bytes`` as ``name`` (folders split by ``/``, none leading).
    /// False, with ``error()`` saying why, for an empty or repeated name, an
    /// archive with ``k_zip_max_entries`` already, a write that failed, or
    /// after ``finish``; after a failed write the archive is unusable.
    [[nodiscard]] bool add(std::string_view name, std::string_view bytes);

    /// Store the file at ``path`` as ``name``, read in pieces so it is never
    /// held whole. ``size`` and ``crc`` are what a first read of it found
    /// (the header comes before the bytes); a file that reads otherwise now
    /// has changed, which fails and leaves the archive unusable, as does
    /// ``progress`` (bytes of this file written so far) answering false.
    [[nodiscard]] bool add_file(std::string_view name, const std::filesystem::path& path,
                                std::uint64_t size, std::uint32_t crc,
                                const CopyProgress& progress = {});

    /// Write the directory that ends the archive. False when the write
    /// failed. Nothing can be added afterwards.
    [[nodiscard]] bool finish();

    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }
    /// Bytes written so far.
    [[nodiscard]] std::uint64_t written() const noexcept { return offset_; }
    /// Why the last call failed; empty while none has.
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    struct Entry {
        std::string name;
        std::uint32_t crc = 0;
        std::uint64_t size = 0;
        std::uint64_t offset = 0;
    };

    [[nodiscard]] bool fail(std::string why);
    [[nodiscard]] bool write(std::string_view bytes);
    /// Checks ``name`` and writes the local header of an entry of ``size``
    /// bytes, its CRC ``crc``; the bytes follow.
    [[nodiscard]] bool begin_entry(std::string_view name, std::uint64_t size, std::uint32_t crc);
    void end_entry();

    std::ostream& out_;
    std::uint16_t dos_time_ = 0;
    std::uint16_t dos_date_ = 0;
    std::uint64_t zip64_from_ = k_zip64_from;
    std::vector<Entry> entries_;
    std::unordered_set<std::string> names_;
    /// The entry whose bytes are being written.
    Entry pending_;
    /// Bytes written so far.
    std::uint64_t offset_ = 0;
    bool finished_ = false;
    bool broken_ = false;
    std::string error_;
};

}  // namespace seventhslide::util
