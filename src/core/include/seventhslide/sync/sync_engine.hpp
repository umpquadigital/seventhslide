#pragma once

// Syncs one library with the other computers that have it, through a
// SyncStore.
//
// A sync applies what the others sent, then sends what changed here:
//
//   - **Applying.** Each other computer's files are applied in order, and a
//     file only once every file it was made on top of (its ``seen``) is
//     applied, so a change never arrives before one it builds on. A file is
//     applied in one transaction, with the record of having applied it.
//   - **Sending.** Each record marked in the outbox is compared with its
//     shared copy (``sync_bookkeeping.hpp``); the fields that differ are the
//     change. They go in change files of at most ``k_change_file_bytes``
//     each, or of one record alone, written to the store (and to the disk)
//     before the bookkeeping is committed: if a write fails nothing is lost,
//     and the changes go in the next file. A record too large to sync
//     (``db::k_largest_record``, or a change that would make a file past
//     ``k_largest_store_file``) stays here, marked, as media over the size
//     limit does (below), and is reported (``SyncResult::too_large``). So
//     does any record naming one never shared, or naming one kept so: the
//     others would wait for a record that never comes.
//   - **Catching up** from a snapshot (below) writes the library in one
//     transaction: half a snapshot is never left applied. It is rare (a
//     computer away past what its peers kept).
//
// The window edits the library on a connection of its own, and an edit
// waits for this one's write lock (``db::k_busy_timeout_ms``, then fails).
// So the lock is held only to write, never to read, sign or write files:
// a change file is made from a read (``db::ReadTransaction``, which no edit
// waits for), written to the store, then recorded in a transaction of its
// own, one file at a time; a record edited after it was read stays marked,
// for the next file. A snapshot is read the same way. Catching up is worked
// out from a read and written in one transaction, of what differs only. If
// the window edited meanwhile it is worked out again from a new read, and
// only after a few such edits under the lock. Joining writes
// the whole library in one, before the library can be opened.
//
// Neither holds a whole snapshot: each reads it a part at a time. Joining
// writes the parts in order as it reads them; one gone or damaged undoes
// what it wrote, and the next-newest is tried. Catching up reads the
// parts beside the shared copies, in the one order both are in, and keeps
// only where what differs is (a part, and a record in it) and what goes (by
// name); then it reads again just the parts holding what differs, each
// checked against its hash (one changed since throws, and nothing is kept).
// A record naming one of its own table that comes later (a folder's parent,
// about half of them, keys being random) waits, its document held, until its
// table is done. So memory is about a part, those waiting (folders and
// service items, small), a little for each record that differs, and what is
// unsent here.
//
// A file in the store that cannot be read was damaged here (the gate let in
// only whole files; a disk can still fail): it is set aside, and fetched
// again from a computer that has it whole. One from a newer SeventhSlide is
// kept (``NewerFormat``).
//
// Merging is field by field. An arriving change to a field takes it, unless:
//   - a change to that field with a later version is already applied here (it
//     wins on every computer alike);
//   - this computer changed the field too and has not sent it yet: it is sent
//     with a later version, so it wins everywhere when it arrives.
// A delete goes through only where no other computer changed the record, or
// anything pointing at it or belonging to it, since the deleting one last saw
// them (a delete carries what it saw). Otherwise the edit, or the use, wins:
// the record stays with everything that belongs to it, and is sent whole, so
// it comes back on every computer.
//
// A change that needs a record not here yet (a service item before its
// service, or the song it names) is held, never written in part, and tried
// again after each file and at each sync. A delete made on top of a held
// change drops it.
//
// A song or Freeform document changed differently on two computers keeps
// both: the version that wins keeps the record, and the other
// becomes a copy beside it, "Amazing Grace (from Church PC)". One computer
// makes the copy, as a record it sends: the one whose change here wins over
// an arriving one it had not seen (it copies the arriving version), or the
// one whose sent change loses to such an arriving one (it copies its own).
//
// A list of words (a service's or Freeform document's tags, a song's topics)
// changed on two computers at once keeps both changes: a change carries the
// list it started from, and the words each added are kept and those each
// removed go, in alphabetical order when two changes are combined.
//
// Now and then a computer writes a snapshot of the whole library as it was
// shared (``snapshot.hpp``): once ``snapshot_after`` change files have come
// since the newest one, and at once when there is none while change files
// were let go of (a computer joining then has nothing to start from). What
// is unsent here (a record too large to send, one the size limit keeps here)
// is left out of it, but for a Bible whose verses changed here and are not
// sent: they are read from the library, so it waits. It is read a record at a
// time and written a part at a time, its parts first and its index last, so a
// snapshot in the store is always whole; a part already there is only
// touched. A snapshot that cannot be written (the store, a record merged from
// others too large to go in one) is said (``SyncResult::snapshot_problem``)
// and not tried again until as many files have come once more. A computer
// joining starts from the newest snapshot and replays only the files after
// it. A computer joining a library it had before (its own files are in the
// store) gets back what it sent that the snapshot does not hold, and sends
// nothing until it has.
//
// A computer behind the newest snapshot whose next file is not here (old
// files go once every member has them: one away for long missed them)
// catches up from the snapshot instead (``sync(may_catch_up)``). First it
// sends what it has not sent, so its own changes are merged by the rules
// above like anyone's; then it takes the snapshot's records, each under the
// id it had here, keeping what is still unsent here on top (a file the size
// limit keeps here, and what names it); then it applies every file after the
// snapshot, its own just sent among them. What only the files it missed held
// is in the snapshot; what came after is in the files.
//
// So does a computer whose next file will never come because it refuses it:
// a file from a computer that is no member by the member list's rules now,
// which the editors took in when it was made. An editor holding such a file
// writes a snapshot straight away (``set_refused_by_others``), so no computer
// waits on one it will not take in. The snapshot is the editor's word for what
// the library holds, trusted as anything else it writes; the refused computer
// still writes nothing that counts.
//
// Media files go with the records that own them (``db::k_sync_files``), by
// name, as a name never changes and means the same bytes everywhere. A change
// file declares the files its changes bring, each with its size and
// fingerprint (``ChangeFile::media``), and every declaration that reaches a
// computer is kept (``sync_media``). Moving the bytes is the link's: it
// fetches a change file's media before putting the file in the store, so a
// picture never arrives before its bytes, and checks each against its
// declaration. A record deleted here takes its files from the disk with it.
// Pointing at a file counts as using it, as for any record: a picture deleted
// on one computer while another puts it in a service comes back. What points
// at what is read from the records themselves (``db::names_in``): a uid
// wherever it is, a file's name where a record may name one
// (``db::k_sync_file_names``); a song titled as a file is not using it.
//
// A library is complete when every file its records own is on the disk
// (``missing_media``, each with its declaration, for the link to fetch it by):
// until then it is not ready to use.
//
// The size limit (``set_size_limit``, from the member list) takes the files
// the library's records own in the order they were first declared (the
// version of the change that did), each as long as it fits: the same files on
// every computer. One that does not fit is not fetched (``missing_media``
// leaves it out), and one made here is not sent: its record, and any record
// naming it, stay here, marked, until there is room (``media_use``). A
// theme's background goes with its output, which is never held back: it is
// declared, and fetched by the others if it fits.
//
// Every file the engine writes is signed with this computer's key
// (``envelope.hpp``), and every file it reads has its signature checked.
// Whether the signer is a member who may write such a file is checked once,
// by the ``Gate``, before the link puts the file in the store.
//
// Seam: Qt Core (QCryptographicHash, for fingerprints) and Qt SQL via
// Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/sync/change_file.hpp"
#include "seventhslide/sync/change_notes.hpp"
#include "seventhslide/sync/changed.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/media_pieces.hpp"
#include "seventhslide/sync/snapshot.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::sync {

