#include "seventhslide/util/atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seventhslide::util {
namespace {

#if defined(_WIN32)

/// Flush the file at ``path`` to the disk.
[[nodiscard]] bool sync_path(const std::filesystem::path& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = FlushFileBuffers(file) != 0;
    return CloseHandle(file) != 0 && ok;
}

/// Write ``contents`` to a new ``path``. With ``sync``, flush it to the disk
/// before returning, so a rename that follows cannot reach the disk first.
/// ``access`` changes nothing here: the file takes its folder's rules.
[[nodiscard]] bool write_contents(const std::filesystem::path& path,
                                  std::string_view contents,
                                  bool sync,
                                  FileAccess /*access*/)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    const char* data = contents.data();
    std::size_t left = contents.size();
    while (ok && left > 0) {
        const DWORD chunk = left > 0x40000000 ? 0x40000000 : static_cast<DWORD>(left);
        DWORD written = 0;
        ok = WriteFile(file, data, chunk, &written, nullptr) != 0 && written > 0;
        data += written;
        left -= written;
    }
    ok = ok && (!sync || FlushFileBuffers(file) != 0);
    return CloseHandle(file) != 0 && ok;
}

/// ``step`` (a call that sets the last error when it fails) until it works,
/// trying again for about a second, waiting longer each time, while it is
/// refused for access or sharing. A file open without delete sharing - a
/// reader, a virus scanner or a sync client looking at it, another thread's
/// replace of it still finishing - refuses to be replaced or removed on
/// Windows, which on POSIX would not stop either. Those let go soon.
template <typename Step>
[[nodiscard]] bool patiently(Step step)
{
    constexpr int k_tries = 12;
    for (int attempt = 0;; ++attempt) {
        if (step()) {
            return true;
        }
        const DWORD error = GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
            || attempt + 1 == k_tries) {
            return false;
        }
        Sleep(attempt == 0 ? 0 : std::min<DWORD>(DWORD{1} << (attempt - 1), 250));
    }
}

/// Put ``from`` in place of ``to`` in one step, so a crash leaves either the
/// old file or the new one - never neither. With ``sync``, return once the
/// move is on the disk.
[[nodiscard]] bool replace_file(const std::filesystem::path& from,
                                const std::filesystem::path& to,
                                bool sync)
{
    // Not every std::filesystem::rename on Windows replaces an existing file
    // (MinGW's does not); this is the documented atomic replace.
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | (sync ? MOVEFILE_WRITE_THROUGH : 0);
    return patiently([&] { return MoveFileExW(from.c_str(), to.c_str(), flags) != 0; });
}

/// Put ``from`` at ``to`` in one step, unless ``to`` is there. Windows has
/// one way.
[[nodiscard]] NewFile place_file(const std::filesystem::path& from,
                                 const std::filesystem::path& to,
                                 detail::Placement /*how*/)
{
    if (patiently([&] { return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0; })) {
        return NewFile::Written;
    }
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? NewFile::There : NewFile::Failed;
}

[[nodiscard]] bool remove_regular_file(const std::filesystem::path& path)
{
    return patiently([&] {
        return DeleteFileW(path.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND
               || GetLastError() == ERROR_PATH_NOT_FOUND;
    });
}

[[nodiscard]] unsigned long process_id()
{
    return GetCurrentProcessId();
}

#else

/// Flush ``fd`` to the disk. macOS's fsync stops at the drive's cache;
/// F_FULLFSYNC goes through it where the filesystem supports it.
[[nodiscard]] bool sync_fd(int fd)
{
#if defined(F_FULLFSYNC)
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return true;
    }
#endif
    return ::fsync(fd) == 0;
}

/// Flush the file at ``path`` to the disk.
[[nodiscard]] bool sync_path(const std::filesystem::path& path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool ok = sync_fd(fd);
    return ::close(fd) == 0 && ok;
}

/// Flush the folder ``path`` is in, where a rename to it lives. The file is in
/// place either way, so a folder that cannot be opened or flushed is let be.
void sync_folder_of(const std::filesystem::path& path)
{
    auto dir = path.parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        (void)sync_fd(dir_fd);
        ::close(dir_fd);
    }
}

