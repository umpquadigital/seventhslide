#pragma once

// Where a synced library's files are kept on this computer: the files the
// computers trade, as the sync engine and the member list see them.
// ``FolderStore`` keeps them in the library's sync folder
// (``library::sync_root``); the link copies between computers' stores whatever
// one has and another lacks, after checking it (``Gate``).
//
// Three kinds of file are numbered: each computer's change files, 1, 2, 3, ...
// in a place of their own (``changes/<machine id>/``), its member list entries
// likewise (``members/<machine id>/``), and its entries in the library's
// shared log, the messages and the notes of what it changed
// (``log/<machine id>/``, ``shared_log.hpp``); a computer writes only its own.
// Its snapshots' indexes are in another (``snapshot_index/<machine id>/``),
// each numbered by how many change files it holds, all computers' together, so
// the one that leaves least to replay is known by its name. A snapshot's parts
// (``snapshot.hpp``) are kept by the SHA-256 of their bytes
// (``snapshot_parts/<sha256>.json``), so one part serves every snapshot that
// holds it; a part is touched each time it is wanted again, and housekeeping
// lets go of one no snapshot names that no one has wanted for a while. An
// index is stored only once its parts are, so a snapshot here is whole. The
// library's admin key, locked under the password, is kept by the key's
// fingerprint (``admin_keys/<fingerprint>.json``), one file for each admin key
// the library has had. A file once written is never changed or replaced, so
// two stores never disagree about one: the link only unions them.
//
// Old files go (``housekeeping.hpp``): a computer's change files up to a
// number, once every member has applied them and a snapshot holds them,
// snapshots older than the newest, and parts no snapshot names. A store
// remembers how far each computer's change files went (``dropped_through``),
// takes none of them again, and the link fetches none of them.
//
// Media is not in the store: the library's media folder is where a media file
// lives, and a change file names the files it brings (``ChangeFile::media``).
//
// Seam: plain C++20 (std::filesystem).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::sync {

/// A snapshot in the store: the computer that wrote it, and how many change
/// files it holds.
struct SnapshotName {
    std::string machine;
    std::int64_t files = 0;

    friend bool operator==(const SnapshotName&, const SnapshotName&) = default;
};

/// A snapshot's part in the store, named by the SHA-256 of its bytes.
struct PartFile {
    std::string sha256;
    std::int64_t size = 0;
    /// When it was written, or last kept (``SyncStore::keep_part``).
    std::filesystem::file_time_type touched;
};

/// The numbered files: a computer's change files, its member list entries, and
/// its entries in the shared log.
enum class Numbered { Changes, Members, Log };

/// A file a newer SeventhSlide made, which this one cannot read whole: kept,
/// and applied once this computer is updated. Any other file that cannot be
/// read was damaged here (the gate let in only whole files), and is set aside
/// to be fetched again. Its message is the one the operator reads, wherever
/// the file is found newer (its envelope, its format, its schema).
struct NewerFormat : std::runtime_error {
    NewerFormat()
        : std::runtime_error("This library was changed by a newer version of SeventhSlide. "
                             "Update SeventhSlide on this computer to keep it in sync.")
    {
    }
};

/// A file written to a store was there already: another write put it there
/// first. A file once written is never replaced.
struct AlreadyThere : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Whether ``machine`` may name a computer's place in a store: 1 to 64 of
/// ``a-z``, ``0-9``, ``-`` and ``_`` (a real one is 32 hex digits). A machine
/// id comes from other computers, and names a folder.
[[nodiscard]] bool is_machine_id(std::string_view machine);
/// Whether ``fingerprint`` is a key's fingerprint: 64 lower-case hex digits.
[[nodiscard]] bool is_fingerprint(std::string_view fingerprint);
/// Whether ``name`` may name a snapshot's part: 64 lower-case hex digits, a
/// SHA-256.
[[nodiscard]] bool is_part_name(std::string_view name);
/// Whether ``uid`` is a library's uid (``db::library_uid``): 32 lower-case
/// hex digits. One another computer names may name a folder (a server's).
[[nodiscard]] bool is_library_uid(std::string_view uid);

/// The largest file that goes between computers: a change file, a snapshot's
/// index or one of its parts. One record of ``db::k_largest_record``, with
/// what comes with it, fits with room to spare; one larger is not sent, nor
/// taken.
inline constexpr std::int64_t k_largest_store_file = 16LL * 1024 * 1024;
/// The size a snapshot's part is cut at, on average (``PartWriter``).
inline constexpr std::int64_t k_snapshot_part_bytes = 4LL * 1024 * 1024;
/// The largest part of several records. A record of ``k_snapshot_part_bytes``
/// or more is a part of its own.
inline constexpr std::int64_t k_snapshot_part_most = 8LL * 1024 * 1024;

