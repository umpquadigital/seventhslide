#pragma once

// Finding each other on the church network. Every SeventhSlide that syncs says
// now and then, to every computer on its networks, that it is there: its key's
// fingerprint and the port it takes connections on, nothing more (never a
// library's name). What it hears from the others it passes on (``heard``);
// whom to connect to is ``Reach``'s to decide, and only a member of a library
// this computer has is ever connected to.
//
// An announcement goes over UDP to each network's broadcast address, on port
// 8766 (the link's port, over UDP), at start, every half a minute, and soon
// after hearing a computer it had not heard lately, so one just started finds
// the others at once. Several SeventhSlides on one computer (a server beside
// the app) share the port and all hear. IPv4 only for now: church networks
// are.
//
// Seam: Qt Network (QUdpSocket, QNetworkInterface).

#include "seventhslide/link/most_heard.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::link {

inline constexpr quint16 k_discovery_port = 8766;
inline constexpr int k_announce_interval_ms = 30000;

struct Announcement {
    /// The key's fingerprint: 64 lower-case hex digits.
    std::string fingerprint;
    /// Where it takes connections.
    quint16 port = 0;

    friend bool operator==(const Announcement&, const Announcement&) = default;
};

[[nodiscard]] QByteArray encode_announcement(const Announcement& announcement);
/// ``std::nullopt`` for anything but an announcement.
[[nodiscard]] std::optional<Announcement> read_announcement(const QByteArray& datagram);

class Discovery final : public QObject {
    Q_OBJECT
public:
    struct Options {
        /// Where to listen; 0 for any free port (tests).
        quint16 port = k_discovery_port;
        /// Where to announce; each network's broadcast address when empty.
        std::vector<std::pair<QHostAddress, quint16>> targets;
        int interval_ms = k_announce_interval_ms;
    };

    /// ``fingerprint`` is this computer's; ``link_port`` where its link
    /// takes connections.
    Discovery(std::string fingerprint, quint16 link_port, Options options, QObject* parent = nullptr);
    Discovery(std::string fingerprint, quint16 link_port, QObject* parent = nullptr)
        : Discovery(std::move(fingerprint), link_port, Options{}, parent)
    {
    }

    /// Listen and announce. False when the port cannot be had: this computer
    /// then still announces itself, and hears no one.
    bool start();
    [[nodiscard]] quint16 port() const { return socket_.localPort(); }
    void set_targets(std::vector<std::pair<QHostAddress, quint16>> targets)
    {
        options_.targets = std::move(targets);
    }

    /// Say that this computer is here, now.
    void announce();
    /// How many computers heard lately are kept track of
    /// (``k_most_heard`` at most).
    [[nodiscard]] std::size_t heard_count() const { return last_heard_.size(); }

signals:
    /// A SeventhSlide with the key ``fingerprint`` takes connections at
    /// ``address:port``.
    void heard(const std::string& fingerprint, const QHostAddress& address, quint16 port);

private:
    void read();
    /// Forget the computers not heard lately; and, for room for one more,
    /// those heard longest ago.
    void forget_unheard(std::size_t room = 0);
    [[nodiscard]] std::vector<std::pair<QHostAddress, quint16>> targets() const;

    std::string fingerprint_;
    quint16 link_port_;
    Options options_;
    QUdpSocket socket_;
    QTimer every_;
    QTimer soon_;
    /// When each computer was last heard, to answer one not heard lately:
    /// only those heard lately.
    std::map<std::string, QElapsedTimer> last_heard_;
    QElapsedTimer last_answer_;
};

}  // namespace seventhslide::link