/// Change files that come before a snapshot is due: replaying this many is
/// quick, and a snapshot is a whole library to read through (its parts that
/// did not change are not sent again).
inline constexpr std::int64_t k_snapshot_after = 200;

/// The most a change file of several records holds, as written: what changed
/// at once (a library put online, a Bible imported with others) goes in as
/// many files as it takes, each small enough to pass on and to apply without
/// much memory. A record that would take a file past this starts the next,
/// and one larger than this (a Bible) has a file of its own. One larger than
/// ``db::k_largest_record`` is not sent.
inline constexpr std::size_t k_change_file_bytes = 4 * 1024 * 1024;

/// A record marked here too large to send (``db::k_largest_record``).
struct TooLarge {
    std::string table;
    std::string key;
    /// What the record is called, from its document; empty for none.
    std::string title;
    /// Its document's bytes, as JSON; for one whose change would make a file
    /// past ``k_largest_store_file``, the change's.
    std::int64_t bytes = 0;

    friend bool operator==(const TooLarge&, const TooLarge&) = default;
};

/// The size limit's account of a library's media.
struct MediaUse {
    /// The limit, in bytes; 0 for none.
    std::int64_t limit = 0;
    /// What the files that fit use: each file records here own, in the order
    /// they were first declared, as long as it fits.
    std::int64_t used = 0;
    /// A file records here own that does not fit.
    struct Over {
        std::string path;
        std::int64_t size = 0;
        /// What the library calls it ("Easter video.mp4").
        std::string title;
        /// Made here and not sent: it stays on this computer.
        bool here = false;
    };
    std::vector<Over> over;
};

