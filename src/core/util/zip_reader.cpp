#include "seventhslide/util/zip_reader.hpp"

#include "seventhslide/util/crc32.hpp"
#include "seventhslide/util/zip_writer.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace seventhslide::util {

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t k_local_header_sig = 0x04034b50;
constexpr std::uint32_t k_central_header_sig = 0x02014b50;
constexpr std::uint32_t k_end_of_directory_sig = 0x06054b50;
constexpr std::uint32_t k_zip64_end_sig = 0x06064b50;
constexpr std::uint32_t k_zip64_locator_sig = 0x07064b50;
constexpr std::uint64_t k_end_size = 22;
constexpr std::uint64_t k_locator_size = 20;
constexpr std::uint64_t k_zip64_end_size = 56;
constexpr std::uint64_t k_local_size = 30;
constexpr std::uint64_t k_central_size = 46;
constexpr std::uint16_t k_flag_encrypted = 0x0001;
constexpr std::uint16_t k_flag_descriptor = 0x0008;
constexpr std::uint16_t k_zip64_extra_id = 0x0001;
constexpr std::uint32_t k_ones32 = 0xFFFFFFFFu;
constexpr std::uint16_t k_ones16 = 0xFFFFu;
/// The largest directory read: far past what k_zip_max_entries entries with
/// long names take, and small enough to hold.
constexpr std::uint64_t k_max_directory = 64ull << 20;
constexpr std::size_t k_chunk = 1 << 20;

constexpr std::string_view k_damaged =
    "The file is not one SeventhSlide can read: it is damaged, or not a ZIP archive.";
constexpr std::string_view k_repacked =
    "The file has been packed again by another program, which SeventhSlide cannot read. "
    "Export it from SeventhSlide again.";

[[nodiscard]] std::uint16_t le16(std::string_view b, std::size_t at) noexcept
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(b[at])
                                      | (static_cast<unsigned char>(b[at + 1]) << 8));
}

[[nodiscard]] std::uint32_t le32(std::string_view b, std::size_t at) noexcept
{
    return static_cast<std::uint32_t>(le16(b, at)) | (static_cast<std::uint32_t>(le16(b, at + 2)) << 16);
}

[[nodiscard]] std::uint64_t le64(std::string_view b, std::size_t at) noexcept
{
    return static_cast<std::uint64_t>(le32(b, at)) | (static_cast<std::uint64_t>(le32(b, at + 4)) << 32);
}

/// ``size`` bytes of ``in`` from ``at``, or nullopt when there are not that many.
[[nodiscard]] std::optional<std::string> read_at(std::ifstream& in, std::uint64_t at, std::uint64_t size)
{
    in.clear();
    in.seekg(static_cast<std::streamoff>(at));
    if (!in) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size) {
        return std::nullopt;
    }
    return bytes;
}

struct Directory {
    std::uint64_t count = 0;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    /// Where the records after the directory begin: nothing may run past it.
    std::uint64_t end = 0;
};

/// The end of the archive's directory, or nullopt when there is none.
[[nodiscard]] std::optional<Directory> find_directory(std::ifstream& in, std::uint64_t base,
                                                      std::uint64_t size)
{
    if (size < k_end_size) {
        return std::nullopt;
    }
    const std::uint64_t tail_size = std::min<std::uint64_t>(size, k_end_size + 0xFFFF);
    const auto tail = read_at(in, base + size - tail_size, tail_size);
    if (!tail) {
        return std::nullopt;
    }
    // The last record whose comment runs exactly to the end.
    std::optional<std::size_t> at;
    for (std::size_t i = tail->size() - k_end_size + 1; i-- > 0;) {
        if (le32(*tail, i) == k_end_of_directory_sig
            && i + k_end_size + le16(*tail, i + 20) == tail->size()) {
            at = i;
            break;
        }
    }
    if (!at) {
        return std::nullopt;
    }
    const std::uint64_t end_offset = size - tail_size + *at;  // from the archive's start
    const std::string_view e(*tail);
    if (le16(e, *at + 4) != 0 || le16(e, *at + 6) != 0) {
        return std::nullopt;  // split across disks
    }
    Directory dir;
    dir.count = le16(e, *at + 10);
    dir.size = le32(e, *at + 12);
    dir.offset = le32(e, *at + 16);
    dir.end = end_offset;
    if (le16(e, *at + 8) != dir.count) {
        return std::nullopt;
    }

    const bool wide = dir.count == k_ones16 || dir.size == k_ones32 || dir.offset == k_ones32;
    if (end_offset >= k_locator_size) {
        const auto locator = read_at(in, base + end_offset - k_locator_size, k_locator_size);
        if (locator && le32(*locator, 0) == k_zip64_locator_sig) {
            // Subtracted, never added, so a crafted offset cannot wrap round.
            const std::uint64_t z = le64(*locator, 8);
            const std::uint64_t locator_at = end_offset - k_locator_size;
            if (le32(*locator, 4) != 0 || le32(*locator, 16) != 1 || z > locator_at
                || locator_at - z < k_zip64_end_size) {
                return std::nullopt;
            }
            const auto record = read_at(in, base + z, k_zip64_end_size);
            if (!record || le32(*record, 0) != k_zip64_end_sig || le64(*record, 4) < k_zip64_end_size - 12
                || le32(*record, 16) != 0 || le32(*record, 20) != 0
                || le64(*record, 24) != le64(*record, 32)) {
                return std::nullopt;
            }
            dir.count = le64(*record, 32);
            dir.size = le64(*record, 40);
            dir.offset = le64(*record, 48);
            dir.end = z;
            return dir;
        }
    }
    if (wide) {
        return std::nullopt;  // says "see ZIP64" and has none
    }
    return dir;
}