class SyncStore {
public:
    SyncStore() = default;
    SyncStore(const SyncStore&) = delete;
    SyncStore& operator=(const SyncStore&) = delete;
    SyncStore(SyncStore&&) = delete;
    SyncStore& operator=(SyncStore&&) = delete;
    virtual ~SyncStore() = default;

    /// Every computer a file of ``kind`` is here from.
    [[nodiscard]] virtual std::vector<std::string> machines(Numbered kind) = 0;
    /// The numbers of ``machine``'s files of ``kind`` here, lowest first.
    [[nodiscard]] virtual std::vector<std::int64_t> sequences(Numbered kind,
                                                               std::string_view machine) = 0;
    /// Throws ``std::runtime_error`` when the file cannot be read.
    [[nodiscard]] virtual std::string read(Numbered kind, std::string_view machine,
                                           std::int64_t sequence) = 0;
    /// Write ``machine``'s file ``sequence`` whole: no one reads part of it.
    /// Throws ``AlreadyThere`` when the file is there already, and
    /// ``std::runtime_error`` when it cannot be written or is a change file
    /// let go of here.
    virtual void write(Numbered kind, std::string_view machine, std::int64_t sequence,
                       std::string_view contents) = 0;

    /// Move ``machine``'s file ``sequence`` of ``kind`` out of the store,
    /// damaged (``NewerFormat``): it is fetched again from a computer that
    /// has it whole.
    virtual void set_aside(Numbered kind, std::string_view machine, std::int64_t sequence) = 0;
    /// Likewise a snapshot.
    virtual void set_aside_snapshot(const SnapshotName& name) = 0;

    /// Let go of ``machine``'s change files numbered up to ``sequence``, for
    /// good. Throws ``std::runtime_error`` when the mark of it cannot be
    /// written (then none goes).
    virtual void drop_changes_through(std::string_view machine, std::int64_t sequence) = 0;
    /// How far ``machine``'s change files were let go of here; 0 for none.
    [[nodiscard]] virtual std::int64_t dropped_through(std::string_view machine) = 0;

    /// Every snapshot here, by computer and number.
    [[nodiscard]] virtual std::vector<SnapshotName> snapshots() = 0;
    /// A snapshot's index. Throws ``std::runtime_error`` when it cannot be
    /// read.
    [[nodiscard]] virtual std::string read_snapshot(const SnapshotName& name) = 0;
    /// Write a snapshot's index whole, once its parts are here. Throws
    /// ``AlreadyThere`` when it is there already, and ``std::runtime_error``
    /// when it cannot be written.
    virtual void write_snapshot(const SnapshotName& name, std::string_view contents) = 0;
    /// Let go of a snapshot's index: a newer one holds what it did. Whether
    /// it is gone.
    virtual bool remove_snapshot(const SnapshotName& name) = 0;

    /// Every snapshot part here, by name.
    [[nodiscard]] virtual std::vector<PartFile> parts() = 0;
    /// Whether the part ``sha256`` is here. One that is is touched, so it
    /// stays a while longer: a snapshot is about to name it.
    [[nodiscard]] virtual bool keep_part(std::string_view sha256) = 0;
    /// Throws ``std::runtime_error`` when the part cannot be read.
    [[nodiscard]] virtual std::string read_part(std::string_view sha256) = 0;
    /// Write the part ``sha256`` whole; one there already is only touched,
    /// unless it is not as named (damaged here), when it is set aside and
    /// written again. Throws ``std::invalid_argument`` when ``contents`` are
    /// not what ``sha256`` names, and ``std::runtime_error`` when it cannot be
    /// written.
    virtual void write_part(std::string_view sha256, std::string_view contents) = 0;
    /// ``write_part`` for ``contents`` just checked to be what ``sha256``
    /// names (by the gate, or the writer that hashed them), so not hashed
    /// again.
    virtual void write_checked_part(std::string_view sha256, std::string_view contents)
    {
        write_part(sha256, contents);
    }
    /// Let go of a part no snapshot here names, unless it was touched at or
    /// after ``touched_before``, as seen when it goes: a snapshot arriving may
    /// have just wanted it (``keep_part``). Whether it went.
    virtual bool remove_part(std::string_view sha256, std::filesystem::file_time_type touched_before) = 0;
    /// Move a part out of the store, damaged, as ``set_aside``.
    virtual void set_aside_part(std::string_view sha256) = 0;