/// Write ``contents`` to a new ``path``. With ``sync``, flush it to the disk
/// before returning, so a rename that follows cannot reach the disk first.
/// ``OwnerOnly`` is set before a byte is written; ``fchmod`` as well as the
/// mode given to ``open``, as a temp file a crash left keeps its old mode.
[[nodiscard]] bool write_contents(const std::filesystem::path& path,
                                  std::string_view contents,
                                  bool sync,
                                  FileAccess access)
{
    const bool owner_only = access == FileAccess::OwnerOnly;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, owner_only ? 0600 : 0666);
    if (fd < 0) {
        return false;
    }
    bool ok = !owner_only || ::fchmod(fd, 0600) == 0;
    const char* data = contents.data();
    std::size_t left = contents.size();
    while (ok && left > 0) {
        const ssize_t n = ::write(fd, data, left);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ok = n > 0;
        if (ok) {
            data += n;
            left -= static_cast<std::size_t>(n);
        }
    }
    ok = ok && (!sync || sync_fd(fd));
    return ::close(fd) == 0 && ok;
}

/// Put ``from`` in place of ``to`` in one step, so a crash leaves either the
/// old file or the new one - never neither. With ``sync``, return once the
/// move is on the disk.
[[nodiscard]] bool replace_file(const std::filesystem::path& from,
                                const std::filesystem::path& to,
                                bool sync)
{
    // POSIX rename() replaces the target atomically.
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        return false;
    }
    if (sync) {
        sync_folder_of(to);
    }
    return true;
}

/// Whether ``a`` and ``b`` name one file.
[[nodiscard]] bool same_file(const std::filesystem::path& a, const std::filesystem::path& b)
{
    struct stat sa {};
    struct stat sb {};
    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0
           && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/// Put ``from`` at ``to`` in one step, unless ``to`` is there.
[[nodiscard]] NewFile place_file(const std::filesystem::path& from,
                                 const std::filesystem::path& to,
                                 detail::Placement how)
{
    using detail::Placement;
#if defined(__linux__) || defined(__APPLE__)
    if (how == Placement::Best) {
#if defined(__APPLE__)
        const int renamed = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
#else
        const int renamed = ::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE);
#endif
        if (renamed == 0) {
            return NewFile::Written;
        }
        if (errno == EEXIST) {
            return NewFile::There;
        }
        if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP) {
            return NewFile::Failed;
        }
    }
    // A filesystem whose rename cannot refuse (NFS, some on macOS) has hard
    // links.
#endif
    if (how != Placement::LookThenRename) {
        if (::link(from.c_str(), to.c_str()) == 0) {
            ::unlink(from.c_str());
            return NewFile::Written;
        }
        if (errno == EEXIST) {
            // NFSv3 sends a request again when its answer is slow, and the
            // second finds the link the first made.
            if (same_file(from, to)) {
                ::unlink(from.c_str());
                return NewFile::Written;
            }
            return NewFile::There;
        }
        if (errno != EPERM && errno != ENOTSUP && errno != EOPNOTSUPP && errno != ENOSYS) {
            return NewFile::Failed;
        }
    }
    // No hard links here either: looked for, then renamed to.
    std::error_code ec;
    if (std::filesystem::exists(to, ec)) {
        return NewFile::There;
    }
    std::filesystem::rename(from, to, ec);
    return ec ? NewFile::Failed : NewFile::Written;
}

[[nodiscard]] unsigned long process_id()
{
    return static_cast<unsigned long>(::getpid());
}

