#include "sync_controller.hpp"

#include "font_controller.hpp"
#include "library_controller.hpp"
#include "library_registry_controller.hpp"
#include "media_controller.hpp"
#include "session_facade.hpp"
#include "entry_error.hpp"
#include "refusals.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/library/computer_key.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/library/machine_store.hpp>
#include <seventhslide/link/peer.hpp>
#include <seventhslide/platform/computer_name.hpp>
#include <seventhslide/sync/admin_key.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/read_file.hpp>

#include <QBuffer>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QImage>
#include <QLocale>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace seventhslide::app {
namespace {

Q_LOGGING_CATEGORY(lc_sync, "seventhslide.sync", QtInfoMsg)

using nlohmann::json;
namespace fs = std::filesystem;

/// The library ``root`` syncs, as its member list names it; none when it
/// is not a synced library.
std::optional<std::string> synced_uid(const fs::path& root)
{
    sync::FolderStore store(library::sync_root(root));
    for (const auto& machine : store.machines(sync::Numbered::Members)) {
        for (const auto n : store.sequences(sync::Numbered::Members, machine)) {
            try {
                const auto envelope =
                    sync::unseal(store.read(sync::Numbered::Members, machine, n), "A sync member list entry");
                const auto uid = envelope.body.value("library", std::string());
                if (sync::is_library_uid(uid)) {
                    return uid;
                }
            } catch (const std::exception&) {
                // the next one may say
            }
        }
    }
    return std::nullopt;
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

QStringList q_list(const std::vector<std::string>& strings)
{
    QStringList out;
    for (const auto& s : strings) {
        out.push_back(q(s));
    }
    return out;
}

std::vector<std::string> std_list(const QStringList& strings)
{
    std::vector<std::string> out;
    for (const auto& s : strings) {
        out.push_back(s.toStdString());
    }
    return out;
}

/// ``address`` as a person reads it: an IPv4 one as itself, not as the IPv6 a
/// listener taking both sees it as.
QString shown_address(const QHostAddress& address)
{
    bool v4 = false;
    const auto ipv4 = address.toIPv4Address(&v4);
    return v4 ? QHostAddress(ipv4).toString() : address.toString();
}

/// Why addresses typed for another computer will not do, in words; empty when they will.
QString addresses_problem(const link::AddressesFor& read)
{
    if (!read.not_an_address.empty()) {
        return SyncController::tr("“%1” is not an address.").arg(q(read.not_an_address));
    }
    if (!read.its_own.empty()) {
        return SyncController::tr("“%1” is one it gave itself, so it is reached there anyway.").arg(q(read.its_own));
    }
    if (read.too_many) {
        return SyncController::tr("A computer is given %n address(es) at most.", nullptr,
                                  static_cast<int>(sync::k_most_addresses));
    }
    return {};
}

/// The mark of a library joined here that has not all arrived: it is not
/// opened until it has (``SyncController::why_not_open``), after a restart
/// too. It is this computer's own, in the library's sync folder, which no
/// other computer reads.
fs::path arriving_mark(const fs::path& root)
{
    return library::sync_root(root) / "arriving";
}

/// Move library ``root``'s sync folder aside, kept beside it and not used.
std::error_code put_sync_aside(const fs::path& root)
{
    std::error_code ec;
    fs::rename(library::sync_root(root),
               root / ("sync-left-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString()),
               ec);
    return ec;
}

QVariantMap reply_map(const json& reply)
{
    QVariantMap out;
    out[QStringLiteral("ok")] = reply.value("ok", false);
    for (const auto& [from, to] : {std::pair{"error", "error"},
                                   {"code", "code"},
                                   {"expires", "expires"},
                                   {"recovery_code", "recoveryCode"},
                                   {"id", "id"},
                                   {"removed", "removed"}}) {
        if (const auto it = reply.find(from); it != reply.end() && it->is_string()) {
            out[QLatin1String(to)] = q(it->get<std::string>());
        }
    }
    if (const auto it = reply.find("heard"); it != reply.end() && it->is_boolean()) {
        out[QStringLiteral("heard")] = it->get<bool>();
    }
    return out;
}

/// ``bytes`` in gigabytes, for the operator: "48.2 GB".
QString gigabytes(std::int64_t bytes)
{
    return QLocale().toString(static_cast<double>(bytes) / 1e9, 'f', 1) + QStringLiteral(" GB");
}

json failure(const std::string& why)
{
    return {{"ok", false}, {"error", why}};
}

}  // namespace

/// A library synced here.
struct SyncController::Held {
    std::string id;
    /// The node's (``SyncNode::hold``).
    link::LibrarySync* sync = nullptr;
    /// This computer's id.
    std::string me;
    /// The library's folder.
    fs::path root;
    /// Joined here, and not all of it has arrived yet (``arriving_mark``).
    bool arriving = false;

    /// Its member list (``sync::read_members``).
    [[nodiscard]] sync::Members members() const { return sync::read_members(sync->store(), sync->uid()); }
    /// The invite this computer joins it by, while its list has not come.
    [[nodiscard]] std::optional<sync::SignedInvite> invite() const
    {
        return members().founded() ? std::nullopt : sync::own_invite(sync->store(), sync->uid(), me);
    }
};

SyncController::SyncController(SessionFacade& facade, Options options, QObject* parent)
    : QObject(parent), facade_(facade), options_(std::move(options)), admin_(new SyncAdmin(*this)),
      log_(new CollaborationLog(
          [this](std::string uid, std::string text, std::function<void(std::string)> done) {
              node_->post_message(uid, std::move(text), std::move(done));
          },
          options_.data_dir, this))
{
    const auto* machine = facade_.open_library().machine_store();
    if (machine == nullptr || machine->machine_id().empty()) {
        throw std::runtime_error("This computer has no id yet: its settings could not be read.");
    }
    settings_ = link::SyncSettings::load(options_.data_dir);
    link::SyncNode::Options node;
    node.data = options_.data_dir;
    node.network = options_.network;
    port_at_start_ = settings_.port.value_or(options_.port);
    node.port = port_at_start_;
    // Found on the church network wherever it listens; and when a port was
    // wanted for a forward on the router, it says so (``portProblem``).
    node.any_port_when_taken = true;
    node.addresses = settings_.addresses;
    node.discovery = options_.announce && settings_.discovery;
    node.sync_automatically = settings_.sync_automatically;
    node.upload_cap_kbit = settings_.upload_cap_kbit;
    node.share_address = settings_.share_address;
    node.picture = settings_.picture;
    node.sync_now_quiet_ms = options_.sync_now_quiet_ms;
    node.sync_now_reach_ms = options_.sync_now_reach_ms;
    node.watch_edits = true;  // the window's
    node.box_params = options_.box_params;
    node_ = std::make_unique<link::SyncNode>(
        sync::Identity{machine->machine_id(), computerName().toStdString(),
                       library::load_computer_key(options_.data_dir)},
        node);
    connect(node_.get(), &link::SyncNode::changed, this, &SyncController::changed);
    connect(node_.get(), &link::SyncNode::log_changed, this, [this](const std::string& uid) {
        if (const auto* held = open_held(); held != nullptr && held->sync->uid() == uid) {
            log_->refresh();
        }
    });
    connect(facade_.libraries(), &LibraryRegistryController::librariesChanged, this,
            &SyncController::follow_registry);
    facade_.set_read_only_check([this](const std::string& id) { return views_only(id); });
    facade_.libraries()->set_open_check([this](const std::string& id) {
        const auto why = why_not_open(id);
        // Let open while arriving: all of it is here, so it no longer is.
        if (auto* held = held_for_id(id); why.isEmpty() && held != nullptr && held->arriving) {
            mark_arriving(*held, false);
        }
        return why;
    });
    // Off the list, a library is let go of (``follow_registry``).
    facade_.libraries()->set_delete_wait([this] { node_->link().wait_for_writes(); });
    facade_.media()->set_import_note(
        [this](const QStringList& titles, std::int64_t bytes) { return import_note(titles, bytes); });
    facade_.fonts()->set_import_note(
        [this](const QStringList& titles, std::int64_t bytes) { return import_note(titles, bytes); });
    // A library joined here has the fonts and Bibles the others have, no more:
    // the shipped ones are copied in only where it was made.
    const auto made_here = [this] {
        const auto* held = open_held();
        return held == nullptr || !sync::joined_here(held->sync->store(), held->sync->uid(), held->me);
    };
    facade_.fonts()->set_may_take_shipped(made_here);
    facade_.library()->set_may_take_shipped(made_here);
    // A file the open library was waiting for shows wherever it is used.
    arrived_soon_.setSingleShot(true);
    arrived_soon_.setInterval(options_.arrived_ms);
    connect(&node_->link(), &link::Link::media_stored, this,
            [this](const std::string& uid, const std::string& path) {
                const auto* held = open_held();
                if (held == nullptr || held->sync->uid() != uid) {
                    return;
                }
                arrived_.push_back(path);
                if (!arrived_soon_.isActive()) {
                    arrived_soon_.start();
                }
            });
    connect(&arrived_soon_, &QTimer::timeout, this,
            [this] {
                facade_.media_arrived(std::exchange(arrived_, {}));
                follow_arrivals();
            });
    connect(&facade_, &SessionFacade::readyChanged, this, &SyncController::follow_open_library);
    connect(&facade_, &SessionFacade::sessionChanged, this, &SyncController::follow_live);
    live_grace_.setSingleShot(true);
    connect(&live_grace_, &QTimer::timeout, this, &SyncController::let_live_go);

    // A transfer that starts or ends says nothing here on its own (one going
    // out, above all): look every second, and tell when what is moving has
    // changed, so the bar's sync spins while it is and the words keep up.
    activity_.setInterval(1000);
    connect(&activity_, &QTimer::timeout, this, [this] {
        std::map<std::string, std::pair<std::size_t, std::size_t>> moving;
        for (const auto& [id, held] : held_) {
            if (active(*held)) {
                const auto& uid = held->sync->uid();
                moving[id] = {node_->link().media_coming(uid).size(), node_->link().media_going(uid).size()};
            }
        }
        if (moving != moving_) {
            moving_ = std::move(moving);
            changed();
        }
        follow_arrivals();
    });
    activity_.start();

    state_soon_.setSingleShot(true);
    state_soon_.setInterval(250);
    connect(&state_soon_, &QTimer::timeout, this, [this] {
        // A library joined here may be opened once all of it is here
        // (``all_here``), and one joined since this computer started opens
        // then, unless something is live.
        for (auto& entry : held_) {
            if (entry.second->arriving && all_here(*entry.second)) {
                mark_arriving(*entry.second, false);
            }
        }
        for (auto it = to_open_.begin(); it != to_open_.end();) {
            auto* held = held_for_id(*it);
            if (held == nullptr) {
                it = to_open_.erase(it);
                continue;
            }
            if (!held->arriving && held->sync->report().arrived && !facade_.anything_live()) {
                const auto id = *it;
                it = to_open_.erase(it);
                (void)facade_.libraries()->switchLibrary(q(id));
            } else {
                ++it;
            }
        }
        follow_log();
        emit stateChanged();
    });

    follow_registry();
}

SyncController::~SyncController()
{
    facade_.set_read_only_check({});
    facade_.libraries()->set_open_check({});
    facade_.libraries()->set_delete_wait({});
    facade_.media()->set_import_note({});
    facade_.fonts()->set_import_note({});
    held_.clear();
    node_.reset();  // every library off the link, its thread waited for
}

QString SyncController::computerName() const
{
    return settings_.computer_name.empty() ? q(platform::computer_name()) : q(settings_.computer_name);
}

bool SyncController::pauseDuringServices() const
{
    return facade_.open_library().machine_settings().sync_pause_live;
}

quint16 SyncController::port() const
{
    return node_->port();
}

QString SyncController::setComputerName(const QString& name)
{
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return tr("Give this computer a name the others will know it by.");
    }
    auto settings = settings_;
    settings.computer_name = trimmed.toStdString();
    if (auto why = keep_settings(std::move(settings)); !why.isEmpty()) {
        return why;
    }
    node_->set_computer_name(settings_.computer_name);
    return {};
}

QString SyncController::setComputerPicture(const QString& glyph, const QString& colour)
{
    sync::Picture picture;
    if (!glyph.isEmpty()) {
        picture = sync::picture_from_json({{"glyph", glyph.toStdString()}, {"colour", colour.toStdString()}});
        if (picture.empty()) {
            return tr("That picture cannot be used.");
        }
    }
    auto settings = settings_;
    settings.picture = std::move(picture);
    if (auto why = keep_settings(std::move(settings)); !why.isEmpty()) {
        return why;
    }
    node_->set_computer_picture(settings_.picture);
    return {};
}

QString SyncController::setComputerPictureFile(const QString& url)
{
    const QUrl at(url);
    QImage image(at.isLocalFile() ? at.toLocalFile() : url);
    if (image.isNull()) {
        return tr("That file is not a picture SeventhSlide can read.");
    }
    // Its middle, square, cut to a circle: small enough for every computer's
    // list to carry.
    const int side = std::min(image.width(), image.height());
    image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    std::string base64;
    for (const int size : {128, 96, 72, 56}) {
        QImage round(size, size, QImage::Format_ARGB32_Premultiplied);
        round.fill(Qt::transparent);
        {
            QPainter painter(&round);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            QPainterPath circle;
            circle.addEllipse(0, 0, size, size);
            painter.setClipPath(circle);
            painter.drawImage(QRect(0, 0, size, size), image);
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        round.save(&buffer, "PNG");
        base64 = png.toBase64().toStdString();
        if (base64.size() <= sync::k_largest_picture) {
            break;
        }
    }
    if (base64.size() > sync::k_largest_picture) {
        return tr("That picture is too detailed to share. Try a simpler one.");
    }
    auto settings = settings_;
    settings.picture = sync::Picture{{}, {}, std::move(base64)};
    if (auto why = keep_settings(std::move(settings)); !why.isEmpty()) {
        return why;
    }
    node_->set_computer_picture(settings_.picture);
    return {};
}

QString SyncController::keep_settings(link::SyncSettings settings)
{
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    emit settingsChanged();
    return {};
}

void SyncController::follow_log()
{
    const auto* held = open_held();
    if (held == nullptr) {
        log_->follow({}, nullptr, {});
        return;
    }
    log_->follow(held->sync->uid(), held->sync->shared_store(), node_->identity().machine);
    log_->set_members(held->members());
}

QStringList SyncController::reachAddresses() const
{
    QStringList out;
    for (const auto& a : settings_.addresses) {
        out.push_back(q(a));
    }
    return out;
}

bool SyncController::portPending() const noexcept
{
    return settings_.port.value_or(options_.port) != port_at_start_;
}

QString SyncController::portProblem() const
{
    if (!options_.network || portPending()) {
        return {};
    }
    if (!node_->listening()) {
        return tr("This computer can’t take connections: no port could be had. It can still reach "
                  "the others.");
    }
    // Only where a port was counted on: anywhere does on the church network.
    const bool counted_on = settings_.port.has_value() || !settings_.addresses.empty();
    if (counted_on && port_at_start_ != 0 && node_->port() != port_at_start_) {
        return tr("Port %1 is taken by another program, so this computer is on port %2 for now. "
                  "Computers outside can’t reach it until %1 is free.")
            .arg(port_at_start_)
            .arg(node_->port());
    }
    return {};
}

QString SyncController::setReachAddresses(const QString& addresses)
{
    auto typed = link::read_typed_addresses(addresses.toStdString());
    if (!typed.not_an_address.empty()) {
        return tr("“%1” is not an address.").arg(QString::fromStdString(typed.not_an_address));
    }
    if (typed.too_many) {
        return tr("A computer gives %n address(es) at most.", nullptr, static_cast<int>(sync::k_most_addresses));
    }
    auto settings = settings_;
    settings.addresses = typed.addresses;
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    node_->set_addresses(std::move(typed.addresses));
    emit settingsChanged();
    return {};
}

QString SyncController::setChosenPort(const QString& port)
{
    const auto text = port.trimmed();
    auto settings = settings_;
    if (text.isEmpty()) {
        settings.port.reset();
    } else {
        bool ok = false;
        const auto value = text.toUInt(&ok);
        if (!ok || value == 0 || value > 65535) {
            return tr("A port is a whole number from 1 to 65535.");
        }
        settings.port = static_cast<std::uint16_t>(value);
    }
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    emit settingsChanged();
    return {};
}

QString SyncController::setUploadCap(const QString& megabits)
{
    const auto kbit = link::read_upload_cap(megabits.toStdString());
    if (!kbit) {
        return tr("An upload speed cap is in megabits a second, from 0.1 to 10000. Leave it empty for none.");
    }
    auto settings = settings_;
    settings.upload_cap_kbit = *kbit;
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    node_->set_upload_cap(*kbit);
    emit settingsChanged();
    return {};
}

QString SyncController::setShareAddress(bool share)
{
    auto settings = settings_;
    settings.share_address = share;
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    node_->set_share_address(share);
    emit settingsChanged();
    return {};
}

QString SyncController::setComputerAddresses(const QString& machine, const QStringList& addresses)
{
    const auto* held = open_held();
    if (held == nullptr) {
        return tr("The open library is not synced.");
    }
    const auto list = held->members();
    const auto* member = list.find(machine.toStdString());
    if (member == nullptr || member->removed || member->machine == node_->identity().machine) {
        return tr("That is not another computer of this library.");
    }
    const auto read = link::read_addresses_for(std_list(addresses), member->addresses);
    if (const auto problem = addresses_problem(read); !problem.isEmpty()) {
        return problem;
    }
    node_->reach().set_typed_addresses(member->key.fingerprint(), read.addresses);
    changed();
    return {};
}

QString SyncController::setInviteAddresses(const QString& invite, const QStringList& addresses)
{
    const auto* held = open_held();
    if (held == nullptr) {
        return tr("The open library is not synced.");
    }
    const auto waiting = sync::waiting_invites(held->members());
    const auto made = std::ranges::find(waiting, invite.toStdString(), &sync::InviteMade::id);
    if (made == waiting.end()) {
        return tr("That invite is no longer waiting: it was used, withdrawn or has run out.");
    }
    const auto read = link::read_addresses_for(std_list(addresses));
    if (const auto problem = addresses_problem(read); !problem.isEmpty()) {
        return problem;
    }
    node_->reach().set_invite_addresses(held->sync->uid(), made->id, made->expires, read.addresses);
    changed();
    return {};
}

QVariantMap SyncController::readAddress(const QString& typed, const QStringList& itsOwn,
                                        const QStringList& added) const
{
    const auto read = link::read_addresses_for({typed.toStdString()}, std_list(itsOwn));
    if (const auto problem = addresses_problem(read); !problem.isEmpty()) {
        return {{QStringLiteral("error"), problem}};
    }
    if (read.addresses.empty()) {
        return {{QStringLiteral("error"), tr("Type a name or an address.")}};
    }
    const auto address = q(read.addresses.front());
    if (added.contains(address)) {
        return {{QStringLiteral("error"), tr("“%1” is added already.").arg(address)}};
    }
    if (std::cmp_greater_equal(added.size(), sync::k_most_addresses)) {
        return {{QStringLiteral("error"), addresses_problem({.too_many = true})}};
    }
    return {{QStringLiteral("address"), address}};
}

QString SyncController::setSyncAutomatically(bool automatically)
{
    auto settings = settings_;
    settings.sync_automatically = automatically;
    if (!settings.save(options_.data_dir)) {
        return tr("This computer’s settings could not be saved.");
    }
    settings_ = std::move(settings);
    node_->set_sync_automatically(automatically);
    emit settingsChanged();
    changed();
    return {};
}

QString SyncController::setPauseDuringServices(bool pause)
{
    auto settings = facade_.open_library().machine_settings();
    settings.sync_pause_live = pause;
    if (!facade_.open_library().save_machine_settings(settings)) {
        return tr("This computer’s settings could not be saved.");
    }
    emit settingsChanged();
    follow_live();
    return {};
}

QString SyncController::passphraseProblem(const QString& passphrase) const
{
    const auto problem = sync::passphrase_problem(passphrase.toStdString());
    return problem ? q(*problem) : QString();
}

QVariantMap SyncController::readInvite(const QString& code) const
{
    try {
        const auto invite = sync::read_invite(code.trimmed().toStdString()).invite;
        const auto name = invite.name.empty() ? tr("a library") : tr("“%1”").arg(q(invite.name));
        return {{QStringLiteral("text"), invite.role == sync::Role::Viewer
                                             ? tr("Join %1 as a viewer").arg(name)
                                             : tr("Join %1 as an editor").arg(name)}};
    } catch (const std::exception& ex) {
        // Read again at each keystroke of the code: said in place, and not
        // logged, as a code half pasted is not a failure.
        return {{QStringLiteral("error"), message_for_operator(ex)}};
    }
}

std::string SyncController::open_id() const
{
    const auto* registry = facade_.open_library().registry();
    if (registry == nullptr || !facade_.ready()) {
        return {};
    }
    return registry->active_id();
}

SyncController::Held* SyncController::held_for_id(const std::string& id) const
{
    const auto it = held_.find(id);
    return it == held_.end() ? nullptr : it->second.get();
}

SyncController::Held* SyncController::open_held() const
{
    const auto id = open_id();
    return id.empty() ? nullptr : held_for_id(id);
}

bool SyncController::all_here(const Held& held) const
{
    const auto report = held.sync->report();
    // Every file here applied, too: one received that waits on a computer this
    // one refuses leaves the library short of what the others have.
    return report.joined && report.arrived && report.problem.empty() && report.waiting_on.empty()
           && !held.sync->busy() && node_->link().has_all_of(held.sync->uid());
}

void SyncController::mark_arriving(Held& held, bool arriving)
{
    const auto mark = arriving_mark(held.root);
    if (arriving) {
        std::ofstream(mark) << "Joined here: not all of the library has arrived yet.\n";
    } else {
        std::error_code ec;
        fs::remove(mark, ec);
    }
    held.arriving = arriving;
}

void SyncController::follow_registry()
{
    const auto* registry = facade_.open_library().registry();
    if (registry == nullptr) {
        return;
    }
    std::set<std::string> synced;
    for (const auto& entry : registry->entries()) {
        std::error_code ec;
        if (!fs::is_directory(entry.root, ec)) {
            continue;
        }
        if (held_.contains(entry.id)) {
            synced.insert(entry.id);
            continue;
        }
        const auto uid = synced_uid(entry.root);
        if (!uid) {
            continue;
        }
        const bool copy = std::ranges::any_of(held_, [&](const auto& h) { return h.second->sync->uid() == *uid; });
        if (copy) {
            // A folder copied by hand: the same library twice would sync as one.
            qCWarning(lc_sync).noquote() << "Not syncing" << q(entry.name)
                                         << ": another library here is the same synced library";
            continue;
        }
        synced.insert(entry.id);
        hold(entry.id, entry.root, *uid);
    }
    for (auto it = held_.begin(); it != held_.end();) {
        if (!synced.contains(it->first)) {
            const auto id = it->first;
            ++it;
            let_go(id);
        } else {
            ++it;
        }
    }
    follow_open_library();
    changed();
}

SyncController::Held& SyncController::hold(const std::string& id, const fs::path& root,
                                           const std::string& uid)
{
    auto held = std::make_unique<Held>();
    held->id = id;
    held->me = node_->identity().machine;
    held->root = root;
    std::error_code ec;
    held->arriving = fs::exists(arriving_mark(root), ec);
    held->sync = &node_->hold(uid, root);
    auto* sync = held->sync;
    connect(sync, &link::LibrarySync::applied, this, [this, id](const sync::Changed& changed) {
        if (id == open_id()) {
            facade_.reload_after_sync(changed);
        }
    });
    connect(sync, &link::LibrarySync::reportChanged, this, [this, id] {
        if (id == open_id()) {
            follow_open_library();
            facade_.fonts()->files_may_have_arrived();
        }
    });
    auto& out = *held;
    held_[id] = std::move(held);
    return out;
}

void SyncController::let_go(const std::string& id)
{
    const auto it = held_.find(id);
    if (it == held_.end()) {
        return;
    }
    const auto uid = it->second->sync->uid();
    held_.erase(it);
    node_->let_go(uid);
    to_open_.erase(id);
}

bool SyncController::views_only(const std::string& id) const
{
    const auto* held = held_for_id(id);
    if (held == nullptr) {
        return false;
    }
    const auto list = held->members();
    const auto* me = list.find(node_->identity().machine);
    return me != nullptr && !me->removed && me->role == sync::Role::Viewer;
}

void SyncController::follow_open_library()
{
    facade_.set_read_only(views_only(open_id()));
    follow_live();
    follow_arrivals();
    changed();
}

void SyncController::follow_arrivals()
{
    // As the bar counts them (``state_of``): a file is on its way while a
    // computer connected may have it.
    std::map<std::string, FileArrival> arrivals;
    std::set<std::string> elsewhere;
    if (const auto* held = open_held()) {
        const auto& uid = held->sync->uid();
        const auto& net = node_->link();
        const bool connected = std::ranges::any_of(net.peers(), [&](const link::Peer* p) {
            return p->ready() && p->libraries().contains(uid);
        });
        for (const auto& [path, arriving] : net.media_arriving(uid)) {
            arrivals[path] = {FileArrival::State::Downloading, arriving.bytes, arriving.size};
        }
        for (const auto& path : net.media_coming(uid)) {
            arrivals.try_emplace(path, FileArrival{});
        }
        for (const auto& d : held->sync->missing_media()) {
            const auto [it, added] = arrivals.try_emplace(d.path, FileArrival{});
            if (it->second.size == 0) {
                it->second.size = d.size;
            }
            if (added) {
                if (auto via = net.coming_through(uid, d.path)) {
                    it->second.state = FileArrival::State::Elsewhere;
                    it->second.via = std::move(*via);
                    elsewhere.insert(d.path);
                } else {
                    it->second.state = net.nobody_has(uid, d.path) ? FileArrival::State::Nowhere
                                       : connected                  ? FileArrival::State::Queued
                                                                    : FileArrival::State::Waiting;
                }
            }
        }
    }
    facade_.media()->set_arrivals(std::move(arrivals));
    facade_.media()->set_sync_paused(live_held_);
    // The bar's words follow what the others say is on its way to them.
    if (elsewhere != elsewhere_) {
        elsewhere_ = std::move(elsewhere);
        changed();
    }
}

void SyncController::follow_live()
{
    // While something is live, nothing syncs: no library takes in or sends
    // changes, and nothing goes either way over the network, so the show has
    // this computer's disk and network to itself. It goes on a moment after
    // the last item closes: the next is often about to start.
    if (!pauseDuringServices()) {
        let_live_go();
        return;
    }
    if (facade_.anything_live()) {
        live_grace_.stop();
        if (!live_held_) {
            live_held_ = true;
            node_->set_paused(true);
            follow_arrivals();
            changed();
        }
    } else if (live_held_ && !live_grace_.isActive()) {
        live_grace_.start(options_.live_grace_ms);
    }
}

void SyncController::let_live_go()
{
    live_grace_.stop();
    if (live_held_) {
        live_held_ = false;
        node_->set_paused(false);
        follow_arrivals();
        changed();
    }
}

void SyncController::changed()
{
    states_.reset();
    open_.reset();
    if (!state_soon_.isActive()) {
        state_soon_.start();
    }
}

QVariantMap SyncController::state_of(const Held& held) const
{
    const auto report = held.sync->report();
    const auto& list = held.members();
    const auto* me = list.find(node_->identity().machine);
    const auto& uid = held.sync->uid();
    const bool connected = std::ranges::any_of(node_->link().peers(), [&](const link::Peer* p) {
        return p->ready() && p->libraries().contains(uid);
    });
    // What is missing here comes only from a computer that has it: what every
    // connected computer said it has not got is not downloading, and never
    // will be (a file lost on all of them).
    const auto missing = held.sync->missing_media();
    const auto nowhere = static_cast<int>(std::ranges::count_if(
        missing, [&](const db::MediaDigest& d) { return node_->link().nobody_has(uid, d.path); }));
    // A file on its way is often on no list of what is missing: the change file
    // that names it waits here until it has come.
    auto on_its_way = node_->link().media_coming(uid);
    // Not downloading either: what a computer connected is itself still
    // getting, and has none of to give yet.
    int elsewhere = 0;
    for (const auto& d : missing) {
        if (node_->link().nobody_has(uid, d.path)) {
            continue;
        }
        if (!on_its_way.contains(d.path) && node_->link().coming_through(uid, d.path)) {
            ++elsewhere;
        } else {
            on_its_way.insert(d.path);
        }
    }
    const auto coming = static_cast<int>(on_its_way.size());
    const auto going = static_cast<int>(node_->link().media_going(uid).size());
    // Said whatever the state; the state itself once nothing passing outranks it.
    QString too_large;
    if (!report.too_large.empty()) {
        const auto& first = report.too_large.front();
        const auto others = static_cast<int>(report.too_large.size()) - 1;
        const auto kept = report.kept_with_too_large;
        const auto name = first.title.empty() ? tr("A record") : tr("“%1”").arg(q(first.title));
        const auto what = others == 0 ? tr("%1 is too large to sync").arg(name)
                                      : tr("%1 and %n other(s) are too large to sync", nullptr, others).arg(name);
        const auto stays = others == 0
                               ? (kept == 0 ? tr("it stays on this computer")
                                            : tr("it and %n record(s) using it stay on this computer", nullptr, kept))
                               : (kept == 0 ? tr("they stay on this computer")
                                            : tr("they and %n record(s) using them stay on this computer", nullptr,
                                                 kept));
        const auto fix = others == 0 ? tr("Shorten it or delete it.") : tr("Shorten them or delete them.");
        too_large = tr("%1, so %2. %3").arg(what, stays, fix);
    }
    // Likewise why no snapshot is written: syncing goes on meanwhile.
    QString no_snapshot;
    if (!report.snapshot_problem.empty()) {
        auto why = q(report.snapshot_problem);
        if (why.endsWith(u'.')) {
            why.chop(1);
        }
        no_snapshot =
            tr("%1. Old change files are kept until a snapshot can be written. Sync goes on.").arg(why);
    }
    QString state;
    QString text;
    const auto& invite = held.invite();
    if (me != nullptr && me->removed) {
        state = QStringLiteral("removed");
        text = tr("This computer was removed from the library.");
    } else if (!report.problem.empty()) {
        state = QStringLiteral("problem");
        text = q(report.problem);
    } else if (!report.opened) {
        state = QStringLiteral("starting");
        text = tr("Starting…");
    } else if (invite && sync::invite_expired(invite->invite)) {
        state = QStringLiteral("problem");
        text = tr("The invite ran out before the library could be reached. Remove this library "
                  "here, and ask for a new invite.");
    } else if (!report.arrived && node_->waiting_for_sync_now()) {
        state = QStringLiteral("paused");
        text = tr("Waiting for Sync Now");
    } else if (!report.arrived) {
        state = QStringLiteral("joining");
        text = connected ? tr("Getting the library…")
                         : tr("Trying to reach the computer the invite came from");
    } else if (!report.media.over.empty()) {
        state = QStringLiteral("full");
        const auto& over = report.media.over;
        const auto others = static_cast<int>(over.size()) - 1;
        text = others == 0
                   ? tr("Over its size limit of %1: “%2” isn't syncing")
                         .arg(gigabytes(report.media.limit), q(over.front().title))
                   : tr("Over its size limit of %1: “%2” and %n other file(s) aren't syncing", nullptr, others)
                         .arg(gigabytes(report.media.limit), q(over.front().title));
    } else if (!report.waiting_on.empty()) {
        state = QStringLiteral("blocked");
        QStringList names;
        int unnamed = 0;
        for (const auto& computer : report.waiting_on) {
            if (computer.name.empty()) {
                ++unnamed;
            } else {
                names.push_back(tr("“%1”").arg(q(computer.name)));
            }
        }
        if (unnamed > 0) {
            names.push_back(unnamed == 1 ? tr("another computer")
                                         : tr("%n other computers", nullptr, unnamed));
        }
        text = tr("Waiting on changes from %1, which this computer can't accept. They come once "
                  "another computer with this version of SeventhSlide or later sends a copy of the "
                  "whole library.")
                   .arg(QLocale().createSeparatedList(names));
    } else if (held.sync->paused()) {
        state = QStringLiteral("paused");
        text = node_->waiting_for_sync_now() ? tr("Waiting for Sync Now")
               : facade_.anything_live()     ? tr("Syncing waits until nothing is live")
                                             : tr("Syncing goes on in a moment");
    } else if (connected && coming > 0) {
        state = QStringLiteral("downloading");
        text = tr("Downloading %n file(s)", nullptr, coming);
    } else if (report.unsent) {
        state = QStringLiteral("sending");
        text = tr("Sending changes");
    } else if (going > 0) {
        state = QStringLiteral("sending");
        text = tr("Sending %n file(s)", nullptr, going);
    } else if (!too_large.isEmpty()) {
        state = QStringLiteral("problem");
        text = too_large;
    } else if (!no_snapshot.isEmpty()) {
        state = QStringLiteral("problem");
        text = no_snapshot;
    } else if (!connected) {
        state = QStringLiteral("waiting");
        text = tr("Waiting for the other computers");
    } else if (held.arriving && !all_here(held)) {
        state = QStringLiteral("joining");
        text = tr("Getting the library…");
    } else if (nowhere > 0) {
        state = QStringLiteral("missing");
        text = tr("%n file(s) missing here and on every computer connected", nullptr, nowhere);
    } else if (elsewhere > 0) {
        state = QStringLiteral("elsewhere");
        text = tr("Waiting for %n file(s) still on the way to another computer", nullptr, elsewhere);
    } else {
        state = QStringLiteral("uptodate");
        text = tr("Up to date");
    }
    const bool trouble = state == QStringLiteral("problem") || state == QStringLiteral("removed")
                         || state == QStringLiteral("full") || state == QStringLiteral("missing")
                         || state == QStringLiteral("blocked");
    return {{QStringLiteral("state"), state},
            {QStringLiteral("text"), text},
            {QStringLiteral("trouble"), trouble},
            {QStringLiteral("tooLarge"), too_large},
            {QStringLiteral("snapshotProblem"), no_snapshot},
            {QStringLiteral("viewOnly"), me != nullptr && me->role == sync::Role::Viewer},
            {QStringLiteral("active"), active(held)},
            {QStringLiteral("missing"), nowhere},
            {QStringLiteral("elsewhere"), elsewhere}};
}

bool SyncController::active(const Held& held) const
{
    return held.sync->busy() || node_->link().moving(held.sync->uid());
}

QVariantMap SyncController::libraryStates() const
{
    if (!states_) {
        QVariantMap out;
        for (const auto& [id, held] : held_) {
            out[q(id)] = state_of(*held);
        }
        states_ = std::move(out);
    }
    return *states_;
}

QVariantMap SyncController::openLibrary() const
{
    if (open_) {
        return *open_;
    }
    QVariantMap out;
    const auto* registry = facade_.open_library().registry();
    const auto* entry = registry != nullptr && facade_.ready() ? registry->active() : nullptr;
    out[QStringLiteral("name")] = entry != nullptr ? q(entry->name) : QString();
    const auto* held = open_held();
    out[QStringLiteral("synced")] = held != nullptr;
    if (held == nullptr) {
        open_ = out;
        return out;
    }
    out.insert(libraryStates().value(q(held->id)).toMap());
    const auto& list = held->members();
    const auto& me = node_->identity().machine;
    if (const auto* self = list.find(me)) {
        out[QStringLiteral("role")] = q(std::string(sync::role_name(self->role)));
    }
    out[QStringLiteral("sizeLimit")] = static_cast<double>(list.size_limit);
    out[QStringLiteral("sizeUsed")] = static_cast<double>(held->sync->report().media.used);
    QVariantList computers;
    for (const auto& [machine, m] : list.members) {
        const bool on = std::ranges::any_of(node_->link().peers(), [&](const link::Peer* p) {
            return p->ready() && p->key() == m.key && p->libraries().contains(held->sync->uid());
        });
        const auto heard = node_->last_heard(m.key);
        const auto fingerprint = m.key.fingerprint();
        const auto* peer = on ? node_->link().peer_with(m.key) : nullptr;
        const auto reached = node_->reach().remembered(fingerprint);
        computers.push_back(QVariantMap{
            {QStringLiteral("key"), q(machine)},
            {QStringLiteral("machine"), q(machine)},
            {QStringLiteral("name"), q(m.name)},
            {QStringLiteral("role"), q(std::string(sync::role_name(m.role)))},
            {QStringLiteral("holdsAdminKey"), m.role == sync::Role::Editor || m.holds_admin_key},
            {QStringLiteral("removed"), m.removed},
            {QStringLiteral("connected"), on},
            {QStringLiteral("thisComputer"), machine == me},
            {QStringLiteral("lastHeard"), q(heard)},
            {QStringLiteral("addresses"), q_list(m.addresses)},
            {QStringLiteral("typedAddresses"), q_list(node_->typed_addresses(m))},
            {QStringLiteral("connectedAt"), peer != nullptr ? shown_address(peer->address()) : QString()},
            {QStringLiteral("lastReachedAt"), reached.empty() ? QString() : q(reached.front())},
            {QStringLiteral("picture"), picture_map(m.picture)}});
    }
    // Then the invites waiting for their computers, each a row of its own.
    for (const auto& made : sync::waiting_invites(list)) {
        const bool viewer = made.role == sync::Role::Viewer;
        const auto* by = list.find(made.made_by);
        computers.push_back(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("invite:") + q(made.id)},
            {QStringLiteral("invite"), q(made.id)},
            {QStringLiteral("name"), viewer ? tr("Invited viewer") : tr("Invited editor")},
            {QStringLiteral("role"), q(std::string(sync::role_name(made.role)))},
            {QStringLiteral("expires"), q(made.expires)},
            {QStringLiteral("madeBy"), by != nullptr ? q(by->name) : q(made.made_by)},
            {QStringLiteral("holdsAdminKey"), false},
            {QStringLiteral("addresses"), QStringList()},
            {QStringLiteral("typedAddresses"), q_list(node_->reach().invite_addresses(held->sync->uid(), made.id))}});
    }
    out[QStringLiteral("computers")] = computers;
    open_ = out;
    return out;
}

