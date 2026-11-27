#pragma once

// What goes out over one connection (``Peer``), in turn: messages first;
// then the change files and snapshots' indexes asked for, then snapshots'
// parts, each read from the store when its turn comes (one at a time, on the
// link's file thread) and sent whole, or in ``piece`` frames when larger than
// ``k_inline_file``; then media, each file's pieces' hashes first
// (``pieces``, worked out on the link's media thread when not kept), then its
// bytes a frame at a time, the files taking turns a piece at a time. Change
// files go before parts and parts before media, so a song edit is never held
// up by a snapshot, nor a snapshot by a video. A file begun in pieces is
// finished before another begins, and one read goes before another is read.
//
// Never more than the write-ahead is waiting to go out on the network, so a
// video is not read into memory faster than the network takes it; the
// connection writes on as what went drains (``pump``).
//
// Media to a computer not on this network goes no faster than the upload speed
// cap lets (``PeerShared::upload``), shared with every other connection, and in
// frames small enough for the uplink to be used in short bursts. What waits for
// it waits on a timer; messages and change files go meanwhile.
//
// Seam: Qt Network (the connection's socket).

#include "seventhslide/link/synced_library.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QSslSocket>
#include <QString>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::link {

struct PeerShared;

/// What a store file sent whole, or in pieces, is.
enum class StoreFile { Change, Snapshot, Part };

/// What a store file of ``kind`` is called in messages (``type``, and
/// ``kind`` in a ``missing`` or a ``piece``) and in a fetch's item:
/// "change", "snapshot" (an index) or "part".
[[nodiscard]] constexpr const char* store_file_name(StoreFile kind)
{
    switch (kind) {
    case StoreFile::Change:
        return "change";
    case StoreFile::Snapshot:
        return "snapshot";
    case StoreFile::Part:
        return "part";
    }
    return "change";
}

/// The kind of store file called ``name``; none for another name.
[[nodiscard]] constexpr std::optional<StoreFile> store_file_kind(std::string_view name)
{
    for (const auto kind : {StoreFile::Change, StoreFile::Snapshot, StoreFile::Part}) {
        if (name == store_file_name(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

class Outbox {
public:
    /// ``socket``, ``shared`` and ``libraries`` are the connection's, and
    /// outlive it.
    Outbox(QSslSocket& socket, PeerShared& shared, const std::vector<SyncedLibrary>& libraries);
    Outbox(const Outbox&) = delete;
    Outbox& operator=(const Outbox&) = delete;

    /// ``message`` goes next, ahead of any file.
    void message(const nlohmann::json& message);
    /// Change file ``number`` (or, ``Snapshot``, the index of the snapshot of
    /// that many files) of ``machine``, in ``library``: read and sent in its
    /// turn, or ``missing`` when it cannot be read or is larger than
    /// ``sync::k_largest_store_file`` (which is logged, once).
    void file(std::string library, StoreFile kind, std::string machine, std::int64_t number);
    /// Snapshot part ``sha256`` of ``library``: likewise, after the change
    /// files and indexes. One here that is not what its name says is set
    /// aside, and ``missing``: the other end would blame this computer.
    void part(std::string library, std::string sha256);
    /// Media file ``path`` of ``lib``, from ``offset`` (a piece's start):
    /// its hashes, then its bytes; ``missing`` when it is not here, cannot
    /// be read, or is too large for its hashes to go in one frame.
    void media(const SyncedLibrary& lib, const std::string& path, std::int64_t offset);
    /// The other end has enough of ``path``: no more of it goes.
    void cancel_media(const std::string& library, const std::string& path);
    /// Nothing more of ``library`` goes.
    void drop_library(const std::string& library);
    /// The files asked for are dropped (the connection paused); messages
    /// still go.
    void drop_files();
    /// The connection is closed: nothing more is written.
    void close() { closed_ = true; }
    /// Whether what goes over this connection is held to the upload speed cap,
    /// asked as media goes: by default, it is.
    void set_capped(std::function<bool()> capped) { capped_ = std::move(capped); }

    /// Write what is next, while the write-ahead has room.
    void pump();
    /// Bytes written to the connection and not yet out on the network.
    [[nodiscard]] qint64 unsent() const;
    /// Whether anything is still to go out, or on its way out.
    [[nodiscard]] bool busy() const;
    /// Whether anything of ``library`` is still to go out.
    [[nodiscard]] bool sending(const std::string& library) const;
    /// The media files of ``library`` still to go out.
    [[nodiscard]] std::set<std::string> media_going(const std::string& library) const;
    /// How long since anything was written.
    [[nodiscard]] qint64 quiet_ms() const { return said_.elapsed(); }

private:
    struct FileJob {
        std::string library;
        StoreFile kind = StoreFile::Change;
        /// The machine, or a part's SHA-256.
        std::string name;
        std::int64_t number = 0;
        std::shared_ptr<const std::string> text;
        std::int64_t offset = 0;

        [[nodiscard]] bool same(const FileJob& other) const
        {
            return library == other.library && kind == other.kind && name == other.name && number == other.number;
        }
    };
    struct MediaJob {
        std::string library;
        std::string path;
        std::int64_t offset = 0;
        std::int64_t size = 0;
    };

    [[nodiscard]] const SyncedLibrary* library(const std::string& uid) const;
    void missing_media(const std::string& library, const std::string& path);
    /// ``file`` of ``library``, ``size`` bytes, is too large to send: logged,
    /// once.
    void too_large(const std::string& library, const QString& file, std::int64_t size);
    /// The file job to go on with: one begun in pieces, then one read
    /// already, then change files and indexes, then parts. Null for none.
    [[nodiscard]] std::deque<FileJob>* file_queue();
    /// Read the front job of ``queue`` from the store, unless a file is
    /// being read; written once it is.
    void read_file_job(std::deque<FileJob>& queue);
    /// Write the next of the front job of ``queue``, read already: the file
    /// whole, or its next piece.
    void write_file_job(std::deque<FileJob>& queue);
    /// Write the next frame of the front media job; false when there is
    /// none.
    bool write_media_job();

    QSslSocket& socket_;
    PeerShared& shared_;
    const std::vector<SyncedLibrary>& libraries_;
    QElapsedTimer said_;
    bool closed_ = false;
    std::function<bool()> capped_;
    /// Pumps again once the upload speed cap lets media go.
    QTimer wake_;
    std::deque<QByteArray> messages_;
    std::deque<FileJob> file_jobs_;
    std::deque<FileJob> part_jobs_;
    /// A front file job's file is being read (``read_file_job``).
    bool reading_file_ = false;
    std::deque<MediaJob> media_jobs_;
    /// Media files whose pieces' hashes are being worked out, "library/path":
    /// each by the ask, so one dropped (a pause, ``cancel_media``) and asked
    /// again is sent as asked the second time.
    std::map<std::string, std::uint64_t> hashing_;
    std::uint64_t asks_ = 0;
    /// What work off the connection's thread reports back to: gone first,
    /// so nothing reports to an outbox half gone.
    QObject context_;
};

}  // namespace seventhslide::link
