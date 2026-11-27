#include "seventhslide/util/zip_writer.hpp"

#include "seventhslide/util/crc32.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace seventhslide::util {

namespace {

constexpr std::uint32_t k_local_header_sig = 0x04034b50;
constexpr std::uint32_t k_central_header_sig = 0x02014b50;
constexpr std::uint32_t k_end_of_directory_sig = 0x06054b50;
constexpr std::uint32_t k_zip64_end_sig = 0x06064b50;
constexpr std::uint32_t k_zip64_locator_sig = 0x07064b50;
/// Version 2.0: the oldest that knows folders and the UTF-8 flag's place.
constexpr std::uint16_t k_version = 20;
/// Version 4.5: ZIP64, needed to read an entry that uses it.
constexpr std::uint16_t k_version_zip64 = 45;
/// General purpose bit 11: the name is UTF-8.
constexpr std::uint16_t k_flag_utf8 = 0x0800;
constexpr std::uint16_t k_method_stored = 0;
constexpr std::uint16_t k_zip64_extra_id = 0x0001;
constexpr std::uint32_t k_ones32 = 0xFFFFFFFFu;
constexpr std::uint16_t k_ones16 = 0xFFFFu;
constexpr std::size_t k_chunk = 1 << 20;

void put16(std::string& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put32(std::string& out, std::uint32_t v)
{
    put16(out, static_cast<std::uint16_t>(v & 0xFFFFu));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

void put64(std::string& out, std::uint64_t v)
{
    put32(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
    put32(out, static_cast<std::uint32_t>(v >> 32));
}

/// Info-ZIP's Unicode Path extra field: its id, and the bytes before the name.
constexpr std::uint16_t k_unicode_path_id = 0x7075;
[[nodiscard]] bool is_ascii(std::string_view s) noexcept
{
    return std::all_of(s.begin(), s.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
}

/// The extra field a non-ASCII ``name`` carries: the name again, in the
/// field Info-ZIP's unzip reads, as some of its builds ignore the UTF-8 flag
/// and would show it as code page 437. Empty for an ASCII name.
[[nodiscard]] std::string unicode_path_field(std::string_view name)
{
    if (is_ascii(name)) {
        return {};
    }
    std::string field;
    put16(field, k_unicode_path_id);
    put16(field, static_cast<std::uint16_t>(5 + name.size()));
    field.push_back(1);  // version
    put32(field, crc32(name));  // of the header's own name, which it matches
    field += name;
    return field;
}

/// ZIP64's extra field holding ``values`` (each 8 bytes), in the order the
/// format gives them: size, compressed size, then the header's offset.
[[nodiscard]] std::string zip64_field(const std::vector<std::uint64_t>& values)
{
    std::string field;
    put16(field, k_zip64_extra_id);
    put16(field, static_cast<std::uint16_t>(8 * values.size()));
    for (const auto v : values) {
        put64(field, v);
    }
    return field;
}

[[nodiscard]] std::uint16_t dos_date(const ZipTime& t) noexcept
{
    const int year = std::clamp(t.year, 1980, 2107);
    const int month = std::clamp(t.month, 1, 12);
    const int day = std::clamp(t.day, 1, 31);
    return static_cast<std::uint16_t>(((year - 1980) << 9) | (month << 5) | day);
}

[[nodiscard]] std::uint16_t dos_time(const ZipTime& t) noexcept
{
    const int hour = std::clamp(t.hour, 0, 23);
    const int minute = std::clamp(t.minute, 0, 59);
    const int second = std::clamp(t.second, 0, 59);
    return static_cast<std::uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

}  // namespace

ZipWriter::ZipWriter(std::ostream& out, ZipTime time, std::uint64_t zip64_from)
    : out_(out)
    , dos_time_(dos_time(time))
    , dos_date_(dos_date(time))
    , zip64_from_(std::min(zip64_from, k_zip64_from))
{
}

bool ZipWriter::fail(std::string why)
{
    error_ = std::move(why);
    return false;
}

bool ZipWriter::write(std::string_view bytes)
{
    out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out_) {
        broken_ = true;
        return fail("Could not write the archive");
    }
    offset_ += bytes.size();
    return true;
}

bool ZipWriter::begin_entry(std::string_view name, std::uint64_t size, std::uint32_t crc)
{
    if (broken_ || finished_) {
        return fail("The archive is closed");
    }
    if (name.empty() || name.front() == '/' || name.size() > 0xFFFFu) {
        return fail("An archive entry needs a relative name");
    }
    if (names_.contains(std::string(name))) {
        return fail("The archive already holds " + std::string(name));
    }
    if (entries_.size() >= k_zip_max_entries) {
        return fail("Too many files for one archive");
    }

    pending_ = Entry{std::string(name), crc, size, offset_};
    const bool big = size >= zip64_from_;
    std::string extra = big ? zip64_field({size, size}) : std::string();
    extra += unicode_path_field(name);
    // The directory's copy may hold the offset too: 28 bytes more at most.
    if (extra.size() > 0xFFFFu - 28) {
        return fail("An archive entry needs a shorter name");
    }

    std::string header;
    header.reserve(30 + name.size() + extra.size());
    put32(header, k_local_header_sig);
    put16(header, big ? k_version_zip64 : k_version);
    put16(header, k_flag_utf8);
    put16(header, k_method_stored);
    put16(header, dos_time_);
    put16(header, dos_date_);
    put32(header, crc);
    // Stored: the compressed size is the size.
    put32(header, big ? k_ones32 : static_cast<std::uint32_t>(size));
    put32(header, big ? k_ones32 : static_cast<std::uint32_t>(size));
    put16(header, static_cast<std::uint16_t>(name.size()));
    put16(header, static_cast<std::uint16_t>(extra.size()));
    header += name;
    header += extra;
    // Unusable until the entry is whole: whatever stops it part way (a
    // throw included) leaves a directory that would not match the bytes.
    broken_ = true;
    if (!write(header)) {
        return false;
    }
    return true;
}

void ZipWriter::end_entry()
{
    broken_ = false;
    names_.insert(pending_.name);
    entries_.push_back(std::move(pending_));
    pending_ = {};
}

bool ZipWriter::add(std::string_view name, std::string_view bytes)
{
    if (!begin_entry(name, bytes.size(), crc32(bytes)) || !write(bytes)) {
        return false;
    }
    end_entry();
    return true;
}

bool ZipWriter::add_file(std::string_view name, const std::filesystem::path& path,
                         std::uint64_t size, std::uint32_t crc, const CopyProgress& progress)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("Could not read " + path.filename().string());
    }
    if (!begin_entry(name, size, crc)) {
        return false;
    }
    std::string chunk(k_chunk, '\0');
    Crc32 check;
    std::uint64_t copied = 0;
    while (copied < size) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(k_chunk, size - copied));
        in.read(chunk.data(), static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got != want) {
            return fail(path.filename().string()
                        + (in.bad() ? " could not be read" : " changed while it was being packed"));
        }
        const std::string_view piece(chunk.data(), got);
        check.update(piece);
        if (!write(piece)) {
            return false;
        }
        copied += got;
        if (progress && !progress(copied)) {
            return fail("Stopped");
        }
    }
    // Nothing more past the size, and the bytes as they were.
    if (in.peek() != std::ifstream::traits_type::eof() || check.value() != crc) {
        return fail(path.filename().string() + " changed while it was being packed");
    }
    end_entry();
    return true;
}

