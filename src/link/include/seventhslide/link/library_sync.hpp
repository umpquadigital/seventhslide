#pragma once

// One synced library on this computer: an ordinary library folder (database,
// media, ``sync/``) whose engine runs on a thread of its own, so a large
// library applying never holds up the network, the window, or the other
// libraries. The app keeps one for every synced library, open or not; the
// server one for every library it holds.
//
// The link fills the store and reads it through ``store()``, on the main
// thread and on a thread of its own; the library's thread has a store of its
// own on the same folder (a store keeps nothing in memory, and a file is
// written whole under a name no listing takes, then renamed into place). The
// database connection is the thread's alone; the app's window has its own on
// the same file (SQLite, WAL), so the two take turns writing.
//
// A sync applies what arrived and sends what changed here, and notes what it
// sent in the library's shared log. It runs a moment after files arrive
// (``schedule_sync``), a moment after an edit here settles
// (``Options::watch_edits``: another connection to the database committed,
// and something is marked to send), and when asked (``sync_now``). Paused (``set_paused``), it waits; files still arrive.
//
// The library is joined (``SyncEngine::join``) the first time it syncs with
// its member list here, once the link has nothing more on its way, so it
// starts from the newest snapshot rather than replaying every change file.
// It waits while the library's files go on coming (``files_coming``), and
// joins anyway once none has come for a while (``Options::join_wait_ms``).
//
// Seam: Qt Core (a thread, timers), the sync engine.

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/link/sync_turns.hpp"
#include "seventhslide/sync/changed.hpp"
#include "seventhslide/sync/housekeeping.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/sync_engine.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::link {

class LibrarySync final : public QObject {
    Q_OBJECT
public:
    struct Options {
        /// How a new admin key is locked (tests lock quicker).
        crypto::BoxParams box_params = crypto::k_box_params;
        /// Send what is edited here: the app's libraries; not the server's,
        /// where nothing is.
        bool watch_edits = false;
        /// How long an edit here settles before it is sent.
        int edit_settle_ms = 1500;
        /// Joining gives up waiting for the link after this long with none of
        /// the library's files coming.
        qint64 join_wait_ms = 60LL * 1000;
        /// How often old files go (``sync/housekeeping.hpp``), after a sync.
        qint64 prune_every_ms = 60LL * 60 * 1000;
        /// Shared by the libraries of this computer: how many sync at once.
        /// None: each syncs when it will.
        std::shared_ptr<SyncTurns> turns;
    };

    /// ``root`` is the library's folder, made when it is not there.
    /// ``link_busy`` says whether the link has anything on its way, which
    /// holds back joining (above).
    LibrarySync(std::filesystem::path root, std::string uid, sync::Identity me,
                std::function<bool()> link_busy, Options options, QObject* parent = nullptr);
    /// Waits for work under way on the library's thread.
    ~LibrarySync() override;
    LibrarySync(const LibrarySync&) = delete;
    LibrarySync& operator=(const LibrarySync&) = delete;