QString SyncController::import_note(const QStringList& titles, std::int64_t bytes) const
{
    const auto* held = open_held();
    if (held == nullptr || titles.isEmpty()) {
        return {};
    }
    const auto media = held->sync->report().media;
    if (media.limit <= 0 || media.used + bytes <= media.limit) {
        return {};
    }
    const auto what = titles.size() == 1 ? tr("“%1” is").arg(titles.front())
                                         : tr("These %n files are", nullptr, static_cast<int>(titles.size()));
    return tr("This library is at %1 of its %2 size limit. %3 imported, but would go over it, so it "
              "stays on this computer and won't sync to the others until there is room.")
        .arg(gigabytes(media.used), gigabytes(media.limit), what);
}

QString SyncController::why_not_open(const std::string& id) const
{
    // A library joined here is opened once all of it is here: opened part
    // way, what has not come yet (its outputs, a profile, its settings) is
    // missing from the window, and what the window makes of the gap would
    // go to every computer.
    const auto* held = held_for_id(id);
    if (held == nullptr) {
        return {};
    }
    const auto report = held->sync->report();
    if (report.arrived && (!held->arriving || all_here(*held))) {
        return {};
    }
    if (!report.opened && report.problem.empty()) {
        return tr("Sync is still opening this library. Try again in a moment.");
    }
    return tr("This library is still on its way from the other computers. It can be opened once it "
              "has arrived: its line here says how it is getting on.");
}

