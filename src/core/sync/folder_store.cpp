#include "seventhslide/sync/sync_store.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace seventhslide::sync {

bool is_machine_id(std::string_view machine)
{
    return !machine.empty() && machine.size() <= 64 && std::ranges::all_of(machine, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

bool is_fingerprint(std::string_view fingerprint)
{
    return fingerprint.size() == 64 && std::ranges::all_of(fingerprint, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool is_part_name(std::string_view name)
{
    return is_fingerprint(name);
}

bool is_library_uid(std::string_view uid)
{
    return uid.size() == 32 && std::ranges::all_of(uid, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

namespace {

/// ``0000000003.json`` for 3: the names sort as the numbers do.
std::string file_name(std::int64_t number)
{
    std::string digits = std::to_string(number);
    return std::string(digits.size() < 10 ? 10 - digits.size() : 0, '0') + digits + ".json";
}

std::optional<std::int64_t> number_of(const std::filesystem::path& file)
{
    if (file.extension() != ".json") {
        return std::nullopt;
    }
    const auto stem = file.stem().string();
    std::int64_t n = 0;
    const auto end = stem.data() + stem.size();
    const auto [at, ec] = std::from_chars(stem.data(), end, n);
    if (ec != std::errc{} || at != end || n < 1) {
        return std::nullopt;
    }
    return n;
}

void check_machine(std::string_view machine)
{
    if (!is_machine_id(machine)) {
        throw std::invalid_argument("A sync store was given a machine id that is not one");
    }
}

void check_number(std::int64_t n)
{
    if (n < 1) {
        throw std::invalid_argument("A sync store's files are numbered from 1");
    }
}

void check_fingerprint(std::string_view fingerprint)
{
    if (!is_fingerprint(fingerprint)) {
        throw std::invalid_argument("A locked admin key is named by its fingerprint");
    }
}

void check_part(std::string_view sha256)
{
    if (!is_part_name(sha256)) {
        throw std::invalid_argument("A snapshot's part is named by its SHA-256");
    }
}

/// Mark ``file`` as wanted now. A part that cannot be touched is still there.
void touch(const std::filesystem::path& file)
{
    std::error_code ec;
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);
}

/// Where ``remove_part`` puts a part while it looks at when it was touched:
/// a name ``parts()`` does not list.
std::filesystem::path going(std::filesystem::path part)
{
    part += ".going";
    return part;
}

/// The folders in ``folder``, by name.
std::vector<std::string> folders_in(const std::filesystem::path& folder)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        std::error_code dir_ec;
        if (entry.is_directory(dir_ec)) {
            out.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(out);
    return out;
}

/// The numbers of the files in ``folder``, lowest first.
std::vector<std::int64_t> numbers_in(const std::filesystem::path& folder)
{
    std::vector<std::int64_t> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (const auto n = number_of(entry.path())) {
            out.push_back(*n);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::string read_file(const std::filesystem::path& path, const std::string& what)
{
    auto contents = util::read_file(path);
    if (!contents) {
        throw std::runtime_error(what + " could not be read: " + path.string());
    }
    return std::move(*contents);
}

/// Write ``target``, which must not be there yet, whole (under a temp name no
/// listing takes, then put in place unless another write put it there first).
void write_new_file(const std::filesystem::path& target, std::string_view contents,
                    const std::string& what)
{
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    const auto written = std::filesystem::exists(target, ec) ? util::NewFile::There
                                                             : util::write_new_file_atomically(target, contents);
    if (written == util::NewFile::There) {
        throw AlreadyThere(what + " is there already: " + target.string());
    }
    if (written == util::NewFile::Failed) {
        throw std::runtime_error(what + " could not be written: " + target.string());
    }
}

/// What a file of ``kind`` is called in messages.
const char* name_of(Numbered kind)
{
    switch (kind) {
    case Numbered::Changes:
        return "A sync change file";
    case Numbered::Members:
        return "A sync member list entry";
    case Numbered::Log:
        return "A shared log entry";
    }
    return "A sync file";
}

}  // namespace

std::filesystem::path FolderStore::folder(Numbered kind) const
{
    switch (kind) {
    case Numbered::Changes:
        return root_ / "changes";
    case Numbered::Members:
        return root_ / "members";
    case Numbered::Log:
        return root_ / "log";
    }
    return root_ / "changes";
}

std::filesystem::path FolderStore::file(Numbered kind, std::string_view machine,
                                        std::int64_t sequence) const
{
    check_machine(machine);
    check_number(sequence);
    return folder(kind) / std::string(machine) / file_name(sequence);
}

std::filesystem::path FolderStore::snapshot(const SnapshotName& name) const
{
    check_machine(name.machine);
    check_number(name.files);
    return index_dir() / name.machine / file_name(name.files);
}

std::vector<std::string> FolderStore::machines(Numbered kind)
{
    auto out = folders_in(folder(kind));
    std::erase_if(out, [](const std::string& m) { return !is_machine_id(m); });
    return out;
}

std::vector<std::int64_t> FolderStore::sequences(Numbered kind, std::string_view machine)
{
    check_machine(machine);
    return numbers_in(folder(kind) / std::string(machine));
}

std::string FolderStore::read(Numbered kind, std::string_view machine, std::int64_t sequence)
{
    return read_file(file(kind, machine, sequence), name_of(kind));
}

void FolderStore::write(Numbered kind, std::string_view machine, std::int64_t sequence,
                        std::string_view contents)
{
    if (kind == Numbered::Changes && sequence <= dropped_through(machine)) {
        throw std::runtime_error("A sync change file this computer let go of is not taken again: "
                                 + file(kind, machine, sequence).string());
    }
    write_new_file(file(kind, machine, sequence), contents, name_of(kind));
}

std::int64_t FolderStore::dropped_through(std::string_view machine)
{
    check_machine(machine);
    std::ifstream in(folder(Numbered::Changes) / std::string(machine) / "dropped");
    std::int64_t n = 0;
    return (in >> n) && n > 0 ? n : 0;
}

void FolderStore::drop_changes_through(std::string_view machine, std::int64_t sequence)
{
    check_number(sequence);
    const auto through = std::max(sequence, dropped_through(machine));
    const auto place = folder(Numbered::Changes) / std::string(machine);
    std::error_code ec;
    std::filesystem::create_directories(place, ec);
    // The mark first: a file left by a stop part way goes next time, and none
    // comes back meanwhile.
    if (!util::write_file_atomically(place / "dropped", std::to_string(through))) {
        throw std::runtime_error("A sync folder could not note the files it let go of: " + place.string());
    }
    for (const auto n : sequences(Numbered::Changes, machine)) {
        if (n <= through) {
            std::filesystem::remove(file(Numbered::Changes, machine, n), ec);
        }
    }
}

std::vector<SnapshotName> FolderStore::snapshots()
{
    std::vector<SnapshotName> out;
    for (auto& machine : folders_in(index_dir())) {
        if (!is_machine_id(machine)) {
            continue;
        }
        for (const auto n : numbers_in(index_dir() / machine)) {
            out.push_back({machine, n});
        }
    }
    return out;
}

std::string FolderStore::read_snapshot(const SnapshotName& name)
{
    return read_file(snapshot(name), "A sync snapshot");
}

void FolderStore::write_snapshot(const SnapshotName& name, std::string_view contents)
{
    write_new_file(snapshot(name), contents, "A sync snapshot");
}

void FolderStore::move_aside(const std::filesystem::path& file, const std::string& name) const
{
    std::error_code ec;
    const auto damaged = root_ / "damaged";
    std::filesystem::create_directories(damaged, ec);
    auto target = damaged / name;
    for (int n = 2; std::filesystem::exists(target, ec); ++n) {
        target = damaged / (name + "-" + std::to_string(n));
    }
    std::filesystem::rename(file, target, ec);
    if (ec) {
        std::filesystem::remove(file, ec);  // gone either way: fetched again
    }
}

void FolderStore::set_aside(Numbered kind, std::string_view machine, std::int64_t sequence)
{
    const char* prefix = kind == Numbered::Changes ? "change-" : kind == Numbered::Members ? "member-" : "log-";
    move_aside(file(kind, machine, sequence),
               std::string(prefix) + std::string(machine) + "-" + std::to_string(sequence));
}

void FolderStore::set_aside_snapshot(const SnapshotName& name)
{
    move_aside(snapshot(name), "snapshot-" + name.machine + "-" + std::to_string(name.files));
}

bool FolderStore::remove_snapshot(const SnapshotName& name)
{
    std::error_code ec;
    std::filesystem::remove(snapshot(name), ec);
    return !ec;  // one not there is gone too
}

std::filesystem::path FolderStore::part(std::string_view sha256) const
{
    check_part(sha256);
    return parts_dir() / (std::string(sha256) + ".json");
}

std::vector<PartFile> FolderStore::parts()
{
    std::vector<PartFile> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(parts_dir(), ec)) {
        auto name = entry.path().stem().string();
        if (entry.path().extension() != ".json" || !is_part_name(name)) {
            continue;
        }
        std::error_code size_ec;
        std::error_code time_ec;
        const auto size = entry.file_size(size_ec);
        const auto touched = entry.last_write_time(time_ec);
        if (!size_ec && !time_ec) {
            out.push_back({std::move(name), static_cast<std::int64_t>(size), touched});
        }
    }
    std::ranges::sort(out, {}, &PartFile::sha256);
    return out;
}

bool FolderStore::keep_part(std::string_view sha256)
{
    const auto file = part(sha256);
    // Touched first: ``remove_part`` sees the touch, or has moved the part
    // away and it is not here.
    std::error_code ec;
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);
    std::error_code is_ec;
    return std::filesystem::is_regular_file(file, is_ec);
}

std::string FolderStore::read_part(std::string_view sha256)
{
    return read_file(part(sha256), "A snapshot's part");
}

void FolderStore::write_part(std::string_view sha256, std::string_view contents)
{
    check_part(sha256);
    if (crypto::sha256_hex(contents) != sha256) {
        throw std::invalid_argument("A snapshot's part is not what its name says");
    }
    write_checked_part(sha256, contents);
}

void FolderStore::write_checked_part(std::string_view sha256, std::string_view contents)
{
    const auto file = part(sha256);
    if (keep_part(sha256)) {
        // Read back, not trusted: one damaged here would be in every snapshot
        // this computer writes from now on.
        std::error_code size_ec;
        const auto size = std::filesystem::file_size(file, size_ec);
        if (!size_ec && size == contents.size()) {
            if (const auto had = util::read_file(file); had && *had == contents) {
                return;
            }
        }
        set_aside_part(sha256);
    }
    std::error_code ec;
    std::filesystem::create_directories(parts_dir(), ec);
    switch (util::write_new_file_atomically(file, contents)) {
    case util::NewFile::Written:
        return;
    case util::NewFile::There:
        // Another write put the same bytes there first.
        touch(file);
        return;
    case util::NewFile::Failed:
        break;
    }
    throw std::runtime_error("A snapshot's part could not be written: " + file.string());
}

bool FolderStore::remove_part(std::string_view sha256, std::filesystem::file_time_type touched_before)
{
    // Moved away first, then looked at: a ``keep_part`` from then on finds it
    // gone (and it is fetched again), and one before is seen in its time.
    const auto file = part(sha256);
    const auto away = going(file);
    std::error_code ec;
    std::filesystem::rename(file, away, ec);
    if (ec) {
        std::error_code there_ec;
        return !std::filesystem::exists(file, there_ec) && !there_ec;
    }
    const auto touched = std::filesystem::last_write_time(away, ec);
    if (!ec && touched < touched_before) {
        std::filesystem::remove(away, ec);
        if (!ec) {
            return true;
        }
    }
    // Wanted meanwhile: back, unless it was written again meanwhile.
    std::error_code there_ec;
    if (std::filesystem::exists(file, there_ec)) {
        std::filesystem::remove(away, ec);
    } else {
        std::filesystem::rename(away, file, ec);
    }
    return false;
}

void FolderStore::set_aside_part(std::string_view sha256)
{
    move_aside(part(sha256), "part-" + std::string(sha256));
}

std::vector<std::string> FolderStore::admin_keys()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(admin_keys_dir(), ec)) {
        if (entry.path().extension() == ".json" && is_fingerprint(entry.path().stem().string())) {
            out.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(out);
    return out;
}

std::string FolderStore::read_admin_key(std::string_view fingerprint)
{
    check_fingerprint(fingerprint);
    return read_file(admin_keys_dir() / (std::string(fingerprint) + ".json"),
                     "A sync locked admin key");
}

void FolderStore::write_admin_key(std::string_view fingerprint, std::string_view contents)
{
    check_fingerprint(fingerprint);
    write_new_file(admin_keys_dir() / (std::string(fingerprint) + ".json"), contents,
                   "A sync locked admin key");
}

bool FolderStore::remove_old_snapshots()
{
    std::error_code ec;
    const auto old = root_ / "snapshots";
    if (!std::filesystem::exists(old, ec)) {
        return false;
    }
    std::filesystem::remove_all(old, ec);
    std::error_code there_ec;
    return !ec && !std::filesystem::exists(old, there_ec) && !there_ec;
}

void FolderStore::remove_old_temps()
{
    // A write puts its temp in place within moments; one an hour old was
    // stopped by a crash.
    util::remove_old_temps(root_, std::chrono::hours(1));
    // A part ``remove_part`` was stopped with goes back, to go after its
    // grace as any other.
    std::vector<std::filesystem::path> stopped;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(parts_dir(), ec)) {
        if (entry.path().extension() == ".going") {
            stopped.push_back(entry.path());
        }
    }
    for (const auto& away : stopped) {
        const auto file = away.parent_path() / away.stem();
        std::error_code move_ec;
        if (std::filesystem::exists(file, move_ec)) {
            std::filesystem::remove(away, move_ec);
        } else {
            std::filesystem::rename(away, file, move_ec);
        }
    }
}

}  // namespace seventhslide::sync
