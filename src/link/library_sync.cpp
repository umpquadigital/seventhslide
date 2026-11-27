#include "seventhslide/link/library_sync.hpp"

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_outbox.hpp"
#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/library/library.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"
#include "seventhslide/link/admin.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/gate.hpp"
#include "seventhslide/sync/housekeeping.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/now_utc.hpp"
#include "seventhslide/sync/shared_log.hpp"
#include "seventhslide/sync/sync_engine.hpp"

#include <QElapsedTimer>
#include <QLoggingCategory>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <algorithm>
#include <string>

#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace seventhslide::link {
namespace {

Q_LOGGING_CATEGORY(lc_library_sync, "seventhslide.sync", QtInfoMsg)

/// Where library ``root`` keeps its media files' pieces' hashes: in its sync
/// folder, which is this computer's.
std::filesystem::path piece_hashes_folder(const std::filesystem::path& root)
{
    return library::sync_root(root) / "pieces";
}

/// What a sync freed goes back to the system. The C library keeps memory a
/// thread freed for that thread to use again; with a thread for every
/// library, a server holding many would keep the most each library ever
/// took, all at once (30 libraries: 900 MB rather than 200). Not under the
/// address or thread sanitizer, whose allocator is not the C library's:
/// malloc_trim then reads an arena that was never set up, and crashes.
void give_back_memory()
{
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    (void)malloc_trim(0);
#endif
}

/// A sync waits this long for more to arrive...
constexpr int k_settle_ms = 300;
/// ...but no longer than this after the first thing that did.
constexpr qint64 k_longest_wait_ms = 2000;
/// How often the library's thread looks for an edit made here.
constexpr int k_edit_check_ms = 1000;

}  // namespace

/// What lives on the library's thread, and is touched only there.
struct LibrarySync::Work {
    Work(std::filesystem::path root_, std::string uid_, sync::Identity me_, Options options_)
        : root(std::move(root_)), uid(std::move(uid_)), me(std::move(me_)), options(options_),
          store(library::sync_root(root))
    {
    }

    std::filesystem::path root;
    std::string uid;
    sync::Identity me;
    Options options;
    sync::FolderStore store;
    /// Read only for a member whose files count by the member list alone no
    /// longer (``refuses``): rarely.
    sync::Vouches vouches{store};
    std::optional<db::Database> db;
    std::optional<sync::SyncEngine> engine;
    bool joined = false;
    /// SQLite's count of commits by other connections, last looked at.
    int data_version = -1;
    QElapsedTimer edited;

    /// What a sync did.
    struct Outcome {
        bool wrote = false;
        int files_applied = 0;
        bool snapshot = false;
        int changes_sent = 0;
        bool joined = false;
        bool arrived = false;
        bool unsent = false;
        /// A note of what it sent went in the shared log.
        bool logged = false;
        std::string problem;
        std::optional<std::vector<db::MediaDigest>> missing;
        std::optional<sync::MediaUse> media;
        std::map<std::string, std::int64_t> applied;
        /// The computers files here wait on that the gate refuses
        /// (``SyncResult::waiting_on``).
        std::vector<Report::Computer> waiting_on;
        sync::Changed changed;
        std::vector<sync::TooLarge> too_large;
        int kept_with_too_large = 0;
        std::string snapshot_problem;
    };

    /// When old files last went; invalid before they first did.
    QElapsedTimer pruned;

    /// Let old files go (``sync/housekeeping.hpp``), by what the members
    /// said (``heard``; one not heard yet is added, as noticed now).
    void prune(std::map<std::string, sync::Progress>& heard)
    {
        const auto now = sync::now_utc();
        const auto members = sync::read_members(store, uid);
        for (const auto& [machine, m] : members.members) {
            if (!m.removed && machine != me.machine && !heard.contains(machine)) {
                heard[machine] = sync::Progress{{}, now};
            }
        }
        sync::let_go(store, sync::old_files(store, members, me.machine, db::SyncBookkeeping(opened_db()).applied(),
                                            heard, now));
        // Snapshots of the format before parts, which nothing reads now.
        if (store.remove_old_snapshots()) {
            qCInfo(lc_library_sync).noquote()
                << "The old snapshots of library" << QString::fromStdString(uid) << "were removed";
        }
        pruned.start();
    }