link::LibrarySync* SyncController::open_sync() const
{
    const auto* held = open_held();
    return held != nullptr ? held->sync : nullptr;
}

std::string SyncController::open_name() const
{
    const auto* registry = facade_.open_library().registry();
    const auto* entry = registry != nullptr ? registry->active() : nullptr;
    return entry != nullptr ? entry->name : std::string();
}

void SyncController::began()
{
    ++working_;
    emit workingChanged();
}

void SyncController::done(const QString& action, const json& reply)
{
    --working_;
    emit workingChanged();
    if (!reply.value("ok", false)) {
        qCInfo(lc_sync).noquote() << action << "not done:" << q(reply.value("error", std::string()));
    }
    emit finished(action, reply_map(reply));
    changed();
}

void SyncController::run(Held& held,
                         std::function<json(sync::SyncStore&, const sync::Identity&)> work,
                         std::function<void(json)> then)
{
    // ``then`` on a turn of its own: it may let go of the library, and so of
    // the LibrarySync whose reply this is.
    held.sync->run(std::move(work), [this, then = std::move(then)](json reply) {
        QTimer::singleShot(0, this, [then, reply = std::move(reply)] { then(reply); });
    });
}

QString SyncController::clear_earlier_sync(const fs::path& root)
{
    std::error_code ec;
    const auto folder = library::sync_root(root);
    const bool left_there = fs::exists(folder, ec) && !fs::is_empty(folder, ec);
    db::SyncBookkeeping book(*facade_.open_library().database());
    if (!left_there && !book.synced_before()) {
        return {};
    }
    if (left_there) {
        if (const auto moved = put_sync_aside(root)) {
            return tr("What this library synced with before could not be moved aside (%1).").arg(q(moved.message()));
        }
    }
    try {
        book.start_afresh();
    } catch (const std::exception& ex) {
        return tr("This library could not start syncing afresh. %1")
            .arg(report_entry_error(__func__, ex));
    }
    return {};
}

