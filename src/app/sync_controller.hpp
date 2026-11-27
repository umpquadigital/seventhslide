#pragma once

// SyncController - this computer's sync, for the window. main() makes the one,
// beside the web remote, and hands it to the QML as Main.qml's ``sync``.
//
// It keeps every synced library in step, open or not, on this computer's
// sync node (``link::SyncNode``, the one ``seventhslide --server`` runs too,
// with the same settings, ``link::SyncSettings``; set here as a desktop's: a
// window's edits sent, another port when the usual one is taken and none was
// chosen): every synced library in the registry is held there.
// A library is synced when its folder holds a member list
// (``<library>/sync/members``); the registry needs no mark of its own.
//
// For the open library it also keeps its shared log for the Collaboration
// window (``log``, ``CollaborationLog``), and: has the window read again what an
// incoming sync changed (``SessionFacade::reload_after_sync``); holds syncing back while
// something is live, when the operator wants that, and for a moment after the
// last live item closes, as the next is often about to start; and opens a
// viewer's library read-only (``SessionFacade::set_read_only``).
//
// The Sync page's work: putting the open library online, joining one by an
// invite, and stopping syncing one; the admin work on the open library
// (invites, roles, the password, a viewer's copy of the admin key, ...) is its ``admin``'s
// (``SyncAdmin``). Each ends with ``finished(action, result)``:
// ``result.ok``, or ``result.error`` in words, and what the action gives (a
// ``recoveryCode`` to write down, a joined library's ``id``).
//
// Seam: the link (Qt Network), the registry, the session facade.

#include "collaboration_log.hpp"
#include "sync_admin.hpp"

#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/link/library_sync.hpp>
#include <seventhslide/link/link.hpp>
#include <seventhslide/link/sync_node.hpp>
#include <seventhslide/sync/identity.hpp>

#include <QtQml/qqmlregistration.h>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {

class SessionFacade;

class SyncController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("main() creates the one sync controller")

    /// What this computer is called to the others.
    Q_PROPERTY(QString computerName READ computerName NOTIFY settingsChanged)
    /// What the others show beside it (``picture_map``): empty for its
    /// initials.
    Q_PROPERTY(QVariantMap computerPicture READ computerPicture NOTIFY settingsChanged)
    /// Whether changes from the others wait while something is live.
    Q_PROPERTY(bool pauseDuringServices READ pauseDuringServices NOTIFY settingsChanged)
    /// Whether it syncs on its own, rather than only at Sync Now.
    Q_PROPERTY(bool syncAutomatically READ syncAutomatically NOTIFY settingsChanged)
    /// How others reach this computer, as typed: said on the list of every
    /// library it syncs.
    Q_PROPERTY(QStringList reachAddresses READ reachAddresses NOTIFY settingsChanged)
    /// The port chosen for it, as for a forward on a router; 0 for none.
    Q_PROPERTY(int chosenPort READ chosenPort NOTIFY settingsChanged)
    /// A port chosen differs from the one in use until the next start.
    Q_PROPERTY(bool portPending READ portPending NOTIFY settingsChanged)
    /// Why computers outside may not reach it at its port, in words; empty for
    /// no reason.
    Q_PROPERTY(QString portProblem READ portProblem NOTIFY settingsChanged)
    /// The upload speed cap, in megabits a second as typed ("5", "0.5"); empty
    /// for none.
    Q_PROPERTY(QString uploadCap READ uploadCap NOTIFY settingsChanged)
    /// Whether the others of its libraries may be told where it is, and it
    /// where they are.
    Q_PROPERTY(bool shareAddress READ shareAddress NOTIFY settingsChanged)
    /// Work under way that the page waits for (a password being tried).
    Q_PROPERTY(bool working READ working NOTIFY workingChanged)
    /// The open library as the Sync page shows it: ``synced``, ``name``,
    /// ``role`` ("editor" / "viewer"), ``state`` and ``text`` (as for
    /// ``libraryStates``), ``sizeLimit`` (bytes, 0 for none), ``computers``
    /// (``key``, ``machine``, ``name``, ``role``, ``holdsAdminKey``,
    /// ``removed``, ``connected``, ``thisComputer``, ``lastHeard``,
    /// ``addresses`` (those it gave), ``typedAddresses`` (those typed for it
    /// here), ``connectedAt`` (where it is connected from, while it is),
    /// ``lastReachedAt``, ``picture``; then a row for each invite waiting for
    /// its computer: ``key``, ``invite`` (its id), ``name``, ``role``,
    /// ``expires``, ``madeBy``, ``holdsAdminKey`` and ``addresses`` (none),
    /// ``typedAddresses`` (where it is reached out with)) and
    /// ``sizeUsed`` (bytes
    /// of media the size limit counts).
    Q_PROPERTY(QVariantMap openLibrary READ openLibrary NOTIFY stateChanged)
    /// Each synced library's state by registry id: ``state`` ("starting",
    /// "uptodate", "sending", "downloading", "joining", "waiting", "paused",
    /// "missing" (files no connected computer has), "elsewhere" (files not here
    /// that a connected computer is itself still getting), "full" (over its size
    /// limit: files not syncing), "problem", "removed"), ``trouble`` (one of
    /// the last four: shown as a warning), ``text`` (for the operator),
    /// ``viewOnly``, ``active`` (something under way now: a sync, a file going
    /// either way), ``missing`` (how many files no computer has),
    /// ``elsewhere`` (how many are on their way to another computer),
    /// ``tooLarge`` (what is too large to sync, in words, whatever the state;
    /// the state is "problem" for it once nothing passing outranks it) and
    /// ``snapshotProblem`` (why no snapshot is written, in words, likewise,
    /// below ``tooLarge``).
    Q_PROPERTY(QVariantMap libraryStates READ libraryStates NOTIFY stateChanged)
    /// The admin work on the open library.
    Q_PROPERTY(seventhslide::app::SyncAdmin* admin READ admin CONSTANT)
    /// The open library's shared log.
    Q_PROPERTY(seventhslide::app::CollaborationLog* log READ log CONSTANT)

