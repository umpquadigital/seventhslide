#pragma once

// Replace a file's contents so a crash or power loss mid-write cannot leave it
// truncated or empty.
//
// Plain C++20 (no Qt) over the platform's file calls.

#include <chrono>
#include <filesystem>
#include <string_view>

namespace seventhslide::util {

/// Who may read a file written here. ``OwnerOnly`` holds from the moment the
/// file exists, so a secret is never readable by others, even briefly. POSIX
/// only: on Windows a file takes the access rules of its folder.
enum class FileAccess { Default, OwnerOnly };

/// Write ``contents`` to a temp file of this call's own beside ``path``
/// (``<path>.<pid>-<n>.tmp``), flush it to the disk, and rename it over
/// ``path``. Several writers of one ``path`` at once, in this process or
/// others, each write their own temp file; the last rename wins. False when a
/// step fails (making the file ``access`` included); ``path`` then keeps its
/// old contents and the temp file is removed. The parent directory must
/// exist.
[[nodiscard]] bool write_file_atomically(const std::filesystem::path& path,
                                         std::string_view contents,
                                         FileAccess access = FileAccess::Default);

/// What ``write_new_file_atomically`` did.
enum class NewFile { Written, There, Failed };

/// Write ``contents`` to ``path``, which is not there yet, for good: to a temp
/// file of this call's own, flushed to the disk, then put in place in one step
/// that refuses when ``path`` is there. Of two writes of one ``path`` at once,
/// one is ``There`` and the other's file stays whole. On a filesystem that has
/// no such step (no hard links, nor a rename that refuses: FAT outside Linux)
/// ``path`` is looked for and then renamed to, which a write at that moment
/// can still replace. The parent directory must exist.
[[nodiscard]] NewFile write_new_file_atomically(const std::filesystem::path& path,
                                                std::string_view contents);

namespace detail {

/// How ``place_new_file`` puts a file in place: the best way the filesystem
/// has, or (for tests) one of the ways a filesystem without it falls back
/// to. Windows has one way.
enum class Placement { Best, Link, LookThenRename };

/// ``write_new_file_atomically``'s last step: put ``from``, written and
/// flushed, at ``to`` unless ``to`` is there. ``from`` is gone once it is
/// ``Written``.
[[nodiscard]] NewFile place_new_file(const std::filesystem::path& from,
                                     const std::filesystem::path& to,
                                     Placement how = Placement::Best);

}  // namespace detail

/// Put ``from``, a file already written, in place of ``to`` for good: its
/// contents are flushed to the disk first, then the rename, so a power loss
/// leaves either no ``to`` or the whole file. False when a step fails.
[[nodiscard]] bool move_file_durably(const std::filesystem::path& from,
                                     const std::filesystem::path& to);

/// Remove the file at ``path``: true once it is gone, or was never there. On
/// Windows a file that something has open (a reader, a virus scanner) refuses
/// to go; it is tried again for about a second. False when it still stays.
[[nodiscard]] bool remove_file(const std::filesystem::path& path);

/// For a file that can be rebuilt (a cache): ``path`` still changes in one
/// step, but nothing is flushed to the disk, and each call writes its own
/// uniquely named temp file, so several threads may write the same ``path``
/// at once. The last rename wins and no reader sees a partial file. (On
/// Windows a replace is refused while the file is held open, by another
/// writer's replace too; it is tried again for about a second.)
[[nodiscard]] bool write_cache_file(const std::filesystem::path& path,
                                    std::string_view contents);

/// Remove the temp files ``write_cache_file`` left anywhere under ``dir``
/// when a crash stopped it between the write and the rename. Those this
/// process is still writing are left alone.
void remove_stale_cache_temps(const std::filesystem::path& dir);

/// Where a sweep for temp files looks: ``dir`` and every folder under it, or
/// ``dir`` alone.
enum class Sweep { Tree, Folder };

/// Remove the temp files ``write_file_atomically``,
/// ``write_new_file_atomically`` and ``write_cache_file`` left in ``dir``
/// (and under it, for ``Sweep::Tree``) when a crash stopped them, last
/// written at least ``age`` ago. Any process's: by then no write still owns
/// one, and a folder another process writes to (or another computer, over
/// the network) may hold its temps too.
void remove_old_temps(const std::filesystem::path& dir, std::chrono::seconds age,
                      Sweep where = Sweep::Tree);

}  // namespace seventhslide::util