QString SyncController::putOnline(const QString& passphrase)
{
    if (!facade_.ready() || facade_.open_library().database() == nullptr) {
        return refusal::no_library();
    }
    if (open_held() != nullptr) {
        return tr("This library is synced already.");
    }
    if (const auto problem = sync::passphrase_problem(passphrase.toStdString())) {
        return q(*problem);
    }
    const auto* entry = facade_.open_library().registry()->active();
    auto uid = db::library_uid(*facade_.open_library().database());
    if (std::ranges::any_of(held_, [&](const auto& h) { return h.second->sync->uid() == uid; })) {
        return tr("Another library on this computer is this library, synced: open that one.");
    }
    if (auto problem = clear_earlier_sync(entry->root); !problem.isEmpty()) {
        return problem;
    }
    uid = db::library_uid(*facade_.open_library().database());
    const auto id = entry->id;
    auto& held = hold(id, entry->root, uid);
    began();
    run(held,
        [uid, pass = passphrase.toStdString(), params = options_.box_params](sync::SyncStore& store,
                                                                            const sync::Identity& me) {
            const auto founded = sync::MemberLog(store, me, uid).found(pass, params);
            return json{{"ok", true}, {"wrote", true}, {"recovery_code", founded.recovery_code}};
        },
        [this, id](json reply) {
            if (auto* h = held_for_id(id); h != nullptr) {
                if (reply.value("ok", false)) {
                    h->sync->share_everything();
                    if (!node_->sync_automatically()) {
                        node_->sync_now();
                    }
                } else {
                    let_go(id);
                }
            }
            follow_open_library();
            done(QStringLiteral("putOnline"), reply);
        });
    return {};
}

