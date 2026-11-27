#pragma once

// One connection to another SeventhSlide. Both ends are alike: once TLS has
// proved which key is on the other end, each says hello with the libraries it
// offers; for each library both have and may share, each tells what its store
// holds (``have``), asks for what it lacks (``want``), and sends what is asked
// for: member list entries first, then the locked admin key (not to a viewer),
// change files and snapshots (an index and its parts), each checked at the
// ``Gate`` before it is kept, and media between them. A change file is kept
// only once the media it declares is on the disk (or no one has it). A store
// that changes, here or by what arrived, is announced with a fresh ``have``.
//
// A snapshot is kept only once all its parts are. Of each library only the
// newest snapshot the other end has is fetched, if newer than any here, and
// one at a time; its index may name ``k_most_parts`` at most. The index, let
// in at the gate, waits here while the parts it names that are not in the
// store are asked of the connection that sent it, at once (not behind media)
// and ``k_parts_at_once`` at a time, each checked by its size and hash
// (``Gate::snapshot_part``) and written as it comes. A part written through
// any connection is waited for by none. Each part is kept once more just
// before the index is stored, and one gone meanwhile fetched again. There
// must be room on the disk for the parts first, or the index is refused for
// now. It never waits for media (that follows the join), nor is it stored on
// a timeout or as the connection closes. It is let go of, for another
// connection to fetch with only the parts still missing, when the other end
// says it has not got a part, sends a bad one, or sends no file for
// ``PeerShared::parts_quiet_ms`` while parts are asked of it; and when a
// snapshot as new is stored. A part that is not what its index names is
// blamed on the computer that sent it, as a media piece is; one this computer
// could not write is its own trouble.
//
// Messages are JSON frames (``frames.hpp``): ``hello``, ``offer`` (a library
// synced here once connected), ``have``, ``have_media`` (a media file come
// here since), ``want``, ``member``, ``log``, ``admin_key``, ``change``, ``snapshot``,
// ``part``, ``piece``, ``pieces`` and ``media`` (each of the last three
// followed by a bytes frame), ``cancel``, ``missing``, ``coming``, ``introduce``, ``met``,
// ``pause`` and ``ping``. Those about a
// library are handled by a table, by type (``handlers``); one this version
// does not know is let alone. A connection that says nothing is asked
// ``ping`` now and then, and one silent for long is closed: a laptop that
// went to sleep is not taken for connected.
//
// The peer decides what is asked for, of whom, and whether what came is
// kept; two parts of it move the bytes. ``Outbox`` is what goes out: the
// messages, then the change files, snapshots' indexes and their parts asked
// for (in pieces when large), then media, each file's pieces' hashes ahead of its bytes, held to
// what the network takes. ``MediaIn`` is the media coming in, each piece
// checked against its hash before it is written, off this thread, so a part
// on the disk holds only checked pieces: a cut-off transfer goes on from
// where it ends,
// from any computer, and a piece that does not match is blamed on the
// computer that sent it, which is not asked for the file again (until it
// next says what it has) and is told to stop sending it (``cancel``).
//
// Nothing that reads a file through holds up the window (or the server's
// other connections): a change file, snapshot's index or part that arrives
// is checked at the gate on a thread of the link's (it may be 16 MiB,
// ``sync::k_largest_store_file``), one at a time, in the order they came, and
// written to the store there; only
// whether a change file counts on another's word is asked here, of the list
// as it is by then.
//
// A computer not on a library's list yet offers the library with its own join
// entry (``Joining::entry``), which proves it holds the invite: the entry says
// which computer it adds, and TLS says which computer this is
// (``sync::proved_invite``). The entry gets no other computer in, so it may be
// offered to any computer. A hello tells who this computer is, by name, only
// where it offers a library: anyone can connect.
//
// A connection made because a better way to the computer was found says so in
// its hello (``replaces``): both ends then keep it and close the one there was,
// unless a plain one crosses it just then (``Link::keep_one``).
//
// Media is asked for a few files at a time (``k_media_at_once``), so the
// computer's other connections share the work: what a connection would ask for
// that is any connection's to fetch (missing here, or owed), it leaves to one
// clearly better placed (``PeerShared::better_for``), as much as that one has
// room for, fetches the rest itself meanwhile, and asks again in a moment for
// what that one did not take. What comes is measured
// (``route``), for the link to rank connections by, and to move a file to a
// better one (``hand_over``).
//
// A computer that shares its address (``PeerShared::share_address``) asks each
// connection, per library, to introduce it (``introduce``, with the port it
// takes connections on: what a router shows of a connection is not that one).
// The link introduces two members that both asked to each other (``met``: the
// other's key and the address it is seen at), and a computer told so may reach
// out there (``Reach``), the key checked as anywhere.
//
// A computer that pauses syncing (while something is live on it) says
// ``pause``: until it says otherwise nothing but the shared log
// (``sync/shared_log.hpp``) moves over the connection, either way. What each
// end was fetching through it, and what it fetched that still waits for its
// media, is let go of, for another connection to fetch, and what is already on
// its way is let go by, so a part on the disk has one writer. Going on, it says
// what it has again.
//
// A message about a library the two do not share is let go by: the other
// end may see the member list differently for a while, and that stops
// nothing else.
//
// Seam: Qt Network.

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/link/frames.hpp"
#include "seventhslide/link/media_in.hpp"
#include "seventhslide/link/outbox.hpp"
#include "seventhslide/link/peer_shared.hpp"
#include "seventhslide/link/synced_library.hpp"
#include "seventhslide/sync/gate.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/member_list.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::link {

