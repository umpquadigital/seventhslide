#pragma once

// The media files arriving over one connection (``Peer``), as their pieces
// come (``sync/media_pieces.hpp``). A fetch begins where the part on the
// disk ends (``begin``), when the hashes kept for the file are those the
// part was checked against and make the root it is declared by; otherwise
// from the start. The pieces' hashes come first, and must make the root;
// then the bytes, each piece held in memory until whole, then checked
// against its hash and only then written to the part, a piece at a time on
// the link's piece thread: a SHA-256 of 4 MiB does not belong on the
// window's thread.
//
// While pieces wait for that thread there is no ``has_room``, and the
// connection stops reading, so TCP holds a fast sender back. One piece of
// one file is taken at a time (a sender finishes a piece before it turns to
// another file), so a connection holds a few pieces in memory however many
// files it fetches.
//
// The last piece written, the file is put in place for good on the link's
// media thread. A part gone on from was written before, maybe before the
// power went: it is read through against its hashes first, and cut back to
// its first bad piece if it has one. A part written in this run alone is not
// (``PeerShared::parts_written``): a transfer moved to another connection
// costs no read of a large file.
//
// Work on a part may outlast the connection that began it, and a part has
// one writer: no connection begins on a part while that work goes on
// (``free_to_begin``, ``PeerShared::parts_at_work``).
//
// A file handed over to another connection (``hand_over``) is taken to the end
// of the piece coming, and let go of once what came is written: the other goes
// on from there, losing nothing but what was already on its way.
//
// What comes of each file is told (``Events``). Which files are asked for,
// and of whom, is the connection's.
//
// Seam: Qt Core (QFile), the file system.

#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/link/synced_library.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QThreadPool;

namespace seventhslide::link {

struct PeerShared;

/// How much of a file on its way is here: ``bytes`` of ``size``, written
/// and checked.
struct Arriving {
    std::int64_t bytes = 0;
    std::int64_t size = 0;
    bool operator==(const Arriving&) const = default;
};

/// A file whose bytes are coming: how many are still to come, and whether a
/// piece of it is part way here (the rest of it comes before it can go
/// elsewhere).
struct Flowing {
    std::int64_t left = 0;
    bool mid_piece = false;
};

class MediaIn {
public:
    /// Each with the library and the path; ``why`` has ``%1`` for the path.
    struct Events {
        /// ``bytes`` of it are on the disk, checked.
        std::function<void(const std::string&, const std::string&, std::int64_t bytes)> progress;
        /// It was not taken, and the computer that sent it is why: not what
        /// was declared, or a piece that does not match.
        std::function<void(const std::string&, const std::string&, const QString& why)> refused;
        /// It could not be taken, and this computer is why (the disk): no
        /// one else would do better just now.
        std::function<void(const std::string&, const std::string&, const QString& why)> failed;
        /// What was on the disk of it from before turned out bad, and was
        /// cut back: no one is to blame, and it is to be fetched on.
        std::function<void(const std::string&, const std::string&, const QString& why)> again;
        /// It came whole, and is in place.
        std::function<void(const std::string&, const std::string&)> done;
        /// There is ``has_room`` again.
        std::function<void()> room;
        /// It was handed over (``hand_over``), and all that came of it is
        /// written: another connection may go on from there.
        std::function<void(const std::string&, const std::string&)> handed_over;
    };

    /// ``shared`` and ``libraries`` are the connection's, and outlive it.
    MediaIn(PeerShared& shared, const std::vector<SyncedLibrary>& libraries, Events events);
    MediaIn(const MediaIn&) = delete;
    MediaIn& operator=(const MediaIn&) = delete;

    /// Whether the part of ``path`` is free for a connection to begin on: no
    /// work on it goes on, this connection's or one's gone since.
    [[nodiscard]] bool free_to_begin(const std::string& library, const std::string& path) const;
    /// Fetch ``digest`` of ``lib`` (this connection writes its part now):
    /// from the offset returned.
    std::int64_t begin(const SyncedLibrary& lib, const db::MediaDigest& digest);
    /// Whether ``path`` of ``library`` is being fetched and its hashes are
    /// what comes next of it.
    [[nodiscard]] bool expects_hashes(const std::string& library, const std::string& path) const;
    /// Whether ``path`` of ``library`` is being fetched and its bytes from
    /// ``offset`` are what comes next of it.
    [[nodiscard]] bool expects_bytes(const std::string& library, const std::string& path,
                                     std::int64_t offset) const;
    /// The pieces' hashes of ``path`` came.
    void take_hashes(const std::string& library, const std::string& path, const QByteArray& hashes);
    /// Bytes of ``path`` came, from ``offset``; ``last``: the sender has no
    /// more of it.
    void take_bytes(const std::string& library, const std::string& path, std::int64_t offset,
                    const QByteArray& bytes, bool last);