QString SyncController::joinLibrary(const QString& code)
{
    const auto text = code.trimmed().toStdString();
    sync::SignedInvite invite;
    try {
        invite = sync::read_invite(text);
    } catch (const std::exception& ex) {
        // A code mistyped or half pasted is the operator's to put right, not
        // a failure: said, and not logged, as readInvite says it.
        return message_for_operator(ex);
    }
    if (sync::invite_expired(invite.invite)) {
        return tr("This invite has expired. Ask for a new one.");
    }
    const auto uid = invite.invite.library;
    if (std::ranges::any_of(held_, [&](const auto& h) { return h.second->sync->uid() == uid; })) {
        return tr("This computer has that library already.");
    }
    auto* registry = facade_.open_library().registry();
    if (registry == nullptr) {
        return tr("This computer’s list of libraries could not be read.");
    }
    const auto created = registry->create(invite.invite.name.empty() ? "Synced library" : invite.invite.name);
    if (!created.ok()) {
        return q(created.error);
    }
    const auto* entry = registry->find(created.id);
    const auto id = created.id;
    auto& held = hold(id, entry->root, uid);
    facade_.libraries()->refresh();
    began();
    run(held,
        [uid, text](sync::SyncStore& store, const sync::Identity& me) {
            sync::MemberLog(store, me, uid).join(sync::read_invite(text));
            return json{{"ok", true}, {"wrote", true}};
        },
        [this, id](json reply) {
            if (reply.value("ok", false)) {
                // Reach goes through the members the invite names (a
                // server), or the computer that made it (on the church
                // network), from the join entry, and keeps trying until the
                // library comes.
                if (auto* joined = held_for_id(id)) {
                    mark_arriving(*joined, true);
                }
                to_open_.insert(id);
                reply["id"] = id;
                if (!node_->sync_automatically()) {
                    node_->sync_now();
                }
                if (options_.network) {
                    node_->reach().check();
                }
            } else {
                // Not joined: no library made for it is left behind.
                let_go(id);
                (void)facade_.libraries()->removeLibrary(q(id));
            }
            done(QStringLiteral("join"), reply);
        });
    return {};
}