[[nodiscard]] bool remove_regular_file(const std::filesystem::path& path)
{
    // An open file goes as any other: what has it open keeps reading it.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

#endif

[[nodiscard]] bool write_then_replace(const std::filesystem::path& temp_path,
                                      const std::filesystem::path& path,
                                      std::string_view contents,
                                      bool sync,
                                      FileAccess access = FileAccess::Default)
{
    if (!write_contents(temp_path, contents, sync, access) || !replace_file(temp_path, path, sync)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return false;
    }
    return true;
}

/// A temp name beside ``path`` that no other call uses: unique across threads
/// (the counter) and processes (the id).
[[nodiscard]] std::filesystem::path own_temp_path(const std::filesystem::path& path)
{
    static std::atomic<unsigned long> next{0};
    auto temp_path = path;
    temp_path += "." + std::to_string(process_id()) + "-" + std::to_string(next++) + ".tmp";
    return temp_path;
}

/// The process id in an ``own_temp_path`` name (``<name>.<pid>-<n>.tmp``),
/// or nullopt for any other name.
[[nodiscard]] std::optional<std::string> temp_writer(std::string_view name)
{
    constexpr std::string_view suffix = ".tmp";
    if (!name.ends_with(suffix)) {
        return std::nullopt;
    }
    name.remove_suffix(suffix.size());
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const auto tag = name.substr(dot + 1);
    const auto dash = tag.find('-');
    const auto digits = [](std::string_view s) {
        return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
    };
    if (dash == std::string_view::npos || !digits(tag.substr(0, dash))
        || !digits(tag.substr(dash + 1))) {
        return std::nullopt;
    }
    return std::string(tag.substr(0, dash));
}

/// Remove the temp files ``own_temp_path`` names in ``dir`` (and in the
/// folders under it, for ``Sweep::Tree``) that ``stale`` (given the writer's
/// process id and the file) says no write still owns.
template <typename Stale>
void remove_temps(const std::filesystem::path& dir, Sweep where, Stale stale)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        return;
    }
    std::vector<fs::path> found;
    const auto look = [&](const fs::directory_entry& entry) {
        const auto writer = temp_writer(entry.path().filename().string());
        std::error_code type_ec;
        if (writer && entry.is_regular_file(type_ec) && stale(*writer, entry)) {
            found.push_back(entry.path());
        }
    };
    if (where == Sweep::Tree) {
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::end(it);
             it.increment(ec)) {
            look(*it);
        }
    } else {
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it); it.increment(ec)) {
            look(*it);
        }
    }
    for (const auto& path : found) {
        fs::remove(path, ec);
    }
}

}  // namespace

bool write_file_atomically(const std::filesystem::path& path, std::string_view contents,
                           FileAccess access)
{
    return write_then_replace(own_temp_path(path), path, contents, true, access);
}

bool move_file_durably(const std::filesystem::path& from, const std::filesystem::path& to)
{
    return sync_path(from) && replace_file(from, to, true);
}

bool remove_file(const std::filesystem::path& path)
{
    return remove_regular_file(path);
}

NewFile write_new_file_atomically(const std::filesystem::path& path, std::string_view contents)
{
    const auto temp_path = own_temp_path(path);
    if (!write_contents(temp_path, contents, true, FileAccess::Default)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return NewFile::Failed;
    }
    const auto placed = detail::place_new_file(temp_path, path);
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);  // gone already, unless it was not placed
#if !defined(_WIN32)
    if (placed == NewFile::Written) {
        sync_folder_of(path);
    }
#endif
    return placed;
}

bool write_cache_file(const std::filesystem::path& path, std::string_view contents)
{
    return write_then_replace(own_temp_path(path), path, contents, false);
}

NewFile detail::place_new_file(const std::filesystem::path& from,
                               const std::filesystem::path& to,
                               Placement how)
{
    return place_file(from, to, how);
}

void remove_stale_cache_temps(const std::filesystem::path& dir)
{
    const auto own = std::to_string(process_id());
    remove_temps(dir, Sweep::Tree,
                 [&](const std::string& writer, const std::filesystem::directory_entry&) {
                     return writer != own;
                 });
}

void remove_old_temps(const std::filesystem::path& dir, std::chrono::seconds age, Sweep where)
{
    const auto before = std::filesystem::file_time_type::clock::now() - age;
    remove_temps(dir, where, [&](const std::string&, const std::filesystem::directory_entry& entry) {
        std::error_code ec;
        const auto written = entry.last_write_time(ec);
        return !ec && written < before;
    });
}

}  // namespace seventhslide::util
