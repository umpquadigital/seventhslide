#pragma once

// What the peer_*.cpp files share: reading a message's fields, the names of
// what is fetched, and the peer's own records of what is on its way. Not part
// of the public API.
// Seam: Qt Network.

#include "seventhslide/link/peer.hpp"

#include "seventhslide/db/sync_tables.hpp"
#include "seventhslide/sync/gate.hpp"
#include "seventhslide/sync/snapshot.hpp"

#include <QLoggingCategory>

#include <chrono>
#include <stdexcept>

namespace seventhslide::link {

// Defined in peer.cpp.
Q_DECLARE_LOGGING_CATEGORY(lc_link)

using nlohmann::json;

[[noreturn]] inline void malformed(const std::string& why)
{
    throw std::runtime_error("The other computer sent something that is not a sync message ("
                             + why + ")");
}

inline std::string text_of(const json& j, const char* name)
{
    const auto it = j.find(name);
    if (it == j.end() || !it->is_string()) {
        malformed(std::string("no ") + name);
    }
    return it->get<std::string>();
}

inline std::int64_t number_of(const json& j, const char* name)
{
    const auto it = j.find(name);
    if (it == j.end() || !it->is_number_integer() || it->get<std::int64_t>() < 0) {
        malformed(std::string("no ") + name);
    }
    return it->get<std::int64_t>();
}

inline std::string part_name_of(const json& j)
{
    auto sha256 = text_of(j, "sha256");
    if (!sync::is_part_name(sha256)) {
        malformed("a snapshot's part named by something not a SHA-256");
    }
    return sha256;
}

inline std::string media_path_of(const json& j)
{
    const auto path = text_of(j, "path");
    if (!db::is_sync_media_path(path)) {
        malformed("a media path that is not one");
    }
    return path;
}

inline const json& list_of(const json& j, const char* name)
{
    static const json k_none = json::array();
    const auto it = j.find(name);
    if (it == j.end()) {
        return k_none;
    }
    if (!it->is_array()) {
        malformed(std::string(name) + " is not a list");
    }
    return *it;
}

inline std::string item(const char* kind, const std::string& machine, std::int64_t n)
{
    return std::string(kind) + "/" + machine + "/" + std::to_string(n);
}

inline std::string item(const char* kind, const std::string& name)
{
    return std::string(kind) + "/" + name;
}

/// What names change file or index ``n`` of ``machine`` in a fetch.
inline std::string item(StoreFile kind, const std::string& machine, std::int64_t n)
{
    return item(store_file_name(kind), machine, n);
}

/// What names part ``sha256`` in a fetch.
inline std::string part_item(const std::string& sha256)
{
    return item(store_file_name(StoreFile::Part), sha256);
}

/// A change file waiting for the media it declares.
struct Peer::Pending {
    std::string machine;
    std::int64_t number = 0;
    std::shared_ptr<const std::string> text;
    std::set<std::string> needs;
    /// When it was let in: stored without its media a while after.
    std::chrono::steady_clock::time_point held_since = std::chrono::steady_clock::now();
};

/// A snapshot's index let in, waiting for the parts it names that are not in
/// the store.
struct Peer::Waiting {
    std::string machine;
    std::int64_t files = 0;
    std::shared_ptr<const std::string> text;
    /// The parts not here yet: their sizes, by SHA-256, as the index says.
    std::map<std::string, std::int64_t> needs;

    [[nodiscard]] std::string name() const { return item(StoreFile::Snapshot, machine, files); }
};

/// A change file or snapshot's index come, to be checked at the gate.
struct Peer::Arrived {
    std::string library;
    bool snapshot = false;
    std::string machine;
    std::int64_t number = 0;
    std::shared_ptr<const std::string> text;

    [[nodiscard]] std::string name() const
    {
        return item(snapshot ? StoreFile::Snapshot : StoreFile::Change, machine, number);
    }
};

/// What the gate said of an arrived file (``check_next``).
struct Peer::FileCheck {
    /// Why it was refused; empty when it was let in.
    std::string refused;
    /// Refused for now (``sync::NotYet``, or ``no_room``).
    bool not_yet = false;
    /// Not room on the disk for its parts: asked again once the other end
    /// next says what it has, not at once.
    bool no_room = false;
    /// The media a change file declares that was not on the disk.
    std::vector<db::MediaDigest> absent;
    /// The parts a snapshot's index names that are not in the store, and
    /// their sizes.
    std::map<std::string, std::int64_t> parts_absent;
};

/// What came of writing a change file or snapshot's index (``store_file``).
struct Peer::StoreOutcome {
    /// Why it was not written; empty when it was.
    std::string problem;
    /// The parts an index names that were gone from the store just before
    /// (housekeeping), and their sizes: it was not written.
    std::map<std::string, std::int64_t> gone;
};

/// What came of checking and writing a part (``part_came``).
struct Peer::PartCheck {
    /// Why it was not kept; empty when it was.
    std::string problem;
    /// It could not be written: this computer's trouble, not the sender's.
    bool ours = false;
};

/// The bytes announced, which come next: of a media file at ``path``, its
/// pieces' hashes, or a piece of a change file, snapshot's index or part,
/// ``path`` its item.
struct Peer::Incoming {
    enum class Kind { Media, Hashes, StoreFile };
    Kind kind = Kind::Media;
    std::string library;
    std::string path;
    std::int64_t offset = 0;
    /// Of a media piece, its bytes; of a store file, the whole file's.
    std::int64_t size = 0;
    /// A media piece is the file's last.
    bool last = false;
    bool accept = true;
    /// Of a store file, which kind it is and what names it: a change file's
    /// or index's machine, or a part's SHA-256.
    StoreFile file = StoreFile::Change;
    std::string name;
    /// A change file's sequence, or how many files an index's snapshot
    /// holds; none for a part.
    std::int64_t number = 0;
};

}  // namespace seventhslide::link