QString SyncController::stopSyncing(const QString& id_text)
{
    const auto id = id_text.toStdString();
    auto* held = held_for_id(id);
    if (held == nullptr) {
        return tr("That library is not synced.");
    }
    const auto uid = held->sync->uid();
    began();
    run(*held,
        [uid](sync::SyncStore& store, const sync::Identity& me) {
            sync::MemberLog log(store, me, uid);
            const auto list = log.members();
            const auto* self = list.find(me.machine);
            const bool leaving = self != nullptr && !self->removed;
            if (leaving) {
                log.leave();
            }
            // What the others are to have: this computer's last entry (its
            // leave, now or from a stop that went no further); none when it
            // was never on the list.
            const auto mine = store.sequences(sync::Numbered::Members, me.machine);
            return json{{"ok", true},
                        {"wrote", leaving},
                        {"sequence", self != nullptr && !mine.empty() ? mine.back() : 0}};
        },
        [this, id](json reply) {
            if (!reply.value("ok", false)) {
                done(QStringLiteral("stopSyncing"), reply);  // still synced, as it was
                return;
            }
            // Not syncing automatically, nothing would carry the leave out.
            if (!node_->sync_automatically()) {
                node_->sync_now();
            }
            await_leave(id, reply.value("sequence", std::int64_t{0}));
        });
    return {};
}

