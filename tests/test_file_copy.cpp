#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/util/file_copy.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using seventhslide::util::copy_file_chunked;
using seventhslide::util::CopyOutcome;
namespace fs = std::filesystem;

namespace {

/// Bytes that differ from offset to offset, so a misplaced chunk shows.
void write_pattern(const fs::path& path, std::size_t size)
{
    std::ofstream out(path, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) {
        out.put(static_cast<char>((i * 131u + i / 4099u) & 0xffu));
    }
}

std::string read_all(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("A chunked copy writes every byte and reports up to the size", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto from = dir.path / "from.bin";
    const auto to = dir.path / "to.bin";
    // More than one chunk on either copy path (a clone, on macOS, is one).
    constexpr std::size_t k_size = 40u * 1024u * 1024u + 123u;
    write_pattern(from, k_size);

    std::vector<std::uintmax_t> reports;
    const auto outcome = copy_file_chunked(from, to, [&](std::uintmax_t copied) {
        reports.push_back(copied);
        return true;
    });

    REQUIRE(outcome == CopyOutcome::Copied);
    REQUIRE(read_all(to) == read_all(from));
#if defined(__APPLE__)
    // On APFS a clone: done in one step, so told once.
    REQUIRE_FALSE(reports.empty());
#else
    REQUIRE(reports.size() > 1);
#endif
    for (std::size_t i = 1; i < reports.size(); ++i) {
        REQUIRE(reports[i] > reports[i - 1]);
    }
    REQUIRE(reports.back() == k_size);
}

TEST_CASE("A chunked copy of an empty file makes an empty file", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto from = dir.path / "empty.bin";
    const auto to = dir.path / "copy.bin";
    { std::ofstream out(from, std::ios::binary); }

    REQUIRE(copy_file_chunked(from, to, {}) == CopyOutcome::Copied);
    REQUIRE(fs::exists(to));
    REQUIRE(fs::file_size(to) == 0);
}

TEST_CASE("A stopped copy leaves no destination behind", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto from = dir.path / "from.bin";
    const auto to = dir.path / "to.bin";
    write_pattern(from, 40u * 1024u * 1024u);

    int calls = 0;
    const auto outcome = copy_file_chunked(from, to, [&](std::uintmax_t) {
        ++calls;
        return false;
    });

    REQUIRE(outcome == CopyOutcome::Stopped);
    // It stopped at the first chunk rather than finishing the file.
    REQUIRE(calls == 1);
    REQUIRE_FALSE(fs::exists(to));
}

TEST_CASE("A copy from a missing file fails and creates nothing", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto to = dir.path / "to.bin";

    REQUIRE(copy_file_chunked(dir.path / "missing.bin", to, {}) == CopyOutcome::Failed);
    REQUIRE_FALSE(fs::exists(to));
}

TEST_CASE("A copy into a missing directory fails", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto from = dir.path / "from.bin";
    write_pattern(from, 1000);

    REQUIRE(copy_file_chunked(from, dir.path / "no-such-dir" / "to.bin", {})
            == CopyOutcome::Failed);
}

TEST_CASE("A chunked copy replaces an existing destination", "[util][copy]")
{
    const seventhslide::test::TempDir dir;
    const auto from = dir.path / "from.bin";
    const auto to = dir.path / "to.bin";
    write_pattern(from, 1000);
    write_pattern(to, 5000);

    REQUIRE(copy_file_chunked(from, to, {}) == CopyOutcome::Copied);
    REQUIRE(read_all(to) == read_all(from));
}

#if defined(__linux__)
TEST_CASE("A file the kernel will not copy directly is read and written instead", "[util][copy]")
{
    // procfs reports a size of 0 and refuses copy_file_range (or claims end of
    // file at once), so this goes through the buffered loop.
    const seventhslide::test::TempDir dir;
    const auto to = dir.path / "status.txt";

    REQUIRE(copy_file_chunked("/proc/self/status", to, {}) == CopyOutcome::Copied);
    REQUIRE(read_all(to).find("Name:") != std::string::npos);
}
#endif