struct SyncResult {
    /// Change files applied from other computers.
    int files_applied = 0;
    /// Files that wait for one they were made on top of, not here yet.
    int files_waiting = 0;
    /// The computers whose change files those wait for: the next file each
    /// needs from them is not in the store. While it is on its way, for a
    /// moment; for good when this computer does not let it in (its writer
    /// is no member by the member list's rules now), until an editor that
    /// applied it writes a snapshot holding it (``set_refused_by_others``).
    std::set<std::string> waiting_on;
    /// Changes in the file this sync sent; 0 when there was nothing to send.
    int changes_sent = 0;
    /// "table key" of each change from another computer still waiting, after
    /// this sync, for what it needs: a record it cannot be without (its
    /// service, its folder), or one it points at (its song).
    std::vector<std::string> held;
    /// Whether this sync wrote a snapshot.
    bool snapshot_written = false;
    /// Media files records here own that are not on the disk, after this sync
    /// (``missing_media``).
    std::vector<db::MediaDigest> media_missing;
    /// The records changes from other computers were applied to (at least
    /// those): what a window showing this library reads again.
    Changed changed;
    /// The media against the size limit, after this sync.
    MediaUse media;
    /// Whether this sync caught up from a snapshot (above).
    bool caught_up = false;
    /// What each change it sent did, for the shared log (``change_notes``).
    std::vector<SentRecord> sent;
    /// The records this sync did not send as too large. Each stays here,
    /// marked, and snapshots leave it out, as the others had it last, until
    /// it is smaller or gone. Found too while it sends nothing, waiting to
    /// have back what it sent before it joined again.
    std::vector<TooLarge> too_large;
    /// The records kept here, marked, with one of those: each names one never
    /// shared, or one kept so. Table and key.
    std::vector<std::pair<std::string, std::string>> kept_with_too_large;
    /// Why no snapshot is written while one is due, in words: the store could
    /// not write it, a record is too large to go in one, or a Bible too large
    /// to send changed here. Empty when none is due or it was written. The
    /// sync's other work is done all the same.
    std::string snapshot_problem;
};

class SyncEngine {
public:
    /// ``media`` is the library's media folder (``library::media_root``);
    /// ``me`` this computer: its id, its name for people, and the key that
    /// signs what it writes.
    SyncEngine(db::Database& db, SyncStore& store, std::filesystem::path media, Identity me);
    /// Where the pieces' hashes of the files declared here are kept, for
    /// the link to send them by (``PieceHashes``); none by default.
    void set_piece_hashes(std::filesystem::path folder) { pieces_ = PieceHashes(std::move(folder)); }

    /// Mark every record here to be sent: a library put online for the first
    /// time.
    void share_everything();

    /// Make this library, new and empty, the other computers' library
    /// ``library_uid``, from what the store here holds of it: it takes that
    /// uid and the newest snapshot's records, and the next sync brings the
    /// rest. Throws ``std::logic_error`` when the library holds
    /// anything already, and ``std::runtime_error`` (the library left as it
    /// was) when a snapshot belongs to another library, came from a newer
    /// SeventhSlide or holds a record this library cannot take, or when none
    /// is whole and the change files one holds are not all here.
    void join(std::string_view library_uid);

    /// Apply what the other computers sent, then send what changed here, then
    /// write a snapshot when one is due. First, with ``may_catch_up``, catch
    /// up from the newest snapshot when this computer is behind it and a file
    /// it needs is not here (above): the link has nothing more on its way.
    /// Throws ``std::runtime_error`` (the database left as it was by the step
    /// that failed) when the store or a file cannot be read or written, a file
    /// belongs to another library, or came from a newer SeventhSlide.
    SyncResult sync(bool may_catch_up = true);

