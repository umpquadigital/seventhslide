// ZIP, both ways: ZipWriter's streamed entries and ZIP64, and ZipReader over
// what it writes, over archives other programs wrote (fixtures/zip), and
// over archives broken on purpose.

#include "fixture_files.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/util/crc32.hpp>
#include <seventhslide/util/zip_reader.hpp>
#include <seventhslide/util/zip_writer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

using seventhslide::util::CopyOutcome;
using seventhslide::util::crc32;
using seventhslide::util::ZipReader;
using seventhslide::util::ZipWriter;

namespace {

void write_file(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    REQUIRE(out);
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/// An archive of ``a.txt`` and ``Café/b.bin`` written with ZIP64 from
/// ``zip64_from`` bytes on.
[[nodiscard]] std::string sample_archive(const fs::path& dir, std::uint64_t zip64_from)
{
    const std::string big(5000, 'x');
    write_file(dir / "big.bin", big);
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {2026, 11, 26, 12, 0, 0}, zip64_from);
    REQUIRE(zip.add("a.txt", "first entry"));
    REQUIRE(zip.add_file("Café/b.bin", dir / "big.bin", big.size(), crc32(big)));
    REQUIRE(zip.finish());
    return out.str();
}

[[nodiscard]] std::optional<ZipReader> open_bytes(const fs::path& dir, const std::string& bytes,
                                                  std::string& error)
{
    write_file(dir / "archive.zip", bytes);
    return ZipReader::open(dir / "archive.zip", error);
}

void put32(std::string& s, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        s[at + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFFu);
    }
}

}  // namespace

TEST_CASE("CRC-32 is the same taken at once or in pieces", "[zip]")
{
    CHECK(crc32("123456789") == 0xCBF43926u);
    seventhslide::util::Crc32 pieces;
    pieces.update("1234");
    pieces.update("");
    pieces.update("56789");
    CHECK(pieces.value() == 0xCBF43926u);
}