    /// The fingerprints of the locked admin keys here.
    [[nodiscard]] virtual std::vector<std::string> admin_keys() = 0;
    /// Throws ``std::runtime_error`` when the file cannot be read.
    [[nodiscard]] virtual std::string read_admin_key(std::string_view fingerprint) = 0;
    /// Write a locked admin key whole. Throws ``AlreadyThere`` when it is
    /// there already, and ``std::runtime_error`` when it cannot be written.
    virtual void write_admin_key(std::string_view fingerprint, std::string_view contents) = 0;

    /// What ``read_members`` keeps of this store (``member_list.cpp``): each
    /// member list entry read here, parsed and its signature checked once (a
    /// file in the store never changes), and the list they resolve to. Any
    /// thread may use it.
    struct MemberCache;
    [[nodiscard]] MemberCache& member_cache() const;

private:
    mutable std::once_flag member_cache_made_;
    mutable std::shared_ptr<MemberCache> member_cache_;
};

/// The store in a folder: ``<root>/changes/<machine id>/<sequence>.json``
/// (and ``dropped`` beside them, how far they were let go of; and files set
/// aside in ``<root>/damaged/``),
/// ``<root>/members/<machine id>/<sequence>.json``,
/// ``<root>/log/<machine id>/<sequence>.json``,
/// ``<root>/snapshot_index/<machine id>/<files>.json``,
/// ``<root>/snapshot_parts/<sha256>.json`` and
/// ``<root>/admin_keys/<fingerprint>.json``. A machine id that is not one
/// (``is_machine_id``), a number below 1, or a part's name or fingerprint
/// that is not one throws ``std::invalid_argument``: no name from another
/// computer reaches outside the folder.
class FolderStore final : public SyncStore {
public:
    explicit FolderStore(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] std::vector<std::string> machines(Numbered kind) override;
    [[nodiscard]] std::vector<std::int64_t> sequences(Numbered kind,
                                                       std::string_view machine) override;
    [[nodiscard]] std::string read(Numbered kind, std::string_view machine,
                                   std::int64_t sequence) override;
    void write(Numbered kind, std::string_view machine, std::int64_t sequence,
               std::string_view contents) override;
    void set_aside(Numbered kind, std::string_view machine, std::int64_t sequence) override;
    void set_aside_snapshot(const SnapshotName& name) override;
    void drop_changes_through(std::string_view machine, std::int64_t sequence) override;
    [[nodiscard]] std::int64_t dropped_through(std::string_view machine) override;
    [[nodiscard]] std::vector<SnapshotName> snapshots() override;
    [[nodiscard]] std::string read_snapshot(const SnapshotName& name) override;
    void write_snapshot(const SnapshotName& name, std::string_view contents) override;
    bool remove_snapshot(const SnapshotName& name) override;
    [[nodiscard]] std::vector<PartFile> parts() override;
    [[nodiscard]] bool keep_part(std::string_view sha256) override;
    [[nodiscard]] std::string read_part(std::string_view sha256) override;
    void write_part(std::string_view sha256, std::string_view contents) override;
    void write_checked_part(std::string_view sha256, std::string_view contents) override;
    bool remove_part(std::string_view sha256, std::filesystem::file_time_type touched_before) override;
    void set_aside_part(std::string_view sha256) override;
    [[nodiscard]] std::vector<std::string> admin_keys() override;
    [[nodiscard]] std::string read_admin_key(std::string_view fingerprint) override;
    void write_admin_key(std::string_view fingerprint, std::string_view contents) override;

    /// Remove the temp files of writes a crash stopped here, old enough that
    /// no write still owns them (``util::remove_old_temps``), and put back a
    /// part a crash stopped ``remove_part`` with.
    void remove_old_temps();
    /// Remove ``<root>/snapshots``, where snapshots of an earlier format were
    /// kept, which nothing reads: whether it was there and is gone now.
    bool remove_old_snapshots();

private:
    [[nodiscard]] std::filesystem::path folder(Numbered kind) const;
    [[nodiscard]] std::filesystem::path index_dir() const { return root_ / "snapshot_index"; }
    [[nodiscard]] std::filesystem::path admin_keys_dir() const { return root_ / "admin_keys"; }
    [[nodiscard]] std::filesystem::path parts_dir() const { return root_ / "snapshot_parts"; }
    [[nodiscard]] std::filesystem::path part(std::string_view sha256) const;
    [[nodiscard]] std::filesystem::path file(Numbered kind, std::string_view machine,
                                             std::int64_t sequence) const;
    [[nodiscard]] std::filesystem::path snapshot(const SnapshotName& name) const;
    /// Move ``file`` to ``damaged/`` as ``name``.
    void move_aside(const std::filesystem::path& file, const std::string& name) const;

    std::filesystem::path root_;
};

}  // namespace seventhslide::sync