    /// Write a snapshot of this library now. False, writing nothing, when
    /// what this computer sent before joining again is not back yet, a
    /// Bible's verses changed here are not sent yet, or a snapshot in the
    /// store holds as many change files already. Throws
    /// ``std::runtime_error`` when the store cannot write it (its parts
    /// written so far go once old, ``housekeeping.hpp``), or a record is too
    /// large to go in it (named).
    bool snapshot();

    /// The media files records here own that are not on the disk, each as
    /// declared by the change that brought it, for the link to fetch and
    /// check it by: none when the library is complete. One lost on the
    /// computer that registered it before it was ever sent has no
    /// declaration (an empty ``root``); no computer can supply it. ``use`` is
    /// the account of the media (``media_use``).
    [[nodiscard]] std::vector<db::MediaDigest> missing_media(const MediaUse& use);

    /// The library's media against the size limit (above).
    [[nodiscard]] MediaUse media_use();
    /// The bytes of media the library may hold (``Members::size_limit``); 0
    /// for no limit.
    void set_size_limit(std::int64_t bytes) { size_limit_ = bytes; }

    /// This computer has never been an editor of the library: it takes in
    /// what they write and writes nothing of its own, neither change file nor
    /// snapshot, as no other computer would take them from it. What the merge
    /// rules kept here, the computers that kept it send.
    void set_holds_only(bool holds_only) { holds_only_ = holds_only; }
    /// Only an editor writes snapshots: no computer takes one from a viewer. A
    /// viewer that was an editor still writes change files, so edits it made
    /// are not lost: they count if it is made an editor again.
    void set_writes_snapshots(bool writes) { writes_snapshots_ = writes; }
    /// Change files that come before a snapshot is due (``k_snapshot_after``);
    /// 0 for never, not even when there is none.
    void set_snapshot_after(std::int64_t files) { snapshot_after_ = files; }
    /// Change files applied here that another computer would not let in now,
    /// by computer, up to the last of them: their writer is no member by the
    /// member list's rules now (a join that counted when it was made, and
    /// does not under a later SeventhSlide's), so the others refuse them, and
    /// every file made on top of them would wait there for good. A snapshot
    /// holding them is written as soon as the newest does not (an editor's
    /// only): the others catch up from it, taking them on this computer's
    /// word, as they take anything else it writes.
    void set_refused_by_others(std::map<std::string, std::int64_t> files)
    {
        refused_by_others_ = std::move(files);
    }
    /// Write that snapshot now, if it is due (``sync`` does, at its end):
    /// whether it wrote one.
    bool snapshot_refused();
    /// What this computer is called in the files it writes from now on.
    void set_computer_name(std::string name) { computer_ = std::move(name); }
    /// The size the snapshots it writes are cut into parts at
    /// (``k_snapshot_part_bytes``): smaller in tests, so a small library is
    /// in several parts.
    void set_part_bytes(std::int64_t bytes) { part_bytes_ = bytes; }

private:
    /// The file a change came in.
    struct Origin {
        db::SyncVersion version;
        std::int64_t sequence = 0;
        /// The file's ``seen``; empty for a held change.
        std::map<std::string, std::int64_t> seen;
        /// The uids of the records the file deletes that stay here: a
        /// reference to one the file sets to null keeps it.
        std::set<std::string> kept;
        /// The name of the computer that sent it, for people.
        std::string computer;

        /// Whether the file was made on top of ``machine``'s file ``sequence``.
        [[nodiscard]] bool saw(const std::string& machine, std::int64_t number) const;
    };

    using RecordName = std::pair<std::string, std::string>;

