#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/util/atomic_file.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using seventhslide::util::write_file_atomically;

using seventhslide::test::TempDir;

namespace {

unsigned long this_process_id()
{
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

std::string read_all(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("write_file_atomically replaces the file and leaves no temp", "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "registry.json";
    REQUIRE(write_file_atomically(file, "first"));
    REQUIRE(write_file_atomically(file, "second"));
    REQUIRE(read_all(file) == "second");
    REQUIRE(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

TEST_CASE("A new file is written once, and one already there is left alone", "[util][atomic_file]")
{
    using seventhslide::util::NewFile;
    using seventhslide::util::write_new_file_atomically;
    TempDir dir;
    const auto file = dir.path / "0000000001.json";
    REQUIRE(write_new_file_atomically(file, "first") == NewFile::Written);
    CHECK(write_new_file_atomically(file, "second") == NewFile::There);
    CHECK(read_all(file) == "first");
    CHECK_FALSE(fs::exists(dir.path / "0000000001.json.tmp"));
    CHECK(write_new_file_atomically(dir.path / "missing" / "x.json", "x") == NewFile::Failed);
}

TEST_CASE("Of writers making one new file at once, one makes it and the rest are told",
          "[util][atomic_file]")
{
    using seventhslide::util::NewFile;
    TempDir dir;
    const auto file = dir.path / "0000000002.json";
    std::atomic<int> written{0};
    std::atomic<int> refused{0};
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i) {
        writers.emplace_back([&, i] {
            const auto got = seventhslide::util::write_new_file_atomically(file, std::to_string(i));
            if (got == NewFile::Written) {
                ++written;
            } else if (got == NewFile::There) {
                ++refused;
            }
        });
    }
    for (auto& t : writers) {
        t.join();
    }
    CHECK(written == 1);
    CHECK(refused == 7);
    CHECK(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

TEST_CASE("write_file_atomically keeps the old file when it cannot write", "[util][atomic_file]")
{
    TempDir dir;
    // A folder with a file in it, which no rename can replace.
    const auto file = dir.path / "cache.json";
    fs::create_directories(file);
    std::ofstream(file / "kept") << "good";
    REQUIRE_FALSE(write_file_atomically(file, "bad"));
    CHECK(read_all(file / "kept") == "good");
    // The temp file it wrote is gone.
    CHECK(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

TEST_CASE("write_file_atomically leaves another writer's temp file alone", "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "registry.json";
    // Another process's write of this file, mid-way.
    const auto other = dir.path / "registry.json.tmp";
    std::ofstream(other) << "half";
    const auto other_numbered = dir.path / "registry.json.999999999-0.tmp";
    std::ofstream(other_numbered) << "half";
    REQUIRE(write_file_atomically(file, "whole"));
    CHECK(read_all(file) == "whole");
    CHECK(read_all(other) == "half");
    CHECK(read_all(other_numbered) == "half");
}

TEST_CASE("write_file_atomically survives several writers of one file", "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "progress.json";
    const std::string a(256 * 1024, 'a');
    const std::string b(64 * 1024, 'b');
    // Catch2's assertions are not thread-safe: count failures, check after.
    std::atomic<int> failed{0};
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back([&, i] {
            for (int n = 0; n < 5; ++n) {
                if (!write_file_atomically(file, i % 2 ? a : b)) {
                    ++failed;
                }
            }
        });
    }
    for (auto& t : writers) {
        t.join();
    }
    REQUIRE(failed == 0);
    const auto got = read_all(file);
    REQUIRE((got == a || got == b));
    REQUIRE(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

#ifndef _WIN32
TEST_CASE("An owner-only file is never readable by others, whatever the umask", "[util][atomic_file]")
{
    using seventhslide::util::FileAccess;
    TempDir dir;
    const auto file = dir.path / "computer_key.pem";
    const auto mode = [](const fs::path& p) {
        struct stat st {};
        REQUIRE(::stat(p.c_str(), &st) == 0);
        return st.st_mode & 0777;
    };
    // The most open umask there is, and a temp file a crash left world-readable.
    const auto before = ::umask(0);
    std::ofstream(dir.path / "computer_key.pem.tmp") << "left over";
    ::chmod((dir.path / "computer_key.pem.tmp").c_str(), 0644);
    const bool written = write_file_atomically(file, "secret", FileAccess::OwnerOnly);
    const bool shared = write_file_atomically(dir.path / "shared.json", "{}");
    ::umask(before);
    REQUIRE(written);
    CHECK(mode(file) == 0600);
    CHECK(read_all(file) == "secret");
    // An ordinary file is as the umask says.
    REQUIRE(shared);
    CHECK(mode(dir.path / "shared.json") == 0666);
}
#endif

TEST_CASE("write_file_atomically writes every byte, or none", "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "big.json";
    std::string big(3 * 1024 * 1024 + 7, '\0');
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<char>('a' + i % 26);
    }
    REQUIRE(write_file_atomically(file, big));
    REQUIRE(read_all(file) == big);
    REQUIRE(write_file_atomically(file, ""));
    REQUIRE(fs::exists(file));
    REQUIRE(read_all(file).empty());
}

#ifndef _WIN32
TEST_CASE("write_file_atomically keeps an owner-only file from others from the start",
          "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "secret.pem";
    const auto others = fs::perms::group_all | fs::perms::others_all;
    const mode_t umask_before = ::umask(022);
    // A temp file a crash left, readable by all, changes nothing.
    std::ofstream(dir.path / "secret.pem.tmp") << "stale";
    fs::permissions(dir.path / "secret.pem.tmp", fs::perms::owner_read | fs::perms::owner_write
                                                     | fs::perms::group_read | fs::perms::others_read);
    const bool written = write_file_atomically(file, "key", seventhslide::util::FileAccess::OwnerOnly);
    const bool shared = write_file_atomically(dir.path / "plain.json", "{}");
    ::umask(umask_before);
    REQUIRE(written);
    REQUIRE(shared);
    CHECK(read_all(file) == "key");
    CHECK((fs::status(file).permissions() & others) == fs::perms::none);
    CHECK((fs::status(dir.path / "plain.json").permissions() & fs::perms::others_read)
          == fs::perms::others_read);
}
#endif

TEST_CASE("write_cache_file survives several writers of one file", "[util][atomic_file]")
{
    TempDir dir;
    const auto file = dir.path / "thumb.jpg";
    const std::string a(256 * 1024, 'a');
    const std::string b(64 * 1024, 'b');
    // Catch2's assertions are not thread-safe: count failures, check after.
    std::atomic<int> failed{0};
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i) {
        writers.emplace_back([&, i] {
            for (int n = 0; n < 20; ++n) {
                if (!seventhslide::util::write_cache_file(file, i % 2 ? a : b)) {
                    ++failed;
                }
            }
        });
    }
    for (auto& t : writers) {
        t.join();
    }
    REQUIRE(failed == 0);
    const auto got = read_all(file);
    REQUIRE((got == a || got == b));
    REQUIRE(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

TEST_CASE("write_new_file_atomically puts one of several writers' files in place, and keeps it",
          "[util][atomic_file]")
{
    using seventhslide::util::NewFile;
    TempDir dir;
    const auto file = dir.path / "0000000001.json";
    for (int round = 0; round < 20; ++round) {
        fs::remove(file);
        std::atomic<bool> go{false};
        std::atomic<int> written{0};
        std::atomic<int> there{0};
        std::vector<std::thread> writers;
        for (int i = 0; i < 4; ++i) {
            writers.emplace_back([&, i] {
                while (!go) {
                    std::this_thread::yield();
                }
                const auto placed =
                    seventhslide::util::write_new_file_atomically(file, std::string(64 * 1024, static_cast<char>('a' + i)));
                if (placed == NewFile::Written) {
                    ++written;
                } else if (placed == NewFile::There) {
                    ++there;
                }
            });
        }
        go = true;
        for (auto& t : writers) {
            t.join();
        }
        INFO("round " << round);
        REQUIRE(written == 1);
        REQUIRE(there == 3);
        const auto got = read_all(file);
        REQUIRE(got.size() == 64 * 1024);
        REQUIRE(got.find_first_not_of(got.front()) == std::string::npos);
        REQUIRE(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
    }
    // One there already stays as it was.
    const auto before = read_all(file);
    CHECK(seventhslide::util::write_new_file_atomically(file, "other") == NewFile::There);
    CHECK(read_all(file) == before);
}

#ifndef _WIN32
TEST_CASE("A new file is put in place by each way a filesystem may have", "[util][atomic_file]")
{
    using seventhslide::util::NewFile;
    using seventhslide::util::detail::Placement;
    using seventhslide::util::detail::place_new_file;
    for (const auto how : {Placement::Best, Placement::Link, Placement::LookThenRename}) {
        INFO("way " << static_cast<int>(how));
        TempDir dir;
        const auto file = dir.path / "1.json";
        const auto temp = dir.path / "1.json.1-0.tmp";
        std::ofstream(temp) << "first";
        CHECK(place_new_file(temp, file, how) == NewFile::Written);
        CHECK(read_all(file) == "first");
        CHECK_FALSE(fs::exists(temp));
        // One there already stays as it was.
        std::ofstream(temp) << "second";
        CHECK(place_new_file(temp, file, how) == NewFile::There);
        CHECK(read_all(file) == "first");
        CHECK(fs::exists(temp));
    }
}

TEST_CASE("A link refused as there already, though it was made, is a new file written",
          "[util][atomic_file]")
{
    // NFSv3 sends a request again when its answer is slow: the second finds
    // the link the first made.
    using seventhslide::util::NewFile;
    using seventhslide::util::detail::Placement;
    TempDir dir;
    const auto file = dir.path / "1.json";
    const auto temp = dir.path / "1.json.1-0.tmp";
    std::ofstream(temp) << "entry";
    REQUIRE(::link(temp.c_str(), file.c_str()) == 0);
    CHECK(seventhslide::util::detail::place_new_file(temp, file, Placement::Link) == NewFile::Written);
    CHECK(read_all(file) == "entry");
    CHECK_FALSE(fs::exists(temp));
}
#endif

TEST_CASE("remove_stale_cache_temps clears what a crashed writer left", "[util][atomic_file]")
{
    TempDir dir;
    const auto tier = dir.path / "160";
    fs::create_directories(tier);
    const auto touch = [](const fs::path& p) { std::ofstream(p) << "x"; };
    const auto own = std::to_string(this_process_id());
    touch(tier / "a.jpg");
    touch(tier / "a.jpg.999999999-3.tmp");      // another process's leftover
    touch(dir.path / "1.stamp.999999999-0.tmp"); // at the top as well
    touch(tier / ("b.jpg." + own + "-7.tmp"));  // this process, mid-write
    touch(tier / "notes.tmp");                   // not a write_cache_file name
    touch(tier / "c.jpg.x-1.tmp");

    seventhslide::util::remove_stale_cache_temps(dir.path);

    CHECK(fs::exists(tier / "a.jpg"));
    CHECK_FALSE(fs::exists(tier / "a.jpg.999999999-3.tmp"));
    CHECK_FALSE(fs::exists(dir.path / "1.stamp.999999999-0.tmp"));
    CHECK(fs::exists(tier / ("b.jpg." + own + "-7.tmp")));
    CHECK(fs::exists(tier / "notes.tmp"));
    CHECK(fs::exists(tier / "c.jpg.x-1.tmp"));
}

TEST_CASE("remove_old_temps clears any writer's temps once they are old", "[util][atomic_file]")
{
    TempDir dir;
    const auto place = dir.path / "log" / "church";
    fs::create_directories(place);
    const auto touch = [](const fs::path& p, std::chrono::minutes age) {
        std::ofstream(p) << "x";
        fs::last_write_time(p, fs::file_time_type::clock::now() - age);
    };
    const auto own = std::to_string(this_process_id());
    touch(place / "3.json.999999999-3.tmp", std::chrono::minutes(90));   // a crash's
    touch(place / ("4.json." + own + "-1.tmp"), std::chrono::minutes(90)); // this pid, before
    touch(place / "5.json.999999999-4.tmp", std::chrono::minutes(1));     // still being written
    touch(place / "2.json", std::chrono::minutes(90));
    touch(place / "notes.tmp", std::chrono::minutes(90));

    seventhslide::util::remove_old_temps(dir.path, std::chrono::hours(1));

    CHECK_FALSE(fs::exists(place / "3.json.999999999-3.tmp"));
    CHECK_FALSE(fs::exists(place / ("4.json." + own + "-1.tmp")));
    CHECK(fs::exists(place / "5.json.999999999-4.tmp"));
    CHECK(fs::exists(place / "2.json"));
    CHECK(fs::exists(place / "notes.tmp"));
}

TEST_CASE("remove_old_temps can sweep one folder and none under it", "[util][atomic_file]")
{
    TempDir dir;
    const auto under = dir.path / "libraries";
    fs::create_directories(under);
    const auto touch = [](const fs::path& p) {
        std::ofstream(p) << "x";
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::minutes(90));
    };
    touch(dir.path / "machine.json.999999999-3.tmp");
    touch(under / "library.json.999999999-4.tmp");

    seventhslide::util::remove_old_temps(dir.path, std::chrono::hours(1),
                                         seventhslide::util::Sweep::Folder);

    CHECK_FALSE(fs::exists(dir.path / "machine.json.999999999-3.tmp"));
    CHECK(fs::exists(under / "library.json.999999999-4.tmp"));
}