TEST_CASE("SHA-256 is the same taken at once or in pieces", "[zip]")
{
    seventhslide::crypto::Sha256 pieces;
    pieces.update("ab");
    pieces.update("c");
    CHECK(pieces.finish_hex() == seventhslide::crypto::sha256_hex("abc"));
    CHECK(seventhslide::crypto::sha256_hex("abc")
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("ZipReader reads back what ZipWriter writes, classic and ZIP64", "[zip]")
{
    seventhslide::test::TempDir tmp;
    // The real limit, and one so low every field is ZIP64's.
    for (const std::uint64_t from : {seventhslide::util::k_zip64_from, std::uint64_t{16}}) {
        const auto bytes = sample_archive(tmp.path, from);
        std::string error;
        const auto zip = open_bytes(tmp.path, bytes, error);
        REQUIRE(zip);
        CHECK(error.empty());
        REQUIRE(zip->entries().size() == 2);
        CHECK(zip->entries()[0].name == "a.txt");
        const auto* b = zip->find("Café/b.bin");
        REQUIRE(b != nullptr);
        CHECK(b->size == 5000);
        CHECK(zip->find("missing") == nullptr);

        const auto a = zip->read(zip->entries()[0], 1000, error);
        REQUIRE(a);
        CHECK(*a == "first entry");
        CHECK_FALSE(zip->read(*b, 4999, error));
        CHECK_FALSE(error.empty());

        error.clear();
        REQUIRE(zip->extract(*b, tmp.path / "out.bin", {}, error) == CopyOutcome::Copied);
        CHECK(read_file(tmp.path / "out.bin") == std::string(5000, 'x'));
    }
    // Under the limit, nothing of ZIP64 is written.
    const auto classic = sample_archive(tmp.path, seventhslide::util::k_zip64_from);
    CHECK(classic.find(std::string("PK\x06\x06", 4)) == std::string::npos);
    const auto wide = sample_archive(tmp.path, 16);
    CHECK(wide.find(std::string("PK\x06\x06", 4)) != std::string::npos);
    CHECK(wide.find(std::string("PK\x06\x07", 4)) != std::string::npos);
}

TEST_CASE("ZipWriter::add_file streams a file and refuses one that changed", "[zip]")
{
    seventhslide::test::TempDir tmp;
    const std::string bytes(3 << 20, 'q');  // more than one piece
    write_file(tmp.path / "f.bin", bytes);

    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    std::uint64_t heard = 0;
    REQUIRE(zip.add_file("f.bin", tmp.path / "f.bin", bytes.size(), crc32(bytes),
                         [&heard](std::uintmax_t copied) {
                             heard = copied;
                             return true;
                         }));
    CHECK(heard == bytes.size());
    REQUIRE(zip.finish());
    CHECK(zip.written() == out.str().size());

    // What a first read found is not what is there now.
    std::ostringstream other(std::ios::binary);
    ZipWriter changed(other, {});
    CHECK_FALSE(changed.add_file("f.bin", tmp.path / "f.bin", bytes.size(), crc32("not it")));
    CHECK_FALSE(changed.finish());
    std::ostringstream shorter(std::ios::binary);
    ZipWriter longer_file(shorter, {});
    CHECK_FALSE(longer_file.add_file("f.bin", tmp.path / "f.bin", 10, crc32(bytes.substr(0, 10))));

    // A stop leaves the archive unusable.
    std::ostringstream stopped_out(std::ios::binary);
    ZipWriter stopped(stopped_out, {});
    CHECK_FALSE(stopped.add_file("f.bin", tmp.path / "f.bin", bytes.size(), crc32(bytes),
                                 [](std::uintmax_t) { return false; }));
    CHECK_FALSE(stopped.add("late.txt", "x"));
}

TEST_CASE("ZipReader reads a stored archive Info-ZIP wrote, and one Python wrote with ZIP64", "[zip]")
{
    std::string error;
    const auto info = ZipReader::open(seventhslide::test::fixtures_dir("zip/info_zip_stored.zip"), error);
    REQUIRE(info);
    const auto* hello = info->find("docs/hello.txt");
    REQUIRE(hello != nullptr);
    CHECK(info->read(*hello, 100, error) == std::optional<std::string>("hello, world\n"));
    const auto* cafe = info->find("Café.txt");
    REQUIRE(cafe != nullptr);
    CHECK(info->read(*cafe, 100, error) == std::optional<std::string>("Café au lait\n"));

    const auto py = ZipReader::open(seventhslide::test::fixtures_dir("zip/python_zip64.zip"), error);
    REQUIRE(py);
    const auto* b = py->find("dir/b.bin");
    REQUIRE(b != nullptr);
    const auto data = py->read(*b, 2000, error);
    REQUIRE(data);
    CHECK(data->size() == 1024);
    CHECK(static_cast<unsigned char>((*data)[255]) == 255);
}

TEST_CASE("ZipReader refuses a deflated archive in words saying why", "[zip]")
{
    std::string error;
    CHECK_FALSE(ZipReader::open(seventhslide::test::fixtures_dir("zip/deflated.zip"), error));
    CHECK(error.find("packed again") != std::string::npos);
}

TEST_CASE("ZipReader refuses damaged archives", "[zip]")
{
    seventhslide::test::TempDir tmp;
    const auto good = sample_archive(tmp.path, seventhslide::util::k_zip64_from);
    std::string error;
    REQUIRE(open_bytes(tmp.path, good, error));

    const auto refused = [&](std::string bytes) {
        std::string why;
        const bool ok = open_bytes(tmp.path, bytes, why).has_value();
        if (!ok) {
            CHECK_FALSE(why.empty());
        }
        return !ok;
    };
    CHECK(refused(""));
    CHECK(refused("not a zip at all, but long enough to have an end record"));
    CHECK(refused(good.substr(0, good.size() - 1)));  // the end cut off
    CHECK(refused(good.substr(40)));                  // the start cut off

    // The local header's CRC disagrees with the directory's.
    auto crc = good;
    put32(crc, 14, 0x12345678u);
    CHECK(refused(crc));

    // An encrypted entry: flag bit 0 in the directory.
    auto encrypted = good;
    const auto dir = encrypted.find(std::string("PK\x01\x02", 4));
    REQUIRE(dir != std::string::npos);
    encrypted[dir + 8] = static_cast<char>(encrypted[dir + 8] | 1);
    CHECK(refused(encrypted));

    // An entry's bytes run past where the directory starts. Its local
    // header says its sizes follow the bytes (bit 3), so only the directory
    // says how long it is.
    auto past = good;
    past[6] = static_cast<char>(past[6] | 0x08);
    put32(past, dir + 20, 0x00100000u);
    put32(past, dir + 24, 0x00100000u);
    CHECK(refused(past));
    // Corrupt bytes are found when read, not opened.
    auto bad_bytes = good;
    bad_bytes[30 + 5] = 'X';  // in "first entry"
    const auto opened = open_bytes(tmp.path, bad_bytes, error);
    REQUIRE(opened);
    CHECK_FALSE(opened->read(opened->entries()[0], 100, error));
    CHECK(opened->extract(opened->entries()[0], tmp.path / "x", {}, error) == CopyOutcome::Failed);
    CHECK_FALSE(fs::exists(tmp.path / "x"));
}

TEST_CASE("ZipReader refuses an entry whose bytes are inside another entry's", "[zip]")
{
    seventhslide::test::TempDir tmp;
    // A whole local header and its byte for "two.txt", to hide inside the
    // first entry's bytes.
    std::ostringstream inner_out(std::ios::binary);
    ZipWriter inner(inner_out, {});
    REQUIRE(inner.add("two.txt", "2"));
    REQUIRE(inner.finish());
    const std::string hidden = inner_out.str().substr(0, 30 + 7 + 1);

    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    REQUIRE(zip.add("one.bin", hidden));
    REQUIRE(zip.add("two.txt", "2"));
    REQUIRE(zip.finish());
    auto bytes = out.str();
    std::string error;
    REQUIRE(open_bytes(tmp.path, bytes, error));

    // The directory's "two.txt" moved to the copy inside "one.bin": its
    // local header is sound, and only the overlap is wrong.
    const auto first = bytes.find(std::string("PK\x01\x02", 4));
    const auto second = bytes.find(std::string("PK\x01\x02", 4), first + 4);
    REQUIRE(second != std::string::npos);
    put32(bytes, second + 42, 30 + 7);  // where one.bin's bytes start
    CHECK_FALSE(open_bytes(tmp.path, bytes, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("ZipReader refuses offsets that would wrap round past its range", "[zip]")
{
    seventhslide::test::TempDir tmp;
    auto bytes = sample_archive(tmp.path, 16);  // ZIP64 throughout
    const auto locator = bytes.find(std::string("PK\x06\x07", 4));
    REQUIRE(locator != std::string::npos);
    // The ZIP64 end said to be just short of 2^64.
    for (int i = 0; i < 8; ++i) {
        bytes[locator + 8 + static_cast<std::size_t>(i)] = static_cast<char>(i == 0 ? 0xF0 : 0xFF);
    }
    std::string error;
    CHECK_FALSE(open_bytes(tmp.path, bytes, error));
}

TEST_CASE("ZipWriter writes a small archive as it always has", "[zip]")
{
    // fixtures/zip/classic_writer.zip is what the writer wrote before it
    // knew ZIP64, for these entries at this time.
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {2026, 11, 26, 12, 0, 0});
    REQUIRE(zip.add("a.txt", "first entry"));
    REQUIRE(zip.add("Café/b.bin", std::string(5000, 'x')));
    REQUIRE(zip.add("empty.txt", ""));
    REQUIRE(zip.finish());
    CHECK(out.str() == read_file(seventhslide::test::fixtures_dir("zip/classic_writer.zip")));
}

TEST_CASE("ZipReader reads an Info-ZIP archive with a ZIP64 directory", "[zip]")
{
    std::string error;
    const auto zip = ZipReader::open(seventhslide::test::fixtures_dir("zip/info_zip_zip64.zip"), error);
    INFO(error);
    REQUIRE(zip);
    REQUIRE(zip->entries().size() == 2);
    CHECK(zip->read(*zip->find("a.txt"), 100, error) == std::optional<std::string>("zip64 directory\n"));
    CHECK(zip->read(*zip->find("b.txt"), 100, error) == std::optional<std::string>("second\n"));
}

TEST_CASE("ZIP64 for an offset alone, empty entries, and a stopped copy", "[zip]")
{
    seventhslide::test::TempDir tmp;
    write_file(tmp.path / "big.bin", std::string(5000, 'x'));
    write_file(tmp.path / "none.bin", "");
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {}, 16);
    REQUIRE(zip.add("a.txt", "first entry"));
    REQUIRE(zip.add_file("b.bin", tmp.path / "big.bin", 5000, crc32(std::string(5000, 'x'))));
    // Small, but after the 16th byte: only its offset is ZIP64's.
    REQUIRE(zip.add("c.txt", "late"));
    REQUIRE(zip.add_file("none.bin", tmp.path / "none.bin", 0, crc32("")));
    CHECK_FALSE(zip.add_file("b.bin", tmp.path / "big.bin", 5000, crc32(std::string(5000, 'x'))));
    REQUIRE(zip.finish());

    std::string error;
    const auto reader = open_bytes(tmp.path, out.str(), error);
    REQUIRE(reader);
    CHECK(reader->read(*reader->find("c.txt"), 100, error) == std::optional<std::string>("late"));
    CHECK(reader->read(*reader->find("none.bin"), 100, error) == std::optional<std::string>(""));
    REQUIRE(reader->extract(*reader->find("none.bin"), tmp.path / "out0", {}, error) == CopyOutcome::Copied);
    CHECK(fs::file_size(tmp.path / "out0") == 0);
    CHECK(reader->extract(*reader->find("b.bin"), tmp.path / "stopped", [](std::uintmax_t) { return false; },
                          error)
          == CopyOutcome::Stopped);
    CHECK_FALSE(fs::exists(tmp.path / "stopped"));
}

TEST_CASE("ZipReader refuses two entries of one name", "[zip]")
{
    seventhslide::test::TempDir tmp;
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    REQUIRE(zip.add("one.txt", "1"));
    REQUIRE(zip.add("two.txt", "2"));
    REQUIRE(zip.finish());
    auto bytes = out.str();
    for (auto at = bytes.find("two.txt"); at != std::string::npos; at = bytes.find("two.txt", at + 1)) {
        bytes.replace(at, 3, "one");
    }
    std::string error;
    CHECK_FALSE(open_bytes(tmp.path, bytes, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("ZipReader opens an archive stored inside another, in place", "[zip]")
{
    seventhslide::test::TempDir tmp;
    const auto inner = sample_archive(tmp.path, seventhslide::util::k_zip64_from);
    std::ostringstream out(std::ios::binary);
    ZipWriter zip(out, {});
    REQUIRE(zip.add("readme.txt", "outer"));
    REQUIRE(zip.add("inner.zip", inner));
    REQUIRE(zip.finish());
    write_file(tmp.path / "outer.zip", out.str());

    std::string error;
    const auto outer = ZipReader::open(tmp.path / "outer.zip", error);
    REQUIRE(outer);
    const auto* entry = outer->find("inner.zip");
    REQUIRE(entry != nullptr);
    const auto nested = ZipReader::open_range(outer->path(), outer->base() + entry->data_offset,
                                              entry->size, error);
    REQUIRE(nested);
    REQUIRE(nested->entries().size() == 2);
    CHECK(nested->read(nested->entries()[0], 100, error) == std::optional<std::string>("first entry"));
    REQUIRE(nested->extract(*nested->find("Café/b.bin"), tmp.path / "b", {}, error) == CopyOutcome::Copied);
    CHECK(read_file(tmp.path / "b") == std::string(5000, 'x'));

    // A range past the file's end.
    CHECK_FALSE(ZipReader::open_range(tmp.path / "outer.zip", 10, out.str().size(), error));
}