    void apply_incoming(SyncResult& result);
    /// The records ``file`` deletes that stay: each one something here points
    /// at, or that belongs to it, which the deleting computer had not seen
    /// (changed, new, or changed here and not sent), and with any of them
    /// what it belongs to and what belongs to it.
    [[nodiscard]] std::set<RecordName> deletes_kept(const ChangeFile& file);
    /// Keep a record another computer deleted: the shared copy takes the
    /// delete, and the record is sent whole, to come back everywhere.
    void keep(const Change& change, const Origin& origin);
    /// A version of a record that another replaced, kept as a copy beside it.
    struct Copy {
        nlohmann::json document;
        /// The version of the change that made it.
        db::SyncVersion of;
        /// The computer it came from, for its name.
        std::string computer;
    };
    /// Add ``copy`` of ``table``'s record ``key``. Its uid comes from the
    /// record and the version, so the same version copied twice (by a
    /// computer that joins again, replaying what it did) is one copy.
    void add_copy(const std::string& table, const std::string& key, Copy copy);
    void apply_file(const ChangeFile& file);
    /// Apply one change. False when it needs a record that is not here: then
    /// nothing was written, and the change is the caller's to hold.
    bool apply_change(const Change& change, const Origin& origin);
    /// Try the held changes again until none more can be applied: every one,
    /// or with ``arrived`` only those naming a record in it (``db::names_in``),
    /// as a held change waits for a record that is not here. What comes
    /// meanwhile joins ``arrived``.
    void retry_held(std::optional<std::set<std::string>> arrived = std::nullopt);
    void send_changes(SyncResult& result);
    /// Every record marked, parents first: the mark for all outputs is each
    /// one here and each one shared (one deleted here is no longer listed).
    [[nodiscard]] std::vector<RecordName> marked_now();
    /// Every media file a record here owns, as paths under the media folder,
    /// sorted.
    [[nodiscard]] std::vector<std::string> files_owned();
    /// The media files records here own, and every file's declaration, read
    /// once for what asks about them all.
    struct MediaHere {
        std::vector<std::string> owned;
        std::map<std::string, db::MediaDigest> declared;
    };
    [[nodiscard]] MediaHere media_here();
    [[nodiscard]] MediaUse media_use(const MediaHere& here);
    [[nodiscard]] std::vector<db::MediaDigest> missing_media(const MediaUse& use, const MediaHere& here);
    /// The records marked here that the size limit keeps here: one owning a
    /// file made here that does not fit, or naming one (a folder's entry, a
    /// service item). Never an output.
    [[nodiscard]] std::set<RecordName> held_back(const MediaUse& use);
    /// Of ``records``, those of ``records[from]``'s table from there on with
    /// something to send that name a record in ``unsent`` (by key: kept here
    /// and never shared, so the others would wait for it), or a record of
    /// theirs that does. Each of them never shared is added to it, and so is
    /// each too large to send that was never shared. Read before any of them
    /// is sent, for a table whose records name each other. ``measured``
    /// gains the size of each document it dumps, as read now.
    [[nodiscard]] std::set<RecordName> waiting_ahead(const std::vector<RecordName>& records,
                                                     std::size_t from, const std::set<RecordName>& held,
                                                     std::set<std::string>& unsent,
                                                     std::map<RecordName, std::size_t>& measured);
    /// What the library calls media file ``path``.
    [[nodiscard]] std::string title_of(const std::string& path);
    /// The files ``table``'s record ``key``, holding ``current``, owns that
    /// its shared copy did not: what a change to it brings.
    [[nodiscard]] std::vector<std::string> new_files(const std::string& table,
                                                     const std::string& key,
                                                     const nlohmann::json& current);
    /// The digest of each file the marked records bring that is on the disk,
    /// read from it, by path.
    [[nodiscard]] std::map<std::string, db::MediaDigest> read_new_digests(
        const std::set<RecordName>& held);
    /// Remove from the disk each file a record here no longer owns, of those
    /// ``apply_change`` let go: once what let go of it is committed.
    void remove_unowned();
    /// The records deleted here and not yet sent, which an arriving change may
    /// use (``bring_back_used``).
    void find_deleted_here();
    /// Bring back each record deleted here and not yet sent that ``change``'s
    /// record names (``db::names_in``), with those deleted with it that are
    /// tied to it: using counts as editing, which wins over a delete. A Bible
    /// cannot be rebuilt here (its shared copy keeps only a fingerprint of its
    /// verses): the computer that used it keeps it and sends it whole.
    void bring_back_used(const Change& change);
    /// Write snapshot ``name``'s records, versions, held changes and media
    /// into this library, new and empty, a part at a time. Throws
    /// ``std::runtime_error`` for a record this library cannot take, and,
    /// once the index is set aside, for a part gone or not as the index
    /// names it. What it wrote is the caller's to undo.
    void restore(const SnapshotName& name, const SnapshotIndex& index);
    /// The snapshot in the store holding the most change files.
    [[nodiscard]] std::optional<SnapshotName> newest_snapshot();
    /// Whether a snapshot is due (above), given the newest in the store.
    [[nodiscard]] bool snapshot_due(const std::optional<SnapshotName>& newest);
    /// ``machine``'s change file ``n``, read whole and checked to be where it
    /// belongs. One that cannot be read was damaged here: it is set aside, to
    /// be fetched again, and nullopt. Throws ``NewerFormat`` for a newer
    /// SeventhSlide's, and ``std::runtime_error`` for one of another library.
    [[nodiscard]] std::optional<ChangeFile> read_change_file(const std::string& machine, std::int64_t n);
    /// Likewise the index of snapshot ``name``.
    [[nodiscard]] std::optional<SnapshotIndex> read_snapshot_index(const SnapshotName& name);
    /// Whether every change file ``index`` holds is here: what joining
    /// without it replays.
    [[nodiscard]] bool holds_files(const SnapshotIndex& index);
    /// The label of snapshot ``name``'s index, as ``read_snapshot_index``.
    [[nodiscard]] std::optional<SnapshotIndex> read_snapshot_label(const SnapshotName& name);
    /// The newest snapshot, when this computer is behind it for some
    /// computer's files and the next of them is not here, and it can read it.
    [[nodiscard]] std::optional<SnapshotName> behind();
    /// The label of snapshot ``name`` (the newest), read once; null when it
    /// cannot be read (set aside, to be fetched again).
    [[nodiscard]] const SnapshotIndex* newest_label(const SnapshotName& name);
    /// Whether the newest snapshot holds each of ``files``' change files.
    [[nodiscard]] bool newest_snapshot_holds(const std::map<std::string, std::int64_t>& files);
    /// Take the snapshot ``name`` as described above; the files it lets go
    /// of are left in ``unowned_`` until the files after it are applied.
    /// False, doing nothing, when it turned out damaged (set aside).
    bool catch_up(const SnapshotName& name, SyncResult& result);
    /// ``snapshot()``, given how many change files the newest one holds.
    /// ``waits`` says why when a Bible too large to send keeps it from being
    /// written.
    bool write_snapshot(std::int64_t newest, std::string& waits);
    /// ``table``'s record ``key`` as it was shared, ``shared`` its shared
    /// copy's document: with its parts (a Bible's verses) read here, if they
    /// are still the ones shared. Nullopt when they are not, or it is not here.
    [[nodiscard]] std::optional<nlohmann::json> as_shared(const std::string& table, const std::string& key,
                                                         const nlohmann::json& shared);
    /// The change files in the store, all computers' together, as this sync
    /// found them.
    [[nodiscard]] std::int64_t files_in_store() const;