struct Parsed {
    ZipEntry entry;
    std::uint64_t local = 0;
};

}  // namespace

std::optional<ZipReader> ZipReader::open(const fs::path& path, std::string& error)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        error = "The file could not be read.";
        return std::nullopt;
    }
    return open_range(path, 0, size, error);
}

std::optional<ZipReader> ZipReader::open_range(const fs::path& path, std::uint64_t offset,
                                               std::uint64_t size, std::string& error)
{
    std::error_code ec;
    const auto file_size = fs::file_size(path, ec);
    std::ifstream in(path, std::ios::binary);
    if (ec || !in) {
        error = "The file could not be read.";
        return std::nullopt;
    }
    if (offset > file_size || size > file_size - offset) {
        error = std::string(k_damaged);
        return std::nullopt;
    }
    const auto dir = find_directory(in, offset, size);
    if (!dir || dir->end > size || dir->size > k_max_directory || dir->offset > dir->end
        || dir->size != dir->end - dir->offset || dir->count > k_zip_max_entries) {
        error = std::string(k_damaged);
        return std::nullopt;
    }
    const auto central = read_at(in, offset + dir->offset, dir->size);
    if (!central) {
        error = std::string(k_damaged);
        return std::nullopt;
    }

    const auto damaged = [&error]() -> std::optional<ZipReader> {
        error = std::string(k_damaged);
        return std::nullopt;
    };
    const std::string_view c(*central);
    std::vector<Parsed> parsed;
    parsed.reserve(static_cast<std::size_t>(dir->count));
    std::size_t at = 0;
    for (std::uint64_t i = 0; i < dir->count; ++i) {
        if (at + k_central_size > c.size() || le32(c, at) != k_central_header_sig) {
            return damaged();
        }
        const std::uint16_t flags = le16(c, at + 8);
        const std::uint16_t method = le16(c, at + 10);
        std::uint64_t compressed = le32(c, at + 20);
        std::uint64_t plain = le32(c, at + 24);
        const std::size_t name_len = le16(c, at + 28);
        const std::size_t extra_len = le16(c, at + 30);
        const std::size_t comment_len = le16(c, at + 32);
        const std::uint16_t disk = le16(c, at + 34);
        std::uint64_t local = le32(c, at + 42);
        const std::size_t record = k_central_size + name_len + extra_len + comment_len;
        if (at + record > c.size()) {
            return damaged();
        }
        if ((flags & k_flag_encrypted) != 0 || method != 0) {
            error = std::string(k_repacked);
            return std::nullopt;
        }
        // ZIP64's field holds, in order, each of these that says "see ZIP64".
        const std::string_view extra = c.substr(at + k_central_size + name_len, extra_len);
        const bool need_plain = plain == k_ones32;
        const bool need_compressed = compressed == k_ones32;
        const bool need_local = local == k_ones32;
        const bool need_disk = disk == k_ones16;
        if (need_plain || need_compressed || need_local || need_disk) {
            std::optional<std::string_view> wide;
            for (std::size_t x = 0; x + 4 <= extra.size();) {
                const std::uint16_t id = le16(extra, x);
                const std::size_t len = le16(extra, x + 2);
                if (x + 4 + len > extra.size()) {
                    return damaged();
                }
                if (id == k_zip64_extra_id) {
                    wide = extra.substr(x + 4, len);
                }
                x += 4 + len;
            }
            if (!wide) {
                return damaged();
            }
            std::size_t w = 0;
            const auto take = [&](std::uint64_t& field) {
                if (w + 8 > wide->size()) {
                    return false;
                }
                field = le64(*wide, w);
                w += 8;
                return true;
            };
            if ((need_plain && !take(plain)) || (need_compressed && !take(compressed))
                || (need_local && !take(local))) {
                return damaged();
            }
            // The disk number, last and four bytes wide: there is one disk.
            if (need_disk && (w + 4 > wide->size() || le32(*wide, w) != 0)) {
                return damaged();
            }
        } else if (disk != 0) {
            return damaged();
        }
        if (compressed != plain) {
            return damaged();
        }
        Parsed p;
        p.entry.name = std::string(c.substr(at + k_central_size, name_len));
        p.entry.size = plain;
        p.entry.crc = le32(c, at + 16);
        p.local = local;
        // The local header must agree with the directory.
        if (p.local > dir->offset || dir->offset - p.local < k_local_size) {
            return damaged();
        }
        const auto head = read_at(in, offset + p.local, k_local_size);
        if (!head || le32(*head, 0) != k_local_header_sig || le16(*head, 8) != method) {
            return damaged();
        }
        const std::uint16_t local_flags = le16(*head, 6);
        const std::size_t local_name = le16(*head, 26);
        const std::size_t local_extra = le16(*head, 28);
        if ((local_flags & k_flag_descriptor) == 0) {
            const std::uint32_t local_size = le32(*head, 22);
            if (le32(*head, 14) != p.entry.crc
                || (local_size != k_ones32 && local_size != p.entry.size)) {
                return damaged();
            }
        }
        const auto name = read_at(in, offset + p.local + k_local_size, local_name);
        if (!name || *name != p.entry.name) {
            return damaged();
        }
        p.entry.data_offset = p.local + k_local_size + local_name + local_extra;
        if (p.entry.data_offset > dir->offset || p.entry.size > dir->offset - p.entry.data_offset) {
            return damaged();
        }
        parsed.push_back(std::move(p));
        at += record;
    }
    if (at != c.size()) {
        return damaged();  // more in the directory than it lists
    }

    // No two entries share bytes.
    std::vector<const Parsed*> by_place;
    by_place.reserve(parsed.size());
    for (const auto& p : parsed) {
        by_place.push_back(&p);
    }
    std::sort(by_place.begin(), by_place.end(),
              [](const Parsed* a, const Parsed* b) { return a->local < b->local; });
    for (std::size_t i = 1; i < by_place.size(); ++i) {
        const auto& prev = by_place[i - 1]->entry;
        if (by_place[i]->local < prev.data_offset + prev.size) {
            return damaged();
        }
    }

    ZipReader reader;
    reader.path_ = path;
    reader.base_ = offset;
    reader.entries_.reserve(parsed.size());
    for (auto& p : parsed) {
        if (reader.by_name_.contains(p.entry.name)) {
            return damaged();
        }
        reader.by_name_.emplace(p.entry.name, reader.entries_.size());
        reader.entries_.push_back(std::move(p.entry));
    }
    return reader;
}

