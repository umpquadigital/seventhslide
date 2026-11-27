#pragma once

// What a computer checks before putting another computer's file in its store:
// the signature is the writer's key, and the writer is a member. For a change
// file, the writer may also have written it: an editor always; a viewer or a
// removed computer up to the files the list keeps for it, or any later one
// another computer's file was made on top of (``Vouches``). That last makes
// every computer agree: one that took a file before hearing its writer was
// removed, and built on it, makes it count everywhere, so no computer waits
// for a file the others refused. A snapshot only an editor writes. So a
// removed laptop or a viewer cannot slip changes in through a church computer
// on the local network, and a viewer that holds the library for the others and
// faces the internet cannot write its contents even with its key in the wrong
// hands: what it passes on, the computers that wrote it signed.
//
// A file is read whole, so one the engine could not apply is never let in;
// but of a file from a newer SeventhSlide only its label is read
// (``change_file_label``, ``snapshot_index_label``), and it is let in by that,
// to be kept and passed on: the engine says this computer is too old to apply
// it, and the computers that can read it whole did when they took it.
//
// A snapshot's index is signed; its parts are not. A part is let in when it
// is what an index let in names, by its hash (``snapshot_part``), and is read
// whole too, but for one of a newer format, which its hash alone lets in.
//
// A member list entry is let in when it is genuine: signed by the computer
// it is from (a joining one by the key the entry gives). Whether it counts
// is the list's to resolve (``read_members``), the same on every computer;
// the gate does not judge it by the list as this computer has it now.
//
// A shared log entry (``shared_log.hpp``) is let in when its writer is an
// editor, not removed, and signed it: a viewer says nothing there, as it
// writes nothing else.
//
// The link asks the gate for every file it is offered, member list entries
// first (they may change who the others are), then locked admin keys,
// change files, snapshots' indexes and their parts. ``NotYet`` says a file
// may be let in later: once this computer knows its writer, or a file built
// on it.
//
// Reading a large file through is the cost, and it needs only the list: a
// gate on a copy of it may check files on another thread
// (``read_change_file``, ``snapshot``), with no ``Vouches``. Whether a change
// file counts on another's word (``admit_change_file``) reads the store, and
// is asked where the ``Vouches`` live.
//
// Seam: the sync engine's file formats and the member list.

#include "seventhslide/sync/change_file.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/snapshot.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::sync {

struct Envelope;

/// A file refused for now: it may be let in once more is known here.
struct NotYet : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// How far other computers built on each computer's change files: for each
/// computer, the highest of its change files that a change file of another
/// computer, or a snapshot by another, in the store was made on top of.
/// Reading is ``refresh``'s alone, each file's label once, and may take a
/// while (a store holds many files): it runs off the window's thread, so a
/// gate asking ``up_to`` on any thread reads nothing.
class Vouches {
public:
    explicit Vouches(SyncStore& store) : store_(store) {}

    /// Read the files stored since the last refresh. On any thread, one at a
    /// time; it does not throw.
    void refresh();
    /// Read the one change file, or snapshot, just stored: what ``refresh``
    /// would find, without listing the whole store. Like ``refresh``, safe on
    /// any thread and never throws.
    void stored(std::string_view machine, std::int64_t sequence);
    void stored(const SnapshotName& name);

    /// As of the last refresh; 0 when none was.
    [[nodiscard]] std::int64_t up_to(std::string_view machine) const;

private:
    using Found = std::map<std::string, std::int64_t>;
    /// What the files not read yet vouch for, into ``found``.
    void read_new(Found& found);
    /// What change file ``sequence`` of ``machine``, or snapshot ``name``,
    /// vouches for, into ``found``, unless it was read already.
    void read_change(const std::string& machine, std::int64_t sequence, Found& found);
    void read_snapshot(const SnapshotName& name, Found& found);
    /// ``found`` into what ``up_to`` answers.
    void take(const Found& found);

    SyncStore& store_;
    /// One refresh at a time; it alone touches ``read_`` and
    /// ``read_snapshots_``.
    std::mutex refreshing_;
    std::set<std::pair<std::string, std::int64_t>> read_;
    std::set<std::pair<std::string, std::int64_t>> read_snapshots_;
    mutable std::mutex mu_;
    std::map<std::string, std::int64_t> up_to_;
};

class Gate {
public:
    /// ``vouches`` may be null: no file is then let in on another's word.
    explicit Gate(Members members, Vouches* vouches = nullptr)
        : members_(std::move(members)), vouches_(vouches)
    {
    }

    [[nodiscard]] const Members& members() const noexcept { return members_; }

    /// Whether ``machine``'s change file ``sequence`` would count: by the
    /// list, or on another computer's word.
    [[nodiscard]] bool may_count(std::string_view machine, std::int64_t sequence) const;

    /// ``machine``'s change file ``sequence``, read and checked (of a newer
    /// SeventhSlide's, its label). Throws ``NotYet``, or
    /// ``std::runtime_error`` saying why it is refused. ``read_change_file``
    /// then ``admit_change_file``.
    [[nodiscard]] ChangeFile change_file(std::string_view machine, std::int64_t sequence,
                                         std::string_view text) const;
    /// Every check of ``change_file`` but whether the file counts yet: the
    /// signature, the writer, the library and the name. It reads nothing but
    /// ``text`` and this gate's copy of the member list, so it may run on
    /// another thread (a large file takes a while to parse and verify).
    [[nodiscard]] ChangeFile read_change_file(std::string_view machine, std::int64_t sequence,
                                              std::string_view text) const;
    /// Throws ``NotYet`` unless ``machine``'s change file ``sequence`` counts
    /// (``may_count``). Reads the vouches: the thread that owns them only.
    void admit_change_file(std::string_view machine, std::int64_t sequence) const;
    /// Likewise a snapshot's index. It reads only ``text`` and the member
    /// list, so it may run on another thread.
    [[nodiscard]] SnapshotIndex snapshot(const SnapshotName& name, std::string_view text) const;
    /// The part ``sha256`` of a snapshot let in, from ``text``: refused, with
    /// ``std::runtime_error``, unless it is that part and, of this format, a
    /// part whole. On any thread.
    static void snapshot_part(std::string_view sha256, std::string_view text);
    /// A member list entry, checked to be genuine.
    void member_entry(std::string_view machine, std::int64_t sequence, std::string_view text) const;
    /// ``machine``'s shared log entry ``sequence``, checked. Throws
    /// ``NotYet`` while its writer is not known here, or
    /// ``std::runtime_error`` saying why it is refused.
    void log_entry(std::string_view machine, std::int64_t sequence, std::string_view text) const;
    /// A locked admin key is let in when it is the admin key's own.
    void admin_key(std::string_view fingerprint, std::string_view text) const;

private:
    /// Refuse ``what`` unless ``machine`` is a member and signed it.
    void check_writer(const Envelope& envelope, std::string_view machine,
                      std::string_view what) const;

    Members members_;
    Vouches* vouches_;
};

}  // namespace seventhslide::sync