    db::Database& db_;
    db::SyncBookkeeping book_;
    SyncStore& store_;
    std::filesystem::path media_;
    PieceHashes pieces_{{}};
    crypto::SigningKey key_;
    std::string machine_;
    std::string computer_;
    std::int64_t snapshot_after_ = k_snapshot_after;
    std::int64_t part_bytes_ = k_snapshot_part_bytes;
    bool holds_only_ = false;
    bool writes_snapshots_ = true;
    std::int64_t size_limit_ = 0;
    std::map<std::string, std::int64_t> refused_by_others_;
    /// The newest snapshot's label, read once for its name (``behind``).
    std::optional<SnapshotIndex> newest_label_;
    /// The last change file in the store from each computer, as this sync
    /// found them and sent one: how many there are, to know when a snapshot
    /// is due.
    std::map<std::string, std::int64_t> last_in_store_;
    /// How many change files were in the store when a snapshot last failed
    /// to be written: none is due again until ``snapshot_after_`` more have
    /// come, as it would most likely fail the same way.
    std::optional<std::int64_t> snapshot_failed_at_;
    /// Why the last snapshot due was not written (``SyncResult``).
    std::string snapshot_problem_;
    /// The keys of the records added here since the file being applied began
    /// (``retry_held``).
    std::set<std::string> arrived_;
    /// Files records let go of in the transaction under way (``remove_unowned``).
    std::vector<std::string> unowned_;
    /// Records changes from others went into that the files applied do not
    /// name: held changes ``retry_held`` let through, and the copies a
    /// conflict keeps (``add_copy``). The next sync reports them.
    Changed changed_;
    /// A record deleted here and not yet sent, as the others last had it:
    /// its document, and the id it had here.
    struct DeletedHere {
        std::string table;
        std::string key;
        nlohmann::json document;
        std::optional<std::int64_t> id_here;
    };
    std::vector<DeletedHere> deleted_here_;