void SyncController::await_leave(const std::string& id, std::int64_t sequence)
{
    const auto* held = held_for_id(id);
    if (sequence == 0 || held == nullptr) {
        finish_stop(id, true);
        return;
    }
    // Until a computer connected now says it has the leave, or a while has
    // passed: once the library is put aside, nothing here sends it.
    const auto uid = held->sync->uid();
    const auto taken = [this, uid, sequence] {
        return std::ranges::any_of(node_->link().peers(), [&](const link::Peer* p) {
            return p->ready()
                   && p->their_last(uid, sync::Numbered::Members, node_->identity().machine) >= sequence;
        });
    };
    auto* poll = new QTimer(this);
    poll->setInterval(200);
    connect(poll, &QTimer::timeout, this, [this, poll, id, taken, until = QDeadlineTimer(options_.leave_wait_ms)] {
        const bool heard = taken();
        if (heard || until.hasExpired()) {
            // Stopped now, so it cannot fire again before it is deleted.
            poll->stop();
            poll->deleteLater();
            finish_stop(id, heard);
        }
    });
    poll->start();
}

void SyncController::finish_stop(const std::string& id, bool heard)
{
    const auto* held = held_for_id(id);
    if (held == nullptr) {
        done(QStringLiteral("stopSyncing"), failure(tr("That library is no longer on this computer.").toStdString()));
        return;
    }
    const auto root = held->sync->root();
    let_go(id);
    // A plain library again: what it synced with is kept beside it, not used.
    const auto ec = put_sync_aside(root);
    if (ec) {
        follow_registry();  // synced still, with this computer off its list
        done(QStringLiteral("stopSyncing"),
             failure(tr("This computer left the library, but its sync folder could not be moved "
                        "aside (%1), so the library still counts as synced here. Try Stop "
                        "Syncing again.")
                         .arg(q(ec.message()))
                         .toStdString()));
        return;
    }
    try {
        // Its own library from now on, under a uid of its own.
        auto db = db::Database::open_file(library::database_file(root));
        db::SyncBookkeeping(db).leave_synced_library();
    } catch (const std::exception& ex) {
        follow_open_library();
        done(QStringLiteral("stopSyncing"),
             failure(tr("The library stopped syncing, but could not be made a library of its "
                        "own. %1 Do not put it online again until it is.")
                         .arg(report_entry_error(__func__, ex))
                         .toStdString()));
        return;
    }
    follow_open_library();
    done(QStringLiteral("stopSyncing"), json{{"ok", true}, {"heard", heard}});
}

void SyncController::syncNow()
{
    // Nothing is live: the moment after the last item need not be waited out.
    if (!facade_.anything_live()) {
        let_live_go();
    }
    node_->sync_now();
    changed();
}

}  // namespace seventhslide::app
