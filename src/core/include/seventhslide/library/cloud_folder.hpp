#pragma once

// Folders a cloud service's desktop app keeps in sync: Dropbox, Google Drive,
// OneDrive, iCloud Drive and the like.
//
// A library must not live in one. Its database is a file SQLite changes in
// place, beside ``-wal`` and ``-shm`` files that have to match it. A sync app
// uploads each of the three whenever it sees fit, and cannot merge two
// computers' edits of one file, so a library in a synced folder can be
// damaged or lose one computer's changes. The app warns rather than refuses:
// going ahead is the operator's call.
//
// A synced folder is recognised by the names the services' apps give their
// folders, and by marker files some of them leave in the folder they sync. A
// synced folder renamed to something unrecognisable is missed, and a folder
// that only looks like one gets a warning it did not need; both are
// acceptable for a warning.
//
// Seam: plain C++20 + std::filesystem (no Qt).

#include <filesystem>
#include <string>

namespace seventhslide::library {

/// The service whose app syncs ``folder`` or a folder it is in ("Dropbox",
/// "Google Drive", ...), or empty when none is recognised. Reads the names of
/// ``folder`` and the folders above it, and looks for marker files in each;
/// ``folder`` itself need not exist.
[[nodiscard]] std::string cloud_sync_service(const std::filesystem::path& folder);

}  // namespace seventhslide::library
