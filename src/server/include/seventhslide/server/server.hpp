#pragma once

// SeventhSlide as a server (docs/SERVER.md):
// always on, holding synced libraries and passing them between the computers
// that sync them. No window, no QML, no video: this module links Qt Core,
// Network and Sql only, over core and the link.
//
// Its sync is the one every SeventhSlide runs (``link::SyncNode``), with the
// same settings (``link::SyncSettings``, which flags override for a run): a
// fixed port, a few libraries syncing at once, no edits of its own. What it
// adds is which libraries it keeps: joining one, the record of what it holds,
// leaving one, its status and its commands.
//
// Its data folder holds what is the server's own and the libraries it holds:
//
//   machine.json         its machine id (``library::MachineStore``)
//   sync_settings.json   its sync settings
//   computer_key.pem     its key: who it is to every computer
//   server.lock          one server per data folder
//   control              where the commands find it (``control.hpp``)
//   held.json            the libraries it holds
//   sync_heard.json      when each computer was last heard from
//   sync_addresses.json  where it last reached each computer
//   libraries/<uid>/     each an ordinary library folder
//   left/<uid>-<when>/   a library it stopped holding, kept until deleted
//
// A library comes to it as to any computer, by an invite (``join``), usually
// as a viewer, so that its key changes nothing however exposed it is. When it
// can reach no one, a member told where it is reaches out to it there with the
// invite (``link::Reach::set_invite_addresses``).
//
// Like any computer it announces itself on the church network and keeps
// connected to the libraries' other computers it can reach
// (``link::Reach``): computers there, and other servers; computers at home
// reach it.
//
// Seam: Qt Core and Network (the link, the commands' local socket).

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/link/discovery.hpp"
#include "seventhslide/link/library_sync.hpp"
#include "seventhslide/link/link.hpp"
#include "seventhslide/link/reach.hpp"
#include "seventhslide/link/sync_node.hpp"
#include "seventhslide/server/command_line.hpp"
#include "seventhslide/sync/identity.hpp"

#include <QDateTime>
#include <QHostAddress>
#include <QLockFile>
#include <QObject>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::server {

class Control;

struct ServerOptions {
    std::filesystem::path data;
    QHostAddress listen = QHostAddress::Any;
    /// 0 for any free port. None: as its settings say, or ``link::k_usual_port``.
    std::optional<std::uint16_t> port;
    /// How computers reach it: "host" or "host:port". None: as its settings
    /// say, or this computer's addresses on its networks.
    std::vector<std::string> addresses;
    /// What it is called. None: as its settings say, or this computer's name.
    std::string name;
    /// How a new admin key is locked (tests lock quicker).
    crypto::BoxParams box_params = crypto::k_box_params;
    /// How many libraries sync at once (``link::SyncTurns``): a small
    /// computer holding many works through them a few at a time.
    int syncs_at_once = 2;
    /// Whether to listen for the commands.
    bool control = true;
    /// Whether it may announce itself on the church network and listen for
    /// the others (off in a container without the host's network), as its
    /// settings also say.
    bool discovery = true;
    link::Discovery::Options discovery_options;
    /// How it keeps connected to the libraries' other computers.
    link::Reach::Options reach_options;
};

/// The library ``text`` names among ``uids``: the whole uid or enough of its
/// start; ``error`` when it names none or several.
struct LibraryChoice {
    std::string uid;
    std::string error;
};
[[nodiscard]] LibraryChoice choose_library(const std::vector<std::string>& uids,
                                           std::string_view text);

using link::disk_nearly_full;

class Server final : public QObject {
    Q_OBJECT
public:
    /// Throws ``std::runtime_error``, in words for the operator, when another
    /// server runs with the same data folder, the folder or the key cannot be
    /// had, or the port is taken.
    explicit Server(ServerOptions options, QObject* parent = nullptr);
    ~Server() override;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] const sync::Identity& identity() const noexcept { return node_->identity(); }
    [[nodiscard]] link::SyncNode& node() noexcept { return *node_; }
    [[nodiscard]] link::Link& link() noexcept { return node_->link(); }
    [[nodiscard]] link::Reach& reach() noexcept { return node_->reach(); }
    /// Null when it does not announce itself.
    [[nodiscard]] link::Discovery* discovery() noexcept { return node_->discovery(); }
    /// The port it listens for announcements on; 0 when it does not.
    [[nodiscard]] quint16 discovery_port() const
    {
        return node_->discovery() != nullptr ? node_->discovery()->port() : 0;
    }
    [[nodiscard]] quint16 port() const { return node_->port(); }
    /// How computers reach it, "host:port" each.
    [[nodiscard]] std::vector<std::string> addresses() const;

    /// Join the library the invite ``code`` is for; ``done`` gets the reply:
    /// its ``library``, or why not. The computers the invite names are tried
    /// until one is reached.
    void join(const std::string& code, std::function<void(nlohmann::json)> done);
    /// Sync every library now.
    void sync_now();

    /// Everything ``seventhslide --server status`` shows.
    [[nodiscard]] nlohmann::json status() const;

    /// Its sync settings (the reply in ``settings_edit.hpp``).
    [[nodiscard]] nlohmann::json settings() const;
    /// ``changes`` made to them and saved, and taken up now where they can be and
    /// no flag set them for this run. The reply as ``settings``, or why not.
    [[nodiscard]] nlohmann::json set_settings(const nlohmann::json& changes);

    [[nodiscard]] std::vector<std::string> library_uids() const;
    [[nodiscard]] link::LibrarySync* library(const std::string& uid) const;

    /// Admin work on ``uid`` (``admin_work``); ``done`` gets the reply. An
    /// invite with ``joining_at`` addresses is reached out to there.
    void admin(const std::string& uid, nlohmann::json request,
               std::function<void(nlohmann::json)> done);
    /// Where this server reaches ``who`` of ``uid``: a computer, before the
    /// addresses it gave (``Reach::set_typed_addresses``), or the computer an
    /// invite waiting is for, by the invite's id or the start of one
    /// (``Reach::set_invite_addresses``). Said to be ``addresses`` when given,
    /// none to go by its own again (or, for an invite, to wait for it). The
    /// reply says who (``name``, or ``invite``), and where (``addresses``).
    [[nodiscard]] nlohmann::json reach_at(const std::string& uid, const std::string& who,
                                          const std::optional<std::vector<std::string>>& addresses);
    /// Stop holding ``uid``: off the link, its folder moved to ``left/``.
    /// The reply says where, and whether the library still lists the server.
    [[nodiscard]] nlohmann::json leave(const std::string& uid);

    /// Whether anything is on its way: a peer's transfer, a sync, admin work.
    [[nodiscard]] bool busy() const;

private:
    /// Sync ``uid``, unless it is synced already.
    void hold(const std::string& uid);
    void load_registry();
    void save_registry() const;

    ServerOptions options_;
    QLockFile lock_;
    QDateTime started_;
    std::unique_ptr<link::SyncNode> node_;
    /// The libraries it holds, and since when.
    std::map<std::string, std::string> held_;
    std::unique_ptr<Control> control_;
};

/// What Qt lacks for a server to do its work, in words naming the plugins and where
/// they were looked for; empty when ``sqlite`` (its driver) and ``tls`` both are there.
/// A server without them could join a library and fetch it all, and open none of it.
[[nodiscard]] std::string lacking_for_server(bool sqlite, bool tls, const std::vector<std::string>& plugin_folders);

}  // namespace seventhslide::server