    /// The engine takes the size limit the member list sets now.
    /// What the member list says of how the engine goes about the library.
    void read_list()
    {
        if (!engine) {
            return;
        }
        try {
            const auto list = sync::read_members(store, uid);
            engine->set_size_limit(list.size_limit);
            const auto* self = list.find(me.machine);
            const bool viewer = self != nullptr && self->role != sync::Role::Editor;
            engine->set_writes_snapshots(!viewer);
            engine->set_holds_only(viewer && !self->has_edited);
            // Only an editor writes the snapshot that carries them.
            engine->set_refused_by_others(viewer ? std::map<std::string, std::int64_t>{}
                                                 : refused_by_others(list));
        } catch (const std::exception&) {
            // The list says nothing yet: no limit, nothing to hold back, and no
            // one refused.
            engine->set_refused_by_others({});
        }
    }

    /// Whether the gate refuses ``machine``'s change file ``n`` now, as the
    /// link asks it (``Gate::may_count``). Another's file made on top of it
    /// counts for it only while ``machine`` is a member, so the store is read
    /// only then.
    bool refuses(const sync::Members& list, const std::string& machine, std::int64_t n)
    {
        if (list.may_have_written(machine, n)) {
            return false;
        }
        if (list.find(machine) == nullptr) {
            return true;
        }
        vouches.refresh();
        return !sync::Gate(list, &vouches).may_count(machine, n);
    }

    /// The change files applied here that the gate would not let in now, by
    /// computer (``SyncEngine::set_refused_by_others``).
    std::map<std::string, std::int64_t> refused_by_others(const sync::Members& list)
    {
        std::map<std::string, std::int64_t> out;
        for (const auto& [machine, n] : db::SyncBookkeeping(opened_db()).applied()) {
            if (machine != me.machine && n > 0 && refuses(list, machine, n)) {
                out.emplace(machine, n);
            }
        }
        return out;
    }

    /// What computer ``machine`` called itself in the member list entries it
    /// wrote for this library (counted or not: a refused join still says);
    /// empty when none do.
    std::string name_of(const std::string& machine)
    {
        std::string name;
        for (const auto n : store.sequences(sync::Numbered::Members, machine)) {
            try {
                const auto body = sync::unseal(store.read(sync::Numbered::Members, machine, n),
                                               "A member list entry")
                                      .body;
                if (body.value("library", std::string()) != uid) {
                    continue;
                }
                // A join or founding names the computer, and a rename names it again.
                const auto said = body.value("kind", std::string()) == "name"
                                      ? body
                                      : body.value("computer", nlohmann::json::object());
                if (const auto it = said.find("name");
                    it != said.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
                    name = it->get<std::string>();
                }
            } catch (const std::exception&) {
                // not one to go by
            }
        }
        return name;
    }

    /// The database open() opened; what works on the library asks for it
    /// only once it has.
    db::Database& opened_db()
    {
        if (!db) {
            throw std::logic_error("The library's database is not open.");
        }
        return *db;
    }

    /// Open the database; ``problem`` when it will not.
    std::string open()
    {
        // What a crash left of a write here.
        store.remove_old_temps();
        try {
            auto opened = db::Database::open_file(library::database_file(root));
            db::ensure_schema(opened);
            auto& database = db.emplace(std::move(opened));
            engine.emplace(database, store, library::media_root(root), me);
            engine->set_piece_hashes(piece_hashes_folder(root));
            read_list();
            joined = db::library_uid(database) == uid;
            data_version = database.query_int("PRAGMA data_version", -1);
            return {};
        } catch (const std::exception& ex) {
            engine.reset();
            db.reset();
            return std::string("The library's database could not be opened: ") + ex.what();
        }
    }