/// The protocol both ends speak: a hello that says another is refused. 6: the
/// member list says the invites made, and takes their withdrawal, which every
/// computer letting another in must know. 7: a snapshot is an index and its
/// parts.
inline constexpr int k_protocol_version = 7;
/// A library this computer joins through the peer, by an invite: its own join
/// entry, which it offers.
struct Joining {
    std::string library;
    std::string entry;
};

class Peer final : public QObject {
    Q_OBJECT
public:
    /// ``socket`` is this side's connection, made (connecting) or accepted
    /// (encrypted already); the peer takes it over. ``expected`` is the key
    /// the other end must show, when known.
    /// ``shared`` says what other connections are fetching, among others. It and
    /// ``libraries`` are the link's, and outlive the peer. ``outgoing``: this
    /// side made the connection.
    Peer(QSslSocket* socket, const sync::Identity& me, const std::vector<SyncedLibrary>& libraries,
         PeerShared& shared, std::optional<crypto::PublicKey> expected,
         std::vector<Joining> joining, bool outgoing, QObject* parent = nullptr);
    ~Peer() override;

    /// The other end's key, once TLS has shown it.
    [[nodiscard]] const std::optional<crypto::PublicKey>& key() const noexcept { return key_; }
    /// What the other end calls itself, once it has said hello.
    [[nodiscard]] const std::string& machine() const noexcept { return machine_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    /// It said hello and shares a library with this computer: let in.
    [[nodiscard]] bool admitted() const noexcept { return ready_ && !allowed_.empty(); }
    [[nodiscard]] bool is_closed() const noexcept { return closed_; }
    /// Whether this side made the connection.
    [[nodiscard]] bool outgoing() const noexcept { return outgoing_; }
    /// The other end's network address.
    [[nodiscard]] QHostAddress address() const { return socket_->peerAddress(); }
    /// The key this side expects at the other end, when it made the
    /// connection to a known computer.
    [[nodiscard]] const std::optional<crypto::PublicKey>& expected() const noexcept
    {
        return expected_;
    }
    /// This connection takes the place of the one there is to that computer:
    /// said in the hello, so before that.
    void set_replaces() noexcept { replaces_ = true; }
    /// Whether it does, as this end made it or the other said.
    [[nodiscard]] bool replaces() const noexcept { return replaces_; }
    /// The libraries this computer joins through this connection.
    [[nodiscard]] const std::vector<Joining>& joining() const noexcept { return joining_; }
    /// The libraries both share over this connection.
    [[nodiscard]] const std::set<std::string>& libraries() const noexcept { return allowed_; }
    /// Whether anything is still on its way, either way.
    [[nodiscard]] bool busy() const;
    /// The last of ``machine``'s numbered files of ``library`` the other end
    /// said it has (0 for none, or not heard yet): what has come through.
    [[nodiscard]] std::int64_t their_last(const std::string& library, sync::Numbered kind,
                                          const std::string& machine) const;
    /// Whether the other end shares ``library`` and has said what it has of it.
    [[nodiscard]] bool told(const std::string& library) const;
    /// Whether anything of ``library`` the other end has is still to come
    /// here: asked for (media too), held back for its media, a snapshot
    /// waiting for its parts, or a change file
    /// this connection would ask for next (``want_more``); while either end
    /// has paused, any member list entry or change file they have past the
    /// last here. What was refused, what they said they have not got, and
    /// what waits for more to be known here are not coming.
    [[nodiscard]] bool coming(const std::string& library);
    /// Whether anything of ``library`` is going over this connection now,
    /// either way: asked for and not come yet, held back for its media or
    /// parts, arriving, or being sent.
    [[nodiscard]] bool moving(const std::string& library) const;
    /// The media files of ``library`` on their way in over this connection: asked
    /// for, or waited on by a change file held for them.
    [[nodiscard]] std::set<std::string> media_coming(const std::string& library) const;
    /// Of those, the ones being written here now, and how far each is.
    [[nodiscard]] std::map<std::string, Arriving> media_arriving(const std::string& library) const;
    /// The media files of ``library`` going out over this connection.
    [[nodiscard]] std::set<std::string> media_going(const std::string& library) const;
    /// The id of the invite the other end got into ``library`` by on this
    /// connection, not as a computer already on its list; empty for none.
    [[nodiscard]] std::string invited_by(const std::string& library) const
    {
        const auto it = invited_.find(library);
        return it == invited_.end() ? std::string() : it->second.invite;
    }
    /// Whether the other end said it has not got media file ``path`` of
    /// ``library`` (until it next says what it has, and is asked again).
    [[nodiscard]] bool said_missing(const std::string& library, const std::string& path) const;
    /// Whether the other end said ``path`` of ``library`` is on its way to it.
    [[nodiscard]] bool says_coming(const std::string& library, const std::string& path) const;

    /// How good a way this connection is to fetch media through.
    [[nodiscard]] Route route() const;
    /// ``seconds`` have passed since this was last asked: what media came in
    /// them is measured (``route``).
    void measure(double seconds);
    /// Whether this connection would ask for media ``file`` of ``library`` now:
    /// it shares the library and is not paused, may ask for the file, has not
    /// heard it is not there, and has room for one more (``k_media_at_once``).
    [[nodiscard]] bool could_take(const std::string& library, const db::MediaDigest& file) const;
    /// Whether any media file is asked for over this connection.
    [[nodiscard]] bool fetching_media() const;
    /// The media files of ``library`` whose bytes are coming over this
    /// connection now.
    [[nodiscard]] std::map<std::string, Flowing> media_flowing(const std::string& library) const;
    /// Let media file ``path`` of ``library`` go to connection ``to``: what
    /// comes of the piece coming is taken, then no more is, and the other end is
    /// told to send no more. Once what came is written, ``to`` goes on from
    /// there (``take_over``), or any connection does if it cannot by then.
    void hand_over(const std::string& library, const std::string& path, Peer& to);
    /// Go on with media ``file`` of ``library`` from where its part ends, handed
    /// over by another connection: whether it does (``could_take``).
    bool take_over(const std::string& library, const db::MediaDigest& file);
    /// Ask now for what was left to a better connection (``PeerShared::better_for``)
    /// that has not taken it: it may have had no room after all.
    void ask_again_for_what_it_left();
    /// The upload speed cap changed: what waited for it goes, if it may now.
    void upload_cap_changed();

    /// The port the other end takes connections on, when it asked to be
    /// introduced to the others of ``library``; nullopt when it has not.
    [[nodiscard]] std::optional<std::uint16_t> asks_introductions(const std::string& library) const;
    /// Tell the other end that the computer with ``key``, of ``library``, is at
    /// ``address`` ("host:port"), unless it was told so already.
    void introduce(const std::string& library, const crypto::PublicKey& key, const std::string& address);
    /// Forget what the other end was told of the computer with ``key``, which
    /// may be told it again.
    void forget_introduced(const crypto::PublicKey& key);
    /// This computer began or stopped sharing its address
    /// (``PeerShared::share_address``): the other end hears, for each library.
    void share_address_changed();

    /// Our store of ``library`` changed: tell the other end what we have now.
    void library_changed(const std::string& library);
    /// ``library`` is now synced here too: offer it to the other end.
    void library_added(const std::string& library);
    /// ``library`` is no longer synced here: nothing more about it, either way.
    void library_removed(const std::string& library);
    /// Join ``joining.library`` through this connection too: the library is
    /// offered with its join entry.
    void join(Joining joining);
    /// Another connection let go of something of ``library``: ask for what
    /// is still missing.
    void fetch_more(const std::string& library);
    /// Another connection put ``path`` of ``library`` on the disk: what
    /// waited here for it is kept, and the other end hears it is here.
    void media_arrived(const std::string& library, const std::string& path);
    /// Part ``sha256`` of a snapshot of ``library`` is in the store now: no
    /// snapshot waits here for it.
    void part_arrived(const std::string& library, const std::string& sha256);
    /// Tell the other end which media files of ``library`` are on their way
    /// here, if that has changed since it was last told. Then one it asked
    /// for, and heard is not here, is not taken for lost.
    void tell_coming(const std::string& library, const std::set<std::string>& paths);
    /// This computer paused syncing, or went on (``PeerShared::paused``):
    /// the other end hears, and what was moving stops, or goes on.
    void pause_changed();
    /// Whether the other end has paused syncing.
    [[nodiscard]] bool paused_there() const noexcept { return their_pause_; }
    void close(const QString& why);

signals:
    /// Something new went into our store of ``library`` from this peer.
    void stored(const std::string& library);
    /// Shared log entries went into our store of ``library`` from this peer.
    void logged(const std::string& library);
    /// The other end, member ``machine`` of ``library``, said what it has
    /// applied (``SyncedLibrary::applied``).
    void progress(const std::string& library, const std::string& machine,
                  const std::map<std::string, std::int64_t>& applied);
    /// A snapshot's part of ``library``, or a piece of a large file of its
    /// store, came: more of the library is on its way.
    void files_coming(const std::string& library);
    /// Part ``sha256`` of a snapshot of ``library`` was written to the store.
    void part_stored(const std::string& library, const std::string& sha256);
    /// ``bytes`` of ``path`` have arrived so far.
    void media_progress(const std::string& library, const std::string& path, std::int64_t bytes);
    void became_ready();
    /// A file the gate refused, or one that arrived damaged: why.
    void refused(const QString& why);
    /// Something of ``library`` this connection was fetching was not had
    /// from it: another connection may have it.
    void let_go(const std::string& library);
    /// A media file of ``library`` came through this connection.
    void media_done(const std::string& library, const std::string& path);
    /// The other end asked to be introduced to the others of ``library``, or
    /// stopped asking.
    void introductions_asked(const std::string& library);
    /// The other end said the computer with key ``fingerprint``, of
    /// ``library``, is at ``address`` ("host:port"), as yet unchecked.
    void introduced(const std::string& library, const std::string& fingerprint, const std::string& address);
    void closed(const QString& why);

private:
    struct Pending;
    struct Waiting;
    struct Incoming;
    struct Arrived;
    struct FileCheck;
    struct StoreOutcome;
    struct LibraryState;
    struct PartCheck;

    void on_encrypted();
    void on_read();
    /// Handle what has come, while pieces of media are not waiting to be
    /// written.
    void read_frames();
    /// A message: the hello, a pause, an offer, or (by the table of
    /// ``handlers``) one about a library shared.
    void handle(const nlohmann::json& message);
    void handle_bytes(const QByteArray& bytes);
    void send(const nlohmann::json& message);

    /// What a message about library ``lib`` is handled by, by its type.
    using Handler = void (Peer::*)(const SyncedLibrary& lib, const nlohmann::json& message);
    [[nodiscard]] static const std::map<std::string, Handler, std::less<>>& handlers();
    void take_hello(const nlohmann::json& message);
    void take_pause(const nlohmann::json& message);
    void take_offer(const std::string& uid, const nlohmann::json& message);
    void take_have(const SyncedLibrary& lib, const nlohmann::json& message);
    void take_have_media(const SyncedLibrary& lib, const nlohmann::json& message);
    void take_coming(const SyncedLibrary& lib, const nlohmann::json& message);
    void serve(const SyncedLibrary& lib, const nlohmann::json& want);
    void take_member(const SyncedLibrary& lib, const nlohmann::json& m);
    void take_log(const SyncedLibrary& lib, const nlohmann::json& m);
    void take_admin_key(const SyncedLibrary& lib, const nlohmann::json& m);
    /// A change file or snapshot's index, whole.
    void take_store_file(const SyncedLibrary& lib, const nlohmann::json& m);
    /// A snapshot's part, whole.
    void take_part(const SyncedLibrary& lib, const nlohmann::json& m);
    /// A piece of a change file, snapshot's index or part announced: its
    /// bytes come next.
    void take_piece_announced(const SyncedLibrary& lib, const nlohmann::json& m);
    /// A media file's hashes, or bytes, announced: they come next.
    void take_media_announced(const SyncedLibrary& lib, const nlohmann::json& m);
    void take_cancel(const SyncedLibrary& lib, const nlohmann::json& m);
    void take_introduce(const SyncedLibrary& lib, const nlohmann::json& m);
    void take_met(const SyncedLibrary& lib, const nlohmann::json& m);
    /// Ask to be introduced to the others of ``uid``, or to be no more.
    void ask_introductions(const std::string& uid);
    void take_missing(const SyncedLibrary& lib, const nlohmann::json& m);
    /// The other end has not got part ``sha256`` of ``lib``, asked of it.
    void part_missing_there(const SyncedLibrary& lib, const std::string& sha256);

    [[nodiscard]] const SyncedLibrary* library(const std::string& uid) const;
    /// ``offered`` (in a hello or an offer) shared over this connection: the
    /// library is here and the other end may have it. False, sharing nothing,
    /// when not.
    bool admit(const std::string& uid, const nlohmann::json& offered);
    /// The member list of ``lib`` as its store has it (``sync::read_members``).
    [[nodiscard]] sync::Members members(const SyncedLibrary& lib) const;
    [[nodiscard]] sync::Gate gate(const SyncedLibrary& lib);
    /// Whether the other end may share ``lib``; its role when an invite let
    /// it in.
    [[nodiscard]] bool allowed(const SyncedLibrary& lib, const nlohmann::json& offered);
    /// Whether the other end may hold the locked admin key: an editor, a
    /// viewer given it, or joining as an editor.
    [[nodiscard]] bool may_hold_admin_key(const SyncedLibrary& lib);
    void store_changed(const std::string& uid);
    [[nodiscard]] nlohmann::json have(const SyncedLibrary& lib) const;
    void want_more(const std::string& uid);
    /// Ask for media and snapshots' parts alone: what ``want_more`` asks for
    /// but member list entries and change files, which wait for those asked
    /// for already.
    void want_media(const std::string& uid);
    /// Put in ``want`` the parts the snapshots waiting here need, as many as
    /// ``k_parts_at_once`` lets be asked for.
    void ask_parts(const std::string& uid, nlohmann::json& want);
    /// How many files the newest snapshot in ``lib``'s store holds; 0 for none.
    [[nodiscard]] static std::int64_t newest_snapshot(const SyncedLibrary& lib);
    /// Put the media of ``uid`` to ask for in ``want``: what the change files
    /// held here wait for, what is owed, and what the library says is missing,
    /// as room allows. What is left to a better connection, as much as it has
    /// room for, is added to ``better``; the rest this one asks for itself.
    void ask_media(const std::string& uid, nlohmann::json& want, std::set<Peer*>& better);
    /// Have each of ``better`` ask for media of ``uid``, in a moment.
    void leave_to(const std::set<Peer*>& better, const std::string& uid);
    /// What came of ``path``, handed over, is written: the connection it was
    /// handed to goes on with it, or any may.
    void handed_over(const std::string& uid, const std::string& path);
    /// How many more media files may be asked for over this connection, and of
    /// them how many large ones (``k_media_at_once``).
    struct MediaFree {
        std::size_t files = 0;
        std::size_t large = 0;
    };
    [[nodiscard]] MediaFree media_free() const;
    /// Whether one more media file of ``size`` bytes may be.
    [[nodiscard]] bool media_room(std::int64_t size) const;
    /// A change file or snapshot's index came: checked once its turn comes
    /// (``check_next``).
    void take_file(const SyncedLibrary& lib, std::string machine, std::int64_t number,
                   std::string text, bool snapshot);
    /// Check the next file that came, unless one is being checked.
    void check_next();
    /// ``file`` was checked at the gate (``check_next``), against its
    /// library's member list as it was then.
    void file_checked(const Arrived& file, FileCheck checked);
    /// Write a change file to the store, its media here (or not to be
    /// waited for), or, ``snapshot``, a snapshot's index, its parts here: each
    /// is kept once more first, and one gone is fetched again.
    void store_file(const SyncedLibrary& lib, bool snapshot, const std::string& machine, std::int64_t number,
                    std::shared_ptr<const std::string> text);
    /// Store each snapshot of ``uid`` that waits for no more parts, and let
    /// go of any no newer than one in the store.
    void settle_waiting(const std::string& uid);
    /// Part ``sha256`` of a snapshot came, whole: checked and written off
    /// this thread.
    void part_came(const SyncedLibrary& lib, const std::string& sha256, std::string text);
    void part_checked(const std::string& uid, const std::string& sha256, const PartCheck& checked);
    /// Whether part ``sha256`` of ``uid``, ``size`` bytes, is of the size
    /// each snapshot waiting for it names. When not, it is blamed on the other
    /// end and not taken, as one not what its name says.
    bool size_as_named(const std::string& uid, const std::string& sha256, std::int64_t size);
    /// Let go of the snapshots of ``uid`` waiting for part ``sha256``: not
    /// asked of this connection again until it next says what it has
    /// (``missing_there``) or, ``ours``, until more is known here
    /// (``not_yet``). Another connection may fetch them.
    void drop_waiting(const std::string& uid, const std::string& sha256, bool ours);
    /// Let go of the snapshots of ``uid`` waiting here ``which`` picks, and of
    /// the parts asked for only for them, each marked in ``mark`` when given:
    /// whether there were any.
    bool let_go_waiting(const std::string& uid, const std::function<bool(const Waiting&)>& which,
                        std::set<std::string>* mark);
    /// The parts asked for that no snapshot waiting here needs are let by,
    /// should they come.
    void forget_unneeded_parts(LibraryState& st);
    /// No file has come over this connection for ``PeerShared::parts_quiet_ms``
    /// while a snapshot waits for parts asked of it: each such is let go of,
    /// as if it had said it has not got them.
    void let_go_of_quiet_parts();
    /// Tell the link of the stores that changed since it was last told.
    void tell_stored();
    /// Media file ``path`` is not had from this connection (``MediaIn``):
    /// ``why`` (with the path for %1) is told, the other end is not asked
    /// for it again until it next says what it has, nor sends more of it,
    /// and what waits for it is kept without it.
    void drop_media(const std::string& uid, const std::string& path, const QString& why);
    /// Media file ``path`` could not be taken, and this computer is why
    /// (``MediaIn``): ``why`` is told, and what waits for it goes on waiting.
    void media_failed(const std::string& uid, const std::string& path, const QString& why);
    /// A media file that arrived whole was put in place (``MediaIn``).
    void media_placed(const std::string& uid, const std::string& path);
    /// A piece of a change file, snapshot's index or part came.
    void take_piece(const SyncedLibrary& lib, const Incoming& in, const QByteArray& bytes);
    /// The bytes of change files, snapshots' indexes and parts arriving in
    /// pieces held here, but for ``path`` of ``uid``.
    [[nodiscard]] std::int64_t parts_held_besides(const std::string& uid, const std::string& path) const;
    /// The other end sent ``what`` of ``path`` of library ``uid``, too large
    /// to take: logged, once.
    void too_large(const std::string& uid, const std::string& path, const QString& what);
    /// Every now and then: ask a quiet connection, close a silent one.
    void keep_alive();
    /// Store what of ``uid`` waits for nothing more: its media is here, or it
    /// has waited ``PeerShared::hold_for_media_ms``.
    void settle_pending(const std::string& uid);
    /// Look again at what is held once the first of it has waited long enough.
    void hold_until_next();
    /// ``file`` is stored without what it still needs: that is owed
    /// (``PeerShared::owed_media``).
    void owe(const std::string& uid, const Pending& file);
    /// The connection is closing: store what it holds now, rather than leave it
    /// to be fetched again from a computer that may be gone for days.
    void store_held();
    /// Nothing moves over this connection: this computer or the other end
    /// paused.
    [[nodiscard]] bool halted() const noexcept { return shared_.paused || their_pause_; }
    /// The connection was halted: drop what was going out, let go of what
    /// was asked for and what waits for its media or parts, and let by what
    /// is already on its way.
    void halt();
    /// Not halted any more: ask for what is missing here.
    void go_on();
    /// How a fetch ended.
    enum class Got { Stored, Held, NotHere };
    void got(const std::string& uid, const std::string& item, Got how);
    /// Offer each library being joined here that the other end has not
    /// been offered yet.
    void offer_joining();
    [[nodiscard]] bool wants_admin_keys(const SyncedLibrary& lib);

    QSslSocket* socket_;
    const sync::Identity& me_;
    const std::vector<SyncedLibrary>& libraries_;
    PeerShared& shared_;
    Fetches& fetches_;
    std::optional<crypto::PublicKey> expected_;
    std::vector<Joining> joining_;
    bool outgoing_ = false;
    FrameReader reader_;
    std::optional<crypto::PublicKey> key_;
    std::string machine_;
    std::string name_;
    QTimer keep_alive_;
    /// Fires when the first file held for its media has waited long enough.
    QTimer held_long_enough_;
    QElapsedTimer opened_;
    QElapsedTimer heard_;
    /// Since a change file, index or part, or a piece of one, last came, or
    /// a part was asked for.
    QElapsedTimer files_heard_;
    bool said_hello_ = false;
    bool ready_ = false;
    bool closed_ = false;
    bool their_pause_ = false;
    bool replaces_ = false;
    std::set<std::string> allowed_;
    /// The libraries we offered the other end (in the hello, or since).
    std::set<std::string> offered_;
    /// The invite the other end got in by, per library it let it into.
    struct Invited {
        std::string invite;
        sync::Role role = sync::Role::Viewer;
    };
    std::map<std::string, Invited> invited_;
    /// Per library: the other end's last ``have``, what we asked for and
    /// have not got, what we refused (never asked for again), what may be
    /// let in once more is known here, what we hold back for its media, and
    /// the digests of media we expect.
    struct LibraryState {
        nlohmann::json their_have;
        std::set<std::string> requested;
        std::set<std::string> refused;
        std::set<std::string> not_yet;
        /// What the other end said it has not got, until its next ``have``
        /// (or ``have_media``).
        std::set<std::string> missing_there;
        /// The media files the other end says are on their way to it.
        std::set<std::string> coming_there;
        /// What it was last told is on its way here (``tell_coming``).
        std::set<std::string> told_coming;
        /// The port it takes connections on, while it asks to be introduced.
        std::optional<std::uint16_t> asks_port;
        /// Who it was told is where (``introduce``): "fingerprint address".
        std::set<std::string> told_met;
        std::vector<Pending> pending;
        /// Snapshots let in, waiting for their parts.
        std::vector<Waiting> waiting;
        /// What is being written to the store (``store_file``), by item.
        std::set<std::string> storing;
        std::map<std::string, db::MediaDigest> media_expected;
        /// Change files, snapshots' indexes and parts arriving in pieces, by
        /// item.
        std::map<std::string, std::string> in_pieces;
    };
    std::map<std::string, LibraryState> state_;
    /// How fast media comes over this connection, and the bytes of it that came
    /// since it was last measured.
    Throughput in_rate_;
    std::int64_t media_bytes_in_ = 0;
    /// The libraries whose media this connection left to a better one, since
    /// it last asked again.
    std::set<std::string> left_;
    /// The media files handed over (``hand_over``), by library and path: to
    /// which connection, while it is there.
    std::map<std::pair<std::string, std::string>, QPointer<Peer>> handing_to_;
    /// Change files and snapshots' indexes that came, in order, to be checked.
    /// A list: Arrived is defined in peer_internal.hpp, and only list, vector and
    /// forward_list may be declared with an incomplete type (libc++ refuses a
    /// deque).
    std::list<Arrived> arrived_;
    /// One of them is being checked (``check_next``).
    bool checking_file_ = false;
    std::unique_ptr<Incoming> incoming_;
    std::set<std::string> changed_;
    /// The libraries shared log entries came for, since last told.
    std::set<std::string> logged_;
    /// What goes out, and the media coming in: gone before the rest, so
    /// nothing they report finds the peer half gone.
    Outbox outbox_;
    MediaIn media_in_;
};

}  // namespace seventhslide::link
