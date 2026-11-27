#pragma once

// How many files this process may have open at once.
//
// Plain C++20 (no Qt).

namespace seventhslide::util {

/// Raise this process's soft limit on open files to its hard limit, or to
/// 10240 (macOS's OPEN_MAX) when that is lower. macOS gives a process that
/// Finder or launchd starts a soft limit of 256. Each library open costs
/// about 5 (the db, its -wal and -shm, and its thread's wakeup pipe), so a
/// few dozen libraries plus sync's connections run out. Then libraries fail
/// to open, and a listening socket stops accepting. macOS only: elsewhere
/// the system's limit (1024 on Linux, kept low for select()) is left alone.
void raise_open_file_limit() noexcept;

}  // namespace seventhslide::util