bool ZipWriter::finish()
{
    if (broken_ || finished_) {
        return fail("The archive is closed");
    }
    const std::uint64_t directory_offset = offset_;
    std::string directory;
    for (const auto& e : entries_) {
        const bool big_size = e.size >= zip64_from_;
        const bool big_offset = e.offset >= zip64_from_;
        std::vector<std::uint64_t> wide;
        if (big_size) {
            wide.push_back(e.size);
            wide.push_back(e.size);
        }
        if (big_offset) {
            wide.push_back(e.offset);
        }
        std::string extra = wide.empty() ? std::string() : zip64_field(wide);
        extra += unicode_path_field(e.name);
        const std::uint16_t version = wide.empty() ? k_version : k_version_zip64;
        put32(directory, k_central_header_sig);
        put16(directory, version);  // made by: MS-DOS, so no Unix modes are claimed
        put16(directory, version);
        put16(directory, k_flag_utf8);
        put16(directory, k_method_stored);
        put16(directory, dos_time_);
        put16(directory, dos_date_);
        put32(directory, e.crc);
        put32(directory, big_size ? k_ones32 : static_cast<std::uint32_t>(e.size));
        put32(directory, big_size ? k_ones32 : static_cast<std::uint32_t>(e.size));
        put16(directory, static_cast<std::uint16_t>(e.name.size()));
        put16(directory, static_cast<std::uint16_t>(extra.size()));
        put16(directory, 0);  // comment
        put16(directory, 0);  // disk number
        put16(directory, 0);  // internal attributes
        put32(directory, 0);  // external attributes
        put32(directory, big_offset ? k_ones32 : static_cast<std::uint32_t>(e.offset));
        directory += e.name;
        directory += extra;
    }
    const std::uint64_t directory_size = directory.size();
    const std::uint64_t count = entries_.size();
    const bool big_end = directory_offset >= zip64_from_ || directory_size >= zip64_from_
        || count >= k_ones16;
    if (big_end) {
        const std::uint64_t zip64_end_offset = directory_offset + directory_size;
        put32(directory, k_zip64_end_sig);
        put64(directory, 44);  // the record's size, after this field
        put16(directory, k_version_zip64);
        put16(directory, k_version_zip64);
        put32(directory, 0);  // this disk
        put32(directory, 0);  // the disk the directory starts on
        put64(directory, count);
        put64(directory, count);
        put64(directory, directory_size);
        put64(directory, directory_offset);
        put32(directory, k_zip64_locator_sig);
        put32(directory, 0);  // the disk the ZIP64 end is on
        put64(directory, zip64_end_offset);
        put32(directory, 1);  // disks in all
    }
    const auto narrow = [&](std::uint64_t v) {
        return big_end && v >= zip64_from_ ? k_ones32 : static_cast<std::uint32_t>(v);
    };
    const auto narrow_count =
        count >= k_ones16 ? k_ones16 : static_cast<std::uint16_t>(count);
    put32(directory, k_end_of_directory_sig);
    put16(directory, 0);  // this disk
    put16(directory, 0);  // the disk the directory starts on
    put16(directory, narrow_count);
    put16(directory, narrow_count);
    put32(directory, narrow(directory_size));
    put32(directory, narrow(directory_offset));
    put16(directory, 0);  // no archive comment
    if (!write(directory)) {
        return false;
    }
    out_.flush();
    if (!out_) {
        broken_ = true;
        return fail("Could not write the archive");
    }
    finished_ = true;
    return true;
}

}  // namespace seventhslide::util