const ZipEntry* ZipReader::find(std::string_view name) const
{
    const auto it = by_name_.find(std::string(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

std::optional<std::string> ZipReader::read(const ZipEntry& entry, std::uint64_t max_bytes,
                                           std::string& error) const
{
    if (entry.size > max_bytes) {
        error = "A part of the file is larger than SeventhSlide reads.";
        return std::nullopt;
    }
    std::ifstream in(path_, std::ios::binary);
    auto bytes = in ? read_at(in, base_ + entry.data_offset, entry.size) : std::nullopt;
    if (!bytes || crc32(*bytes) != entry.crc) {
        error = std::string(k_damaged);
        return std::nullopt;
    }
    return bytes;
}

CopyOutcome ZipReader::extract(const ZipEntry& entry, const fs::path& to,
                               const CopyProgress& progress, std::string& error) const
{
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        error = "The file could not be read.";
        return CopyOutcome::Failed;
    }
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) {
        // Nothing of this call's is there to take away.
        error = "Could not write a file there.";
        return CopyOutcome::Failed;
    }
    const auto give_up = [&](CopyOutcome outcome, std::string why) {
        out.close();
        std::error_code ec;
        fs::remove(to, ec);
        error = std::move(why);
        return outcome;
    };
    in.seekg(static_cast<std::streamoff>(base_ + entry.data_offset));
    std::string chunk(k_chunk, '\0');
    Crc32 crc;
    std::uint64_t copied = 0;
    while (copied < entry.size) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(k_chunk, entry.size - copied));
        in.read(chunk.data(), static_cast<std::streamsize>(want));
        if (static_cast<std::size_t>(in.gcount()) != want) {
            return give_up(CopyOutcome::Failed, std::string(k_damaged));
        }
        crc.update(std::string_view(chunk.data(), want));
        out.write(chunk.data(), static_cast<std::streamsize>(want));
        if (!out) {
            return give_up(CopyOutcome::Failed, "Could not write a file: is the disk full?");
        }
        copied += want;
        if (progress && !progress(copied)) {
            return give_up(CopyOutcome::Stopped, {});
        }
    }
    if (crc.value() != entry.crc) {
        return give_up(CopyOutcome::Failed, std::string(k_damaged));
    }
    out.close();
    if (!out) {
        return give_up(CopyOutcome::Failed, "Could not write a file: is the disk full?");
    }
    return CopyOutcome::Copied;
}

}  // namespace seventhslide::util