    /// No more of ``path`` is taken here: the pieces written stay on the
    /// disk, for a fetch to go on from.
    void forget(const std::string& library, const std::string& path);
    /// Take ``path`` of ``library`` to the end of the piece coming, and no
    /// further: once that is written, it is let go of (``Events::handed_over``),
    /// never at once. False when it is not coming here, or is all here.
    bool hand_over(const std::string& library, const std::string& path);
    void forget_library(const std::string& library);
    void forget_all();
    /// The files of ``library`` this connection is writing, by path.
    [[nodiscard]] std::map<std::string, Arriving> arriving(const std::string& library) const;
    /// Of those, the ones whose bytes are coming now (their hashes came, and
    /// they are not handed over).
    [[nodiscard]] std::map<std::string, Flowing> flowing(const std::string& library) const;
    /// Whether the bytes of any file are coming now, of any library.
    [[nodiscard]] bool expecting() const;
    /// Whether ``path`` of ``library`` is being put in place.
    [[nodiscard]] bool placing(const std::string& library, const std::string& path) const;
    /// Whether any piece waits to be written, or any file to go in place.
    [[nodiscard]] bool busy() const { return writing_ || !waiting_.empty() || !placing_.empty(); }
    /// Whether more may be read: too many pieces wait to be written otherwise.
    [[nodiscard]] bool has_room() const;

private:
    using Key = std::pair<std::string, std::string>;
    /// A file arriving: its declaration, the hashes it is checked against
    /// once they came, how much of it is on the disk, how much has come,
    /// and the piece coming.
    struct Part {
        db::MediaDigest digest;
        std::shared_ptr<const std::string> hashes;
        std::int64_t written = 0;
        std::int64_t received = 0;
        std::string piece;
        /// Gone on from a part on the disk, not written in this run alone: it
        /// is read through once whole.
        bool resumed = false;
        /// All of it has come.
        bool whole = false;
        /// Taken no further than the piece coming (``hand_over``).
        bool handing_over = false;
    };
    /// A whole piece, waiting to be checked and written.
    struct WholePiece {
        Key key;
        std::filesystem::path file;
        std::shared_ptr<const std::string> bytes;
        std::string hash;
        std::int64_t at = 0;
        /// Of a part written in this run alone so far (``Part::resumed``).
        bool fresh = false;
    };
    enum class Written { Good, Damaged, Failed };
    enum class Placed { Good, Damaged, Failed };

    [[nodiscard]] const SyncedLibrary* library(const std::string& uid) const;
    /// ``work()`` on ``pool`` with the part of ``key`` held, then ``then``
    /// with what it returned, unless this connection is gone by then; ``always``
    /// even so, for what is known of the part whichever connection goes on.
    template <typename Work, typename Then, typename Always>
    void at_work(const Key& key, QThreadPool& pool, Work work, Then then, Always always);
    /// Take no more of ``key``, and let go of what waits of it.
    void drop(const Key& key);
    /// Write the next piece waiting, unless one is being written.
    void write_next();
    void piece_written(const Key& key, Written result, std::int64_t bytes);
    /// Put the whole part of ``key`` in place.
    void finish(const Key& key);
    void finished(const Key& key, Placed result);
    /// Let go of ``key`` if it was handed over and all that came of it is
    /// written.
    void hand_over_if_done(const Key& key);

    PeerShared& shared_;
    const std::vector<SyncedLibrary>& libraries_;
    Events events_;
    std::map<Key, Part> parts_;
    std::deque<WholePiece> waiting_;
    bool writing_ = false;
    /// The file whose piece is only partly here.
    std::optional<Key> collecting_;
    std::set<Key> placing_;
    /// Found bad on the disk once already: a second time, the disk is why.
    std::set<Key> cut_back_;
    /// Tells work off the connection's thread whether this is still here:
    /// gone first.
    QObject context_;
};

}  // namespace seventhslide::link
