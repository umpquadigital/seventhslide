#pragma once

// A library the link syncs: where its store and its media are on this
// computer. The member list is in the store; what each peer may do follows
// from it.
//
// Seam: the sync engine's store and media declarations.

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/sync/gate.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace seventhslide::link {

struct SyncedLibrary {
    std::string uid;
    /// Shared: the link reads and writes files in it off the window's
    /// thread, and holds it until that is done, even once the library is
    /// let go of.
    std::shared_ptr<sync::SyncStore> store;
    /// The library's media folder (``library::media_root``).
    std::filesystem::path media;
    /// Where the pieces' hashes of its media files are kept
    /// (``sync::PieceHashes``); none are kept when empty.
    std::filesystem::path pieces;
    /// The media files the library's records own that are not on the disk
    /// (``SyncEngine::missing_media``), asked of every peer; may be empty.
    std::function<std::vector<db::MediaDigest>()> missing_media;
    /// The bytes of media the library's size limit has room for; -1 (or no
    /// function) for no limit. A file larger than that, which an arriving
    /// change file names, is not fetched before it is stored: the engine
    /// decides then whether it fits.
    std::function<std::int64_t()> media_room;
    /// What this computer has applied of each computer's change files (its
    /// own sent among them), said to every peer with what it has, so the
    /// others know when old files may go (``sync/housekeeping.hpp``); may be
    /// empty.
    std::function<std::map<std::string, std::int64_t>()> applied;
    /// How far the files in ``store`` vouch for each computer's, for every
    /// connection's gate. ``Link::add_library`` makes it, and it is read
    /// again off the window's thread whenever files are stored.
    std::shared_ptr<sync::Vouches> vouches;
};

}  // namespace seventhslide::link
