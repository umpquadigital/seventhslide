#include "seventhslide/link/discovery.hpp"

#include "seventhslide/sync/sync_store.hpp"

#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QNetworkInterface>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace seventhslide::link {
namespace {

Q_LOGGING_CATEGORY(lc_discovery, "seventhslide.discovery", QtWarningMsg)

using nlohmann::json;

/// What is in every announcement, so nothing else on the port is taken for
/// one.
constexpr const char* k_kind = "seventhslide-announce";
constexpr int k_version = 1;
/// No announcement is longer.
constexpr qsizetype k_longest = 512;
/// A computer not heard for this long is answered when it is heard again.
constexpr qint64 k_answer_after_ms = 3 * k_announce_interval_ms;
/// Answers go at most this often.
constexpr qint64 k_answer_gap_ms = 2000;

}  // namespace

QByteArray encode_announcement(const Announcement& a)
{
    return QByteArray::fromStdString(
        json{{"kind", k_kind}, {"version", k_version}, {"fingerprint", a.fingerprint}, {"port", a.port}}
            .dump());
}

std::optional<Announcement> read_announcement(const QByteArray& datagram)
{
    if (datagram.size() > k_longest) {
        return std::nullopt;
    }
    const auto j = json::parse(datagram.constData(), datagram.constData() + datagram.size(), nullptr,
                               /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return std::nullopt;
    }
    const auto kind = j.find("kind");
    const auto version = j.find("version");
    const auto fingerprint = j.find("fingerprint");
    const auto port = j.find("port");
    if (kind == j.end() || *kind != k_kind || version == j.end() || !version->is_number_integer()
        || version->get<std::int64_t>() < 1 || fingerprint == j.end() || !fingerprint->is_string()
        || !sync::is_fingerprint(fingerprint->get<std::string>()) || port == j.end()
        || !port->is_number_integer() || port->get<std::int64_t>() < 1
        || port->get<std::int64_t>() > 65535) {
        return std::nullopt;
    }
    // A later version says at least this much, the same way.
    return Announcement{fingerprint->get<std::string>(), static_cast<quint16>(port->get<std::int64_t>())};
}

Discovery::Discovery(std::string fingerprint, quint16 link_port, Options options, QObject* parent)
    : QObject(parent), fingerprint_(std::move(fingerprint)), link_port_(link_port),
      options_(std::move(options))
{
    connect(&socket_, &QUdpSocket::readyRead, this, &Discovery::read);
    every_.setInterval(options_.interval_ms);
    connect(&every_, &QTimer::timeout, this, &Discovery::announce);
    connect(&every_, &QTimer::timeout, this, [this] { forget_unheard(); });
    soon_.setSingleShot(true);
    soon_.setInterval(300);
    connect(&soon_, &QTimer::timeout, this, &Discovery::announce);
}

bool Discovery::start()
{
    const bool listening = socket_.bind(QHostAddress::AnyIPv4, options_.port,
                                        QAbstractSocket::ShareAddress
                                            | QAbstractSocket::ReuseAddressHint);
    if (!listening) {
        qCWarning(lc_discovery).noquote()
            << "Cannot hear other SeventhSlides on the network (UDP port" << options_.port
            << "):" << socket_.errorString();
    }
    every_.start();
    announce();
    return listening;
}

std::vector<std::pair<QHostAddress, quint16>> Discovery::targets() const
{
    if (!options_.targets.empty()) {
        return options_.targets;
    }
    std::vector<std::pair<QHostAddress, quint16>> out;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || !flags.testFlag(QNetworkInterface::CanBroadcast)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                out.emplace_back(entry.broadcast(), k_discovery_port);
            }
        }
    }
    if (out.empty()) {
        out.emplace_back(QHostAddress::Broadcast, k_discovery_port);
    }
    return out;
}

void Discovery::announce()
{
    const auto datagram = encode_announcement({fingerprint_, link_port_});
    for (const auto& [address, port] : targets()) {
        socket_.writeDatagram(datagram, address, port);
    }
}

void Discovery::read()
{
    while (socket_.hasPendingDatagrams()) {
        const auto datagram = socket_.receiveDatagram(k_longest + 1);
        const auto heard_one = read_announcement(datagram.data());
        if (!heard_one || heard_one->fingerprint == fingerprint_) {
            continue;  // not one, or this computer's own
        }
        // The address is where it came from: what an announcement says
        // about itself is only its key and its port.
        auto from = datagram.senderAddress();
        if (from.protocol() == QAbstractSocket::IPv6Protocol) {
            bool v4 = false;
            const QHostAddress mapped(from.toIPv4Address(&v4));
            if (v4) {
                from = mapped;
            }
        }
        if (!last_heard_.contains(heard_one->fingerprint)) {
            forget_unheard(1);
        }
        auto& last = last_heard_[heard_one->fingerprint];
        const bool new_here = !last.isValid() || last.elapsed() > k_answer_after_ms;
        last.start();
        if (new_here && (!last_answer_.isValid() || last_answer_.elapsed() > k_answer_gap_ms)) {
            // Just started, or back: let it hear this one at once.
            last_answer_.start();
            soon_.start();
        }
        qCDebug(lc_discovery).noquote() << "heard" << QString::fromStdString(heard_one->fingerprint.substr(0, 8))
                                        << "at" << from.toString() << heard_one->port;
        emit heard(heard_one->fingerprint, from, heard_one->port);
    }
}

void Discovery::forget_unheard(std::size_t room)
{
    // One not heard for this long is answered as one never heard.
    std::erase_if(last_heard_, [](const auto& entry) { return entry.second.elapsed() > k_answer_after_ms; });
    while (!last_heard_.empty() && last_heard_.size() + room > k_most_heard) {
        last_heard_.erase(std::ranges::max_element(last_heard_, {}, [](const auto& entry) {
            return entry.second.elapsed();
        }));
    }
}

}  // namespace seventhslide::link