    [[nodiscard]] const std::string& uid() const noexcept { return uid_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path media() const;
    /// Where the pieces' hashes of its media files are kept
    /// (``sync::PieceHashes``): this computer's own, in its sync folder.
    [[nodiscard]] std::filesystem::path pieces() const;
    /// The store, for the link and anything else on this thread.
    [[nodiscard]] sync::SyncStore& store() noexcept { return *store_; }
    /// Likewise, for the link to hold while it works on it elsewhere.
    [[nodiscard]] const std::shared_ptr<sync::SyncStore>& shared_store() const noexcept
    {
        return store_;
    }

    /// The media files the library's records own that are not on the disk,
    /// as the last sync found them, less those that have come since
    /// (``media_arrived``). Asked for often (the link, the window), so it
    /// does not look at the disk.
    [[nodiscard]] std::vector<db::MediaDigest> missing_media() const;
    /// The media file ``path`` came (``Link::media_stored``).
    void media_arrived(const std::string& path);
    /// More of the library's files came (``Link::files_coming``): joining
    /// waits on for the rest.
    void files_coming();
    /// Member ``machine``, connected, said what it has applied
    /// (``Link::progress``): kept (``<sync>/progress.json``), for old files
    /// to go once every member has them.
    void heard_progress(const std::string& machine, const std::map<std::string, std::int64_t>& applied);
    /// What this library has applied of each computer's change files, as of
    /// the last sync, for the link to tell the others.
    [[nodiscard]] std::map<std::string, std::int64_t> applied_files() const;
    /// The bytes of media the size limit has room for, as of the last sync;
    /// -1 for no limit. The link fetches no file a change brings that is
    /// larger, before storing the change.
    [[nodiscard]] std::int64_t media_room() const;

    /// Sync soon: what arrived is applied once a moment has passed with
    /// nothing more.
    void schedule_sync();
    /// Sync now (unless paused).
    void sync_now();
    /// Mark every record to be sent, then sync: a library put online.
    void share_everything();
    /// What this computer is called in what it writes from now on.
    void set_computer_name(std::string name);
    /// While paused, no sync runs; one asked for runs when it ends.
    void set_paused(bool paused);
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    /// Whether a sync is due or under way, or admin work.
    [[nodiscard]] bool busy() const;

    /// Admin work (``admin_work``) on the library's thread; ``done`` gets the
    /// reply here.
    void admin(nlohmann::json request, std::function<void(nlohmann::json)> done);
    /// Run ``work`` with the library's store on its thread (writing this
    /// computer's own member list entries, say); ``done`` gets what it
    /// returned, or ``{"ok": false, "error": why}`` when it threw.
    void run(std::function<nlohmann::json(sync::SyncStore&, const sync::Identity&)> work,
             std::function<void(nlohmann::json)> done);

    struct Report {
        /// Whether the database opened: when it did not, ``problem`` says
        /// why, and the store is still passed on.
        bool opened = false;
        /// Whether it is the library its store holds yet.
        bool joined = false;
        /// Whether, joined, the library is in it: the others' records taken
        /// in (from a snapshot or change files), or its own when it was
        /// founded here. A library joins empty, and a window opening it
        /// before then would make a library of its own in it (a profile, an
        /// output) for every computer to get.
        bool arrived = false;
        /// What went wrong at the last open or sync, in words; empty for
        /// nothing.
        std::string problem;
        QDateTime last_sync;
        /// Since this object started.
        std::int64_t files_applied = 0;
        std::int64_t snapshots_written = 0;
        std::int64_t changes_sent = 0;
        /// Changes here not sent yet, as of the last look; after a sync, not
        /// counting what it kept as too large, or with one.
        bool unsent = false;
        /// The media against the size limit (``SyncEngine::media_use``).
        sync::MediaUse media;
        /// The records here too large to sync, as of the last sync
        /// (``SyncResult::too_large``).
        std::vector<sync::TooLarge> too_large;
        /// The records kept here with those (``SyncResult::kept_with_too_large``).
        int kept_with_too_large = 0;
        /// Why no snapshot is written while one is due, in words, as of the
        /// last sync (``SyncResult::snapshot_problem``); empty for nothing.
        /// Syncing goes on meanwhile, but old change files are not let go of.
        std::string snapshot_problem;
        /// What it has applied of each computer's change files.
        std::map<std::string, std::int64_t> applied;
        struct Computer {
            std::string machine;
            /// As it called itself; empty when nothing here says.
            std::string name;
        };
        /// The computers change files here wait on that this computer refuses
        /// (``SyncResult::waiting_on``), as of the last sync with nothing more
        /// on its way: what they wrote is not here, and the files made on top
        /// of it wait until a snapshot holding it comes.
        std::vector<Computer> waiting_on;
    };
    [[nodiscard]] Report report() const;

signals:
    /// The library's thread wrote to the store: a change file, a snapshot,
    /// or admin work.
    void wrote();
    /// A sync finished: what it did, for the log. ``problem`` is why it
    /// could not, in words; empty when it could.
    void synced(int files_applied, bool snapshot_written, const QString& problem);
    /// A note of what a sync sent went into the shared log
    /// (``sync/change_notes.hpp``).
    void logged();
    /// Changes from other computers went into these records: whatever shows
    /// them reads them again.
    void applied(const seventhslide::sync::Changed& changed);
    /// ``report()`` changed.
    void reportChanged();

private:
    struct Work;

    void start_sync();
    /// The sync ``start_sync`` began, on its turn.
    void run_sync();
    /// ``missing`` as the library's thread found it; under ``mutex_``.
    void take_missing(std::vector<db::MediaDigest> missing);
    [[nodiscard]] std::filesystem::path progress_file() const;
    void load_progress();
    /// Under ``mutex_``.
    void save_progress() const;
    void finish_sync(bool wrote, int files_applied, bool snapshot, const std::string& problem,
                     const sync::Changed& changed);

    std::filesystem::path root_;
    std::string uid_;
    std::function<bool()> link_busy_;
    Options options_;
    std::shared_ptr<sync::SyncStore> store_;
    QThread thread_;
    /// Lives on ``thread_``: what runs there runs in its name.
    QObject* context_ = nullptr;
    std::unique_ptr<Work> work_;
    QTimer debounce_;
    bool syncing_ = false;
    /// Whether a turn (``Options::turns``) is this library's now.
    bool holding_turn_ = false;
    bool again_ = false;
    bool paused_ = false;
    bool wanted_while_paused_ = false;
    int running_ = 0;
    /// When the sync now waiting was first asked for.
    QDateTime holding_since_;
    /// When joining was first held back for the link, or files last came
    /// since; invalid when not held back.
    QDateTime join_wait_;

    mutable std::mutex mutex_;
    Report report_;
    std::vector<db::MediaDigest> missing_;
    /// Media that came while the library's thread was working out what is
    /// missing: it may have looked before they did.
    std::set<std::string> arrived_meanwhile_;
    /// What each other member said it has applied, and when.
    std::map<std::string, sync::Progress> progress_;
};

}  // namespace seventhslide::link