    /// Joined, and the library is in it (``Report::arrived``): it holds
    /// records, the others' or (founded here) its own. A library joins
    /// empty, and is until the others' snapshot or change files are taken in.
    [[nodiscard]] bool arrived()
    {
        if (!db || !joined) {
            return false;
        }
        for (const auto table : db::record_tables_in_order()) {
            if (table != "library_meta"
                && db->query_int("SELECT EXISTS(SELECT 1 FROM " + std::string(table) + ")", 0) == 1) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool unsent()
    {
        return db && db->query_int("SELECT EXISTS(SELECT 1 FROM sync_outbox)", 0) == 1;
    }

    /// Whether anything is still to send after ``result``: a record marked,
    /// but not one it kept here as too large, or with one.
    [[nodiscard]] bool unsent_after(const sync::SyncResult& result)
    {
        if (result.too_large.empty()) {
            return unsent();
        }
        std::set<std::pair<std::string, std::string>> kept(result.kept_with_too_large.begin(),
                                                           result.kept_with_too_large.end());
        for (const auto& r : result.too_large) {
            kept.emplace(r.table, r.key);
        }
        return std::ranges::any_of(db::marked_records(opened_db()), [&](const db::MarkedRecord& m) {
            return !kept.contains({m.table, m.key});
        });
    }

    /// Whether an edit made here has settled with something to send.
    bool edit_settled()
    {
        if (!db || !joined) {
            return false;
        }
        const int version = db->query_int("PRAGMA data_version", -1);
        if (version != data_version) {
            data_version = version;
            edited.start();
            return false;
        }
        if (!edited.isValid() || edited.elapsed() < options.edit_settle_ms) {
            return false;
        }
        edited.invalidate();
        return unsent();
    }

    /// Note in the shared log what a sync sent; whether it wrote. A note that
    /// cannot be written is only lost: what it notes went.
    bool note(const std::vector<sync::SentRecord>& sent)
    {
        if (sent.empty()) {
            return false;
        }
        try {
            auto notes = sync::change_notes(opened_db(), sent);
            if (notes.items.empty()) {
                return false;
            }
            sync::LogBook(store, me, uid).post_changes(std::move(notes.items), std::move(notes.counts));
            return true;
        } catch (const std::exception& ex) {
            qCWarning(lc_library_sync).noquote() << "What was sent is not in the shared log:" << ex.what();
            return false;
        }
    }

    /// ``may_catch_up``: the link has nothing more on its way, so a file not
    /// here is not coming (``SyncEngine::sync``).
    Outcome sync(bool may_join, bool may_catch_up)
    {
        Outcome out;
        if (!engine) {
            out.problem = "The library's database is not open.";
            return out;
        }
        sync::SyncEngine& eng = *engine;
        try {
            if (!joined) {
                if (!may_join || !sync::read_members(store, uid).founded()) {
                    return out;  // nothing to be yet: the library has not come
                }
                eng.join(uid);
                joined = true;
            }
            out.joined = true;
            read_list();
            const auto result = eng.sync(may_catch_up);
            out.files_applied = result.files_applied;
            out.snapshot = result.snapshot_written;
            if (result.files_applied > 0) {
                // The files just applied may be such files themselves.
                read_list();
                out.snapshot = eng.snapshot_refused() || out.snapshot;
            }
            out.changes_sent = result.changes_sent;
            out.wrote = result.changes_sent > 0 || out.snapshot;
            out.logged = note(result.sent);
            out.wrote = out.wrote || out.logged;
            out.missing = result.media_missing;
            out.media = result.media;
            out.changed = result.changed;
            out.too_large = result.too_large;
            out.kept_with_too_large = static_cast<int>(result.kept_with_too_large.size());
            out.snapshot_problem = result.snapshot_problem;
            out.unsent = unsent_after(result);
            out.arrived = arrived();
            out.applied = db::SyncBookkeeping(opened_db()).applied();
            // Only those it refuses: one whose files are on their way, or will
            // be once a connection is back, is not waited on for good.
            if (!result.waiting_on.empty()) {
                const auto list = sync::read_members(store, uid);
                const auto applied = db::SyncBookkeeping(opened_db()).applied();
                for (const auto& machine : result.waiting_on) {
                    const auto next = applied.contains(machine) ? applied.at(machine) + 1 : 1;
                    if (refuses(list, machine, next)) {
                        out.waiting_on.push_back({machine, name_of(machine)});
                    }
                }
            }
            // This connection's own commits leave data_version be; another's
            // during the sync is looked at next time.
        } catch (const std::exception& ex) {
            out.joined = joined;
            out.problem = ex.what();
        }
        return out;
    }
};

LibrarySync::LibrarySync(std::filesystem::path root, std::string uid, sync::Identity me,
                         std::function<bool()> link_busy, Options options, QObject* parent)
    : QObject(parent), root_(std::move(root)), uid_(std::move(uid)),
      link_busy_(std::move(link_busy)), options_(options),
      store_(std::make_shared<sync::FolderStore>(library::sync_root(root_))),
      work_(std::make_unique<Work>(root_, uid_, std::move(me), options))
{
    std::error_code ec;
    std::filesystem::create_directories(media(), ec);
    std::filesystem::create_directories(library::sync_root(root_), ec);
    load_progress();

    context_ = new QObject;
    context_->moveToThread(&thread_);
    thread_.setObjectName(QStringLiteral("library %1").arg(QString::fromStdString(uid_.substr(0, 8))));
    thread_.start();

    debounce_.setSingleShot(true);
    debounce_.setInterval(k_settle_ms);
    connect(&debounce_, &QTimer::timeout, this, &LibrarySync::start_sync);

    // Open on the library's thread, then catch up with what is in the store.
    syncing_ = true;
    QMetaObject::invokeMethod(context_, [this, work = work_.get()] {
        const auto problem = work->open();
        {
            const std::lock_guard lock(mutex_);
            report_.opened = problem.empty();
            report_.joined = work->joined;
            report_.arrived = work->arrived();
            report_.problem = problem;
            report_.unsent = work->unsent();
            if (work->db) {
                try {
                    report_.applied = db::SyncBookkeeping(*work->db).applied();
                } catch (const std::exception&) {
                    // the first sync says what is wrong
                }
            }
            if (work->engine && work->joined) {
                try {
                    report_.media = work->engine->media_use();
                    take_missing(work->engine->missing_media(report_.media));
                } catch (const std::exception&) {
                    // the first sync says what is wrong
                }
            }
        }
        if (work->options.watch_edits) {
            // On this thread: the database is looked at where it is used.
            auto* edits = new QTimer(context_);
            edits->setInterval(k_edit_check_ms);
            connect(edits, &QTimer::timeout, context_, [this, work] {
                if (work->edit_settled()) {
                    QMetaObject::invokeMethod(this, [this] { schedule_sync(); }, Qt::QueuedConnection);
                }
            });
            edits->start();
        }
        QMetaObject::invokeMethod(this, [this] {
            syncing_ = false;
            emit reportChanged();
            schedule_sync();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

LibrarySync::~LibrarySync()
{
    debounce_.stop();
    if (options_.turns) {
        options_.turns->forget(this);
        if (std::exchange(holding_turn_, false)) {
            options_.turns->give();  // its sync's end will not come now
        }
    }
    // The database closes on the thread that opened it, after whatever is
    // under way there; the edit watch goes with the thread's objects.
    QMetaObject::invokeMethod(context_, [this, work = work_.get()] {
        qDeleteAll(context_->children());
        work->engine.reset();
        work->db.reset();
    }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    delete context_;
}

std::filesystem::path LibrarySync::media() const
{
    return library::media_root(root_);
}

std::filesystem::path LibrarySync::pieces() const
{
    return piece_hashes_folder(root_);
}

std::vector<db::MediaDigest> LibrarySync::missing_media() const
{
    const std::lock_guard lock(mutex_);
    return missing_;
}

void LibrarySync::media_arrived(const std::string& path)
{
    const std::lock_guard lock(mutex_);
    std::erase_if(missing_, [&](const db::MediaDigest& d) { return d.path == path; });
    arrived_meanwhile_.insert(path);
}

void LibrarySync::take_missing(std::vector<db::MediaDigest> missing)
{
    // Under the lock. What came after the library's thread looked is here.
    std::erase_if(missing, [&](const db::MediaDigest& d) { return arrived_meanwhile_.contains(d.path); });
    missing_ = std::move(missing);
}

void LibrarySync::files_coming()
{
    if (join_wait_.isValid()) {
        join_wait_ = QDateTime::currentDateTimeUtc();
    }
}

void LibrarySync::schedule_sync()
{
    if (!debounce_.isActive()) {
        holding_since_ = QDateTime::currentDateTimeUtc();
        debounce_.start();
    } else if (holding_since_.msecsTo(QDateTime::currentDateTimeUtc()) < k_longest_wait_ms) {
        debounce_.start();  // more is arriving: wait for it
    }
}

void LibrarySync::sync_now()
{
    debounce_.stop();
    start_sync();
}

void LibrarySync::share_everything()
{
    // Queued before the sync it asks for, so done first.
    QMetaObject::invokeMethod(context_, [work = work_.get()] {
        if (work->engine) {
            work->engine->share_everything();
        }
    }, Qt::QueuedConnection);
    schedule_sync();
}

void LibrarySync::set_computer_name(std::string name)
{
    QMetaObject::invokeMethod(context_, [work = work_.get(), name = std::move(name)] {
        work->me.computer = name;
        if (work->engine) {
            work->engine->set_computer_name(name);
        }
    }, Qt::QueuedConnection);
}

void LibrarySync::set_paused(bool paused)
{
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    if (!paused_ && std::exchange(wanted_while_paused_, false)) {
        schedule_sync();
    }
}

bool LibrarySync::busy() const
{
    return debounce_.isActive() || syncing_ || running_ > 0;
}

void LibrarySync::start_sync()
{
    if (paused_) {
        wanted_while_paused_ = true;
        return;
    }
    if (syncing_) {
        again_ = true;
        return;
    }
    bool joined = false;
    {
        const std::lock_guard lock(mutex_);
        joined = report_.joined;
    }
    if (!joined && link_busy_ && link_busy_()) {
        // Still arriving: the newest snapshot may be among it. Given up on
        // only once nothing of it has come for a while (``files_coming``).
        if (!join_wait_.isValid()) {
            join_wait_ = QDateTime::currentDateTimeUtc();
        }
        if (join_wait_.msecsTo(QDateTime::currentDateTimeUtc()) < options_.join_wait_ms) {
            debounce_.start();
            return;
        }
    }
    join_wait_ = {};
    syncing_ = true;
    // Its turn among the libraries of this computer, now or when one is free.
    if (options_.turns) {
        options_.turns->take(this, [this] {
            holding_turn_ = true;
            run_sync();
        });
    } else {
        run_sync();
    }
}

void LibrarySync::run_sync()
{
    const bool quiet = !link_busy_ || !link_busy_();
    {
        // The library's thread looks at the disk from now on.
        const std::lock_guard lock(mutex_);
        arrived_meanwhile_.clear();
    }
    QMetaObject::invokeMethod(context_, [this, work = work_.get(), quiet] {
        auto outcome = work->sync(true, quiet);
        {
            const std::lock_guard lock(mutex_);
            report_.joined = outcome.joined;
            report_.problem = outcome.problem;
            if (outcome.joined && outcome.problem.empty()) {
                report_.arrived = outcome.arrived;
                report_.last_sync = QDateTime::currentDateTimeUtc();
                report_.files_applied += outcome.files_applied;
                report_.snapshots_written += outcome.snapshot ? 1 : 0;
                report_.changes_sent += outcome.changes_sent;
                report_.unsent = outcome.unsent;
            }
            if (outcome.missing) {
                take_missing(std::move(*outcome.missing));
            }
            if (outcome.media) {
                report_.media = std::move(*outcome.media);
            }
            if (outcome.joined && outcome.problem.empty()) {
                report_.applied = outcome.applied;
                // Said once, when first found: it is not sent at every sync.
                for (const auto& r : outcome.too_large) {
                    if (std::ranges::none_of(report_.too_large, [&](const sync::TooLarge& had) {
                            return had.table == r.table && had.key == r.key;
                        })) {
                        qCWarning(lc_library_sync).nospace().noquote()
                            << "A record of library " << QString::fromStdString(uid_)
                            << " is too large to sync, and stays on this computer: " << r.table.c_str() << " "
                            << r.key.c_str() << " (" << QString::fromStdString(r.title) << "), " << r.bytes
                            << " bytes; the most is " << db::k_largest_record;
                    }
                }
                report_.too_large = std::move(outcome.too_large);
                report_.kept_with_too_large = outcome.kept_with_too_large;
                // Likewise, when it is new.
                if (!outcome.snapshot_problem.empty() && outcome.snapshot_problem != report_.snapshot_problem) {
                    qCWarning(lc_library_sync).noquote()
                        << "A snapshot of library" << QString::fromStdString(uid_)
                        << "was not written:" << QString::fromStdString(outcome.snapshot_problem);
                }
                report_.snapshot_problem = std::move(outcome.snapshot_problem);
                // Waiting for good only once nothing more is on its way; meanwhile
                // only what has come since lets one off.
                if (quiet) {
                    report_.waiting_on = outcome.waiting_on;
                } else {
                    std::erase_if(report_.waiting_on, [&](const Report::Computer& c) {
                        return std::ranges::none_of(outcome.waiting_on, [&](const Report::Computer& w) {
                            return w.machine == c.machine;
                        });
                    });
                }
            }
        }
        // Now and then, old files go.
        if (outcome.joined && outcome.problem.empty()
            && (!work->pruned.isValid() || work->pruned.elapsed() >= work->options.prune_every_ms)) {
            std::map<std::string, sync::Progress> heard;
            {
                const std::lock_guard lock(mutex_);
                heard = progress_;
            }
            try {
                work->prune(heard);
                const std::lock_guard lock(mutex_);
                for (auto& [machine, p] : heard) {
                    progress_.try_emplace(machine, std::move(p));  // those first noticed now
                }
                save_progress();
            } catch (const std::exception& ex) {
                outcome.problem = std::string("Old sync files could not be let go of: ") + ex.what();
                const std::lock_guard lock(mutex_);
                report_.problem = outcome.problem;
            }
        }
        give_back_memory();
        QMetaObject::invokeMethod(this, [this, outcome] {
            finish_sync(outcome.wrote, outcome.files_applied, outcome.snapshot, outcome.problem,
                        outcome.changed);
            if (outcome.logged) {
                emit logged();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void LibrarySync::finish_sync(bool wrote, int files_applied, bool snapshot,
                              const std::string& problem, const sync::Changed& changed)
{
    syncing_ = false;
    if (std::exchange(holding_turn_, false)) {
        options_.turns->give();
    }
    if (wrote) {
        emit this->wrote();
    }
    if (!changed.empty()) {
        emit applied(changed);
    }
    if (files_applied > 0 || snapshot || !problem.empty()) {
        emit synced(files_applied, snapshot, QString::fromStdString(problem));
    }
    emit reportChanged();
    if (std::exchange(again_, false)) {
        schedule_sync();
    }
}

void LibrarySync::run(std::function<nlohmann::json(sync::SyncStore&, const sync::Identity&)> work,
                      std::function<void(nlohmann::json)> done)
{
    ++running_;
    QMetaObject::invokeMethod(context_, [this, w = work_.get(), work = std::move(work),
                                         done = std::move(done)]() mutable {
        nlohmann::json reply;
        try {
            reply = work(w->store, w->me);
        } catch (const std::exception& ex) {
            reply = {{"ok", false}, {"error", ex.what()}};
        }
        const bool wrote = reply.value("wrote", false);
        QMetaObject::invokeMethod(this, [this, reply = std::move(reply), wrote,
                                         done = std::move(done)] {
            --running_;
            if (wrote) {
                emit this->wrote();
                schedule_sync();  // the list may say something new: a size limit, say
            }
            done(reply);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void LibrarySync::admin(nlohmann::json request, std::function<void(nlohmann::json)> done)
{
    const auto params = options_.box_params;
    const auto uid = uid_;
    run(
        [request = std::move(request), params, uid](sync::SyncStore& store, const sync::Identity& me) {
            auto reply = admin_work(store, me, uid, request, params);
            reply["wrote"] = reply.value("ok", false);
            return reply;
        },
        std::move(done));
}

std::filesystem::path LibrarySync::progress_file() const
{
    return library::sync_root(root_) / "progress.json";
}

void LibrarySync::load_progress()
{
    const auto text = util::read_file(progress_file());
    if (!text) {
        return;
    }
    const auto j = nlohmann::json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return;
    }
    for (const auto& [machine, p] : j.items()) {
        if (!sync::is_machine_id(machine) || !p.is_object() || !p.contains("at") || !p["at"].is_string()) {
            continue;
        }
        sync::Progress progress{{}, p["at"].get<std::string>()};
        if (const auto applied = p.find("applied"); applied != p.end() && applied->is_object()) {
            for (const auto& [m, n] : applied->items()) {
                if (sync::is_machine_id(m) && n.is_number_integer()) {
                    progress.applied.emplace(m, n.get<std::int64_t>());
                }
            }
        }
        progress_.emplace(machine, std::move(progress));
    }
}

void LibrarySync::save_progress() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [machine, p] : progress_) {
        j[machine] = {{"applied", p.applied}, {"at", p.at}};
    }
    (void)util::write_file_atomically(progress_file(), j.dump(2));
}

void LibrarySync::heard_progress(const std::string& machine, const std::map<std::string, std::int64_t>& applied)
{
    const std::lock_guard lock(mutex_);
    auto& p = progress_[machine];
    for (const auto& [m, n] : applied) {
        auto& had = p.applied[m];
        had = std::max(had, n);
    }
    p.at = sync::now_utc();
}

std::map<std::string, std::int64_t> LibrarySync::applied_files() const
{
    const std::lock_guard lock(mutex_);
    return report_.applied;
}

LibrarySync::Report LibrarySync::report() const
{
    const std::lock_guard lock(mutex_);
    return report_;
}

std::int64_t LibrarySync::media_room() const
{
    const std::lock_guard lock(mutex_);
    const auto& media = report_.media;
    return media.limit > 0 ? std::max<std::int64_t>(0, media.limit - media.used) : -1;
}

}  // namespace seventhslide::link
