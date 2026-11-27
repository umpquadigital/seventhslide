#pragma once

// What a link's connections (``Peer``) share, and the sizes things go over
// a connection in.
//
// Seam: Qt Core (QThreadPool).

#include "seventhslide/link/fetches.hpp"
#include "seventhslide/link/routes.hpp"

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <QHostAddress>
#include <QObject>
#include <QThreadPool>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace seventhslide::link {

struct Joining;
class Peer;

/// Media goes in frames of this size, between other messages: a part of a
/// piece (``sync::k_media_piece_bytes``), which it divides.
inline constexpr std::int64_t k_media_piece = 256 * 1024;
/// The most media files one connection asks for at once, and of those how many
/// large ones (``k_large_media`` and over): the other connections share the
/// work, and a faster one takes more of it. Enough that small files are not
/// asked for a round trip at a time, and that they go on coming beside a video.
inline constexpr std::size_t k_media_at_once = 16;
inline constexpr std::size_t k_large_at_once = 2;
inline constexpr std::int64_t k_large_media = 16 * 1024 * 1024;
/// The most parts of a snapshot one connection asks for at once: from the one
/// that sent its index.
inline constexpr std::size_t k_parts_at_once = 4;
/// The most parts a snapshot's index may name: 16 GiB of records in parts of
/// about 4 MiB (``sync::k_snapshot_part_bytes``). Ten Bibles and 2,000 songs
/// make some 13, and a hundred Bibles some 250. An index naming more is
/// refused: what waits here for its parts stays small.
inline constexpr std::size_t k_most_parts = 4096;

/// What a link's peers share: which connection fetches what, the threads
/// files are read through on, and how often a quiet connection is asked
/// whether it is still there.
struct PeerShared {
    Fetches fetches;
    /// Media files are worked out in pieces here, to be sent, and put in
    /// place once arrived, a file at a time: a disk read through is the
    /// cost.
    QThreadPool checks;
    /// The pieces of media arriving are checked and written here, a piece at
    /// a time: never behind a read through.
    QThreadPool pieces;
    /// Change files and snapshots' indexes and parts are checked, read and
    /// written here, a file at a time: never behind a video's check, and
    /// never two large files in memory at once.
    QThreadPool files;
    /// Asked after this long without a word sent.
    int ping_ms = 30000;
    /// Closed after this long without a word heard.
    int silent_ms = 120000;
    /// Closed after this long without having said hello: a connection that
    /// never says who it is holds nothing open for long.
    int hello_ms = 15000;
    /// A change file is kept from the store for the media it names this long
    /// at most, then stored without what has not come: that is fetched as
    /// any missing file is. Edits made after a big file, and every
    /// later edit of a computer switched off mid-upload, are not held behind
    /// it; pictures and songs, which come in moments, are never seen missing.
    int hold_for_media_ms = 60000;
    /// A connection asked for a snapshot's parts that sends no file for this
    /// long is taken not to have them: what waits for them is let go of, for
    /// another connection to fetch. A part of 16 MiB comes in pieces of 1 MiB,
    /// so it is one piece in two minutes, about 70 kbit/s.
    int parts_quiet_ms = 120000;
    /// Closed after this long, come from elsewhere, without TLS finished: all a
    /// real computer needs for that is a moment, and it holds a place meanwhile
    /// (``k_max_unadmitted``).
    int handshake_ms = 5000;
    /// The libraries this computer joins through the computer with this key
    /// (``Reach``): a connection that computer makes offers them in its hello,
    /// as one this computer makes does. Empty for none.
    std::function<std::vector<Joining>(const crypto::PublicKey&)> joining_with;
    /// This computer has paused syncing: nothing but the shared log moves over
    /// any connection (``Link::set_paused``).
    bool paused = false;
    /// What goes out of media to computers not on this network, over every
    /// connection: held to the upload speed cap (``Link::set_upload_cap``).
    UploadCap upload;
    /// Whether ``peer``'s computer is on this network: heard there, at the
    /// address it is connected at (``Reach``). Without it, none is.
    std::function<bool(const Peer& peer)> close_by;
    /// Ask the computers connected to tell the others of each library where
    /// this one is, and this one where they are (``Link::set_share_address``).
    bool share_address = true;
    /// The port this computer takes connections on, said when it asks; 0 while
    /// it takes none, and asks nothing.
    std::uint16_t listening_port = 0;
    /// Whether an address is one of a place's own networks, never passed on to
    /// another (``is_local_address`` unless given: tests treat loopback as
    /// far).
    std::function<bool(const QHostAddress&)> is_local;
    /// The connection clearly better placed than ``asking`` to fetch media
    /// ``file`` of ``library`` that would ask for it now (``Link``): null for
    /// none. A connection leaves such a file to that one, while it has room.
    std::function<Peer*(const Peer& asking, const std::string& library, const db::MediaDigest& file)> better_for;
    /// The parts of media files being worked on off this thread, by library
    /// and path: the connection that began it may have gone since, and a
    /// part has one writer (``MediaIn::free_to_begin``).
    std::set<std::pair<std::string, std::string>> parts_at_work;
    /// The parts written in this run alone, every piece checked as it came,
    /// by library and path: how much of each. Another connection going on from
    /// one need not read it through, as it must a part from before.
    std::map<std::pair<std::string, std::string>, std::int64_t> parts_written;
    /// A part of ``library`` was let go of with no connection on it now:
    /// another may begin on it.
    std::function<void(const std::string& library)> part_free;
    /// Something went into the store of ``library`` for a connection gone
    /// since (what it held, stored as it closed): as ``Peer::stored``.
    std::function<void(const std::string& library)> stored;
    /// How many of those writes are under way: the link is busy meanwhile.
    int storing_held = 0;
    /// The media change files were stored without (held long enough, or as
    /// their connection closed), by library and path: any connection fetches
    /// it until it is here, as it would a file the library says is missing,
    /// without waiting for the library to have taken the file in.
    std::map<std::string, std::map<std::string, db::MediaDigest>> owed_media;
    /// The bytes free on the disk at ``path``, asked off the window's thread
    /// before a snapshot's parts are fetched; -1 when it cannot be told.
    /// ``std::filesystem::space`` unless given (tests fake a full disk).
    std::function<std::int64_t(const std::filesystem::path& path)> free_space;
    /// The files too large to pass between computers that were logged: by
    /// the computer that sent one (empty for this one), library and name.
    /// Each is asked for again at every sync, and said once. A library let go
    /// of here forgets its own (``Link::remove_library``), and past 1,000 all
    /// are forgotten.
    std::set<std::tuple<std::string, std::string, std::string>> said_too_large;
    /// Whether ``file`` (as ``said_too_large``) is to be logged: not said yet.
    bool said_once(std::tuple<std::string, std::string, std::string> file)
    {
        if (said_too_large.size() >= 1000) {
            said_too_large.clear();
        }
        return said_too_large.insert(std::move(file)).second;
    }
    /// What that work reports back to, whichever connections are left: gone
    /// first, so nothing reports to what is half gone.
    QObject context;
};

}  // namespace seventhslide::link