    /// The records marked here (not a table marked as a whole), whether each
    /// is here, whether its shared copy holds it, and what each names, kept
    /// through the files one sync applies: read once, then again only where
    /// what was applied since may have changed them. A file reads again
    /// records it may have changed; of the rest of what is marked, a delete
    /// lists the marks of a few tables.
    ///   - A record a change wrote, or marked, is read again. Only such a
    ///     change writes a shared copy.
    ///   - When one went, a foreign key may have deleted, or set to null, the
    ///     records pointing at it, and what named it by an id in JSON names
    ///     nothing there now: the tables pointing at its table list their
    ///     marks again (one query each), and the marks new among them, or
    ///     naming it, are read again, and so on for each of those that went.
    ///   - When one came back under an id it had, what still holds that id in
    ///     JSON names it again: every mark of those tables is read again. A
    ///     record new here takes an id never used (each table a record may
    ///     point at is ``AUTOINCREMENT``), so nothing named it before.
    ///   - All is read again after the window wrote.
    class MarkedHere {
    public:
        MarkedHere(db::Database& db, db::SyncBookkeeping& book) : db_(db), book_(book) {}

        /// Read everything again when next asked.
        void forget();
        /// ``table``'s record ``key`` was written, or marked.
        void changed(std::string_view table, std::string_view key);
        /// ``table``'s record ``key`` was deleted, or may have been.
        void went(std::string_view table, std::string_view key);
        /// ``table``'s record ``key`` was written under an id it had before.
        void came_back(std::string_view table, std::string_view key);
        /// Read again what may have changed since last asked. Run in the
        /// transaction the answers are used in.
        void refresh();

        /// Each marked record, by table and key.
        [[nodiscard]] auto records() const { return std::views::keys(marks_); }
        [[nodiscard]] bool marked(const RecordName& record) const { return marks_.contains(record); }
        /// Whether marked ``record`` is here.
        [[nodiscard]] bool here(const RecordName& record);
        /// Whether marked ``record``'s shared copy holds it, not a delete.
        [[nodiscard]] bool shared_live(const RecordName& record);
        /// The marked records whose record here names ``name``
        /// (``db::names_in``), leaving out the record's own key.
        [[nodiscard]] std::vector<RecordName> naming(const std::string& name);

    private:
        struct Mark {
            std::optional<bool> here;
            std::optional<bool> shared_live;
            /// What it names, once read; nothing when it is not here.
            std::optional<std::vector<std::string>> names;
        };
        /// The tables whose records point at one of ``tables``'s, and those
        /// pointing at those: a foreign key's action goes on from table to table.
        [[nodiscard]] static std::set<std::string> pointing_at(const std::set<std::string>& tables);
        /// Read ``table``'s marks again; every table's when empty.
        void read_marks(std::string_view table);
        /// Add ``table``'s marks not here yet, each also to ``added``. A mark
        /// goes only when sent, or by the window's write.
        void list_marks(std::string_view table, std::vector<RecordName>& added);
        /// Read ``record``'s mark again, if it is marked.
        void read_again(RecordName record);
        void add(RecordName record);
        void drop(std::map<RecordName, Mark>::iterator it);
        /// Read what every mark names, once.
        void read_all_names();
        /// Read what the mark at ``it`` names.
        void read_names(std::map<RecordName, Mark>::iterator it);

        db::Database& db_;
        db::SyncBookkeeping& book_;
        bool read_ = false;
        /// Whether every mark's names are read (the first ``naming``).
        bool names_read_ = false;
        std::int64_t data_version_ = 0;
        std::map<RecordName, Mark> marks_;
        std::map<std::string, std::set<RecordName>> named_;
        std::set<RecordName> changed_;
        std::set<RecordName> went_;
        std::set<std::string> came_back_;
    };
    MarkedHere marked_{db_, book_};
};

/// ``document`` with each of its parts (a Bible's verses) as a fingerprint:
/// the form a shared copy keeps, and compares by.
[[nodiscard]] nlohmann::json shared_form(std::string_view table, const nlohmann::json& document);

}  // namespace seventhslide::sync