public:
    struct Options {
        /// The data folder: this computer's key and the addresses it
        /// remembers live here.
        std::filesystem::path data_dir;
        /// Take connections and reach out to the other computers; off in
        /// tests that only need the page.
        bool network = true;
        /// Announce this computer on the church network and hear the
        /// others; off in tests, which keep to this computer.
        bool announce = true;
        /// Where to listen unless a port was chosen (``SyncSettings::port``);
        /// another free one when it is taken.
        quint16 port = 8766;
        crypto::BoxParams box_params = crypto::k_box_params;
        /// How long stopping waits for another computer to take this one's
        /// leave from the list.
        int leave_wait_ms = 15000;
        /// How long syncing still waits after the last live item closes, so a
        /// quick change between items does not start it.
        int live_grace_ms = 15000;
        /// Sync Now, not syncing automatically: nothing having moved for this
        /// long, it has synced.
        int sync_now_quiet_ms = 3000;
        /// ... once it has looked this long for the computers not connected.
        int sync_now_reach_ms = 20000;
        /// Files arriving for the open library are told to the window this
        /// long after the first of them.
        int arrived_ms = 250;
    };

    SyncController(SessionFacade& facade, Options options, QObject* parent = nullptr);
    ~SyncController() override;

    [[nodiscard]] QString computerName() const;
    [[nodiscard]] QVariantMap computerPicture() const { return picture_map(settings_.picture); }
    [[nodiscard]] bool pauseDuringServices() const;
    [[nodiscard]] bool syncAutomatically() const noexcept { return node_->sync_automatically(); }
    [[nodiscard]] QStringList reachAddresses() const;
    [[nodiscard]] int chosenPort() const noexcept { return settings_.port.value_or(0); }
    [[nodiscard]] bool portPending() const noexcept;
    [[nodiscard]] QString portProblem() const;
    [[nodiscard]] QString uploadCap() const
    {
        return QString::fromStdString(link::upload_cap_text(settings_.upload_cap_kbit));
    }
    [[nodiscard]] bool shareAddress() const noexcept { return settings_.share_address; }
    [[nodiscard]] bool paused_now() const noexcept { return node_->paused(); }
    [[nodiscard]] bool working() const noexcept { return working_ > 0; }
    [[nodiscard]] QVariantMap openLibrary() const;
    [[nodiscard]] QVariantMap libraryStates() const;
    [[nodiscard]] link::SyncNode& node() noexcept { return *node_; }
    [[nodiscard]] link::Link& link() noexcept { return node_->link(); }
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] SyncAdmin* admin() const noexcept { return admin_; }
    [[nodiscard]] CollaborationLog* log() const noexcept { return log_; }

    // For ``admin``.
    /// The open library's sync; null when it is not synced.
    [[nodiscard]] link::LibrarySync* open_sync() const;
    /// What the open library is called here.
    [[nodiscard]] std::string open_name() const;
    /// Whether this computer takes connections and reaches out.
    [[nodiscard]] bool network() const noexcept { return options_.network; }
    /// Work the page waits for began (``working``); it ends with ``done``.
    void began();
    /// Work began ended: ``finished(action, reply)``.
    void done(const QString& action, const nlohmann::json& reply);

    /// Empty return = done, as for every command here that returns a string.
    Q_INVOKABLE QString setComputerName(const QString& name);
    /// This computer's picture: ``glyph`` (a glyph's name) on ``colour``
    /// ("#rrggbb"); an empty glyph for its initials.
    Q_INVOKABLE QString setComputerPicture(const QString& glyph, const QString& colour);
    /// This computer's picture from the image file at ``url``, its middle cut
    /// to a circle.
    Q_INVOKABLE QString setComputerPictureFile(const QString& url);
    Q_INVOKABLE QString setPauseDuringServices(bool pause);
    Q_INVOKABLE QString setSyncAutomatically(bool automatically);
    /// How others reach this computer: names or addresses, each with ``:port``
    /// when not the one it is set to, apart by commas or spaces; empty for none.
    Q_INVOKABLE QString setReachAddresses(const QString& addresses);
    /// The port to take connections on from the next start: empty for the usual
    /// one, with another when that is taken.
    Q_INVOKABLE QString setChosenPort(const QString& port);
    /// Hold media sent to computers not on this network to ``megabits`` a
    /// second, at once: empty for no cap.
    Q_INVOKABLE QString setUploadCap(const QString& megabits);
    /// Ask to be introduced to the others of each library, or not, at once.
    Q_INVOKABLE QString setShareAddress(bool share);
    /// Reach computer ``machine`` of the open library at ``addresses`` ("host" or
    /// "host:port" each, in turn) before those it gave, on this computer alone;
    /// none to go by its own again. For when its addresses changed and this
    /// computer did not hear, or it gave none this computer can reach.
    Q_INVOKABLE QString setComputerAddresses(const QString& machine, const QStringList& addresses);
    /// Reach out with invite ``invite`` (its id) of the open library at
    /// ``addresses``, for a computer joining that can reach no other of the
    /// library; none to wait for it to reach one. On this computer alone.
    Q_INVOKABLE QString setInviteAddresses(const QString& invite, const QStringList& addresses);
    /// ``typed`` as one more address to reach a computer at, beside ``itsOwn``
    /// (those it gave) and ``added`` (those typed for it already):
    /// ``{address}`` ("host:port"), or ``{error}`` in words.
    Q_INVOKABLE QVariantMap readAddress(const QString& typed, const QStringList& itsOwn,
                                        const QStringList& added) const;
    /// Why ``passphrase`` will not do as an admin password; empty when it will.
    Q_INVOKABLE QString passphraseProblem(const QString& passphrase) const;
    /// What an invite code is for, in words ("Join “Grace Church” as an
    /// editor"), or why it is not one: ``{text}`` or ``{error}``.
    Q_INVOKABLE QVariantMap readInvite(const QString& code) const;

    // Each of these ends with ``finished`` (or returns why it cannot start).
    /// Put the open library online, ``passphrase`` its admin password;
    /// gives the ``recoveryCode``. Whatever an earlier sync left behind (one
    /// stopped, or one this build cannot read) is put aside first: it is a new
    /// synced library, under a uid of its own, which the computers that had
    /// the old one join anew.
    Q_INVOKABLE QString putOnline(const QString& passphrase);
    /// Join the library ``code`` invites to, as a new library on this
    /// computer; gives its registry ``id``. The computers the invite names
    /// are tried until one is reached (``link::Reach``). It opens once it is
    /// all here, unless something is live, and cannot be opened before it
    /// has arrived.
    Q_INVOKABLE QString joinLibrary(const QString& code);
    /// Stop syncing library ``id`` here: this computer leaves its member list
    /// (waiting a while for a computer connected now to take that), and the
    /// library stays as a plain one, under a uid of its own; ``heard`` says
    /// whether another computer took the leave.
    Q_INVOKABLE QString stopSyncing(const QString& id);
    /// Sync every library now, and look for the computers not connected. Not
    /// syncing automatically, it goes on until everything has moved. While
    /// something is live it waits for that, when the operator wants it to.
    Q_INVOKABLE void syncNow();

signals:
    void settingsChanged();
    void stateChanged();
    void workingChanged();
    void finished(const QString& action, const QVariantMap& result);

private:
    struct Held;

    [[nodiscard]] Held* held_for_id(const std::string& id) const;
    [[nodiscard]] Held* open_held() const;
    /// Tell the window what of the open library's files not here is coming.
    void follow_arrivals();
    /// Everything of ``held``'s library the connected computers have is here
    /// and taken in: joined, records in, and nothing still coming or waiting
    /// for its media (``Link::has_all_of``). A file no computer has does not
    /// hold it back.
    [[nodiscard]] bool all_here(const Held& held) const;
    /// Mark ``held`` as joined here and not all arrived, or no longer.
    void mark_arriving(Held& held, bool arriving);
    /// Something of ``held``'s library is under way: a sync running or due,
    /// or a file going either way over a connection.
    [[nodiscard]] bool active(const Held& held) const;
    [[nodiscard]] std::string open_id() const;
    /// Bring the libraries synced here in line with the registry.
    void follow_registry();
    Held& hold(const std::string& id, const std::filesystem::path& root, const std::string& uid);
    void let_go(const std::string& id);
    /// This computer is a viewer of library ``id`` on its member list, so the
    /// library is read-only here (``SessionFacade::set_read_only_check``).
    [[nodiscard]] bool views_only(const std::string& id) const;
    /// The open library changed, or its list did: read-only for a viewer,
    /// and the pause.
    void follow_open_library();
    /// Hold syncing back while something is live, and let it go on a moment
    /// after, when the operator wants that.
    void follow_live();
    void let_live_go();
    /// ``stateChanged`` soon, once, however many things changed.
    void changed();
    /// Keep ``settings`` and say so; why not, when they could not be saved.
    [[nodiscard]] QString keep_settings(link::SyncSettings settings);
    /// The log shows the open library's, with its member list as it stands.
    void follow_log();
    /// ``work`` on ``held``'s thread (``LibrarySync::run``), then ``then``
    /// here, which may let go of the library.
    void run(Held& held, std::function<nlohmann::json(sync::SyncStore&, const sync::Identity&)> work,
             std::function<void(nlohmann::json)> then);
    /// What to tell the operator about media just imported into the open
    /// library: the size limit it goes over (``MediaController::set_import_note``).
    [[nodiscard]] QString import_note(const QStringList& titles, std::int64_t bytes) const;
    /// Why library ``id`` may not be opened yet (still arriving); empty when
    /// it may (``LibraryRegistryController::set_open_check``).
    [[nodiscard]] QString why_not_open(const std::string& id) const;
    /// Stopping: wait for a computer to take this one's leave (its member
    /// list entry ``sequence``, 0 for none), then ``finish_stop``.
    void await_leave(const std::string& id, std::int64_t sequence);
    /// Put library ``id`` aside as a plain library; ``heard``: another
    /// computer has the leave.
    void finish_stop(const std::string& id, bool heard);
    /// Put aside what an earlier sync left in library ``root``, the open one,
    /// for it to start afresh; why not, when it could not be.
    [[nodiscard]] QString clear_earlier_sync(const std::filesystem::path& root);
    [[nodiscard]] QVariantMap state_of(const Held& held) const;

    SessionFacade& facade_;
    Options options_;
    /// As they are kept; the node works by them.
    link::SyncSettings settings_;
    std::unique_ptr<link::SyncNode> node_;
    /// By registry id.
    std::map<std::string, std::unique_ptr<Held>> held_;
    /// Joins started here, to open once they are all here.
    std::set<std::string> to_open_;
    QTimer state_soon_;
    /// The port it asked for at its start.
    quint16 port_at_start_ = 0;
    /// Syncing is held back for what is live, or was a moment ago.
    bool live_held_ = false;
    QTimer live_grace_;
    /// The open library's files arrived since the window was last told, told
    /// together a moment after the first: they often come many at once.
    std::vector<std::string> arrived_;
    /// The open library's files on their way to another computer, last looked at.
    std::set<std::string> elsewhere_;
    QTimer arrived_soon_;
    /// Every second, while sync is on: whether what is moving has changed.
    QTimer activity_;
    /// The libraries (by registry id) with something moving, last looked at.
    /// Each library with something moving: how many media files in, and out.
    std::map<std::string, std::pair<std::size_t, std::size_t>> moving_;
    /// ``libraryStates`` and ``openLibrary``, worked out once after each
    /// change rather than at every look.
    mutable std::optional<QVariantMap> states_;
    mutable std::optional<QVariantMap> open_;
    int working_ = 0;
    SyncAdmin* admin_ = nullptr;
    CollaborationLog* log_ = nullptr;
};

}  // namespace seventhslide::app
