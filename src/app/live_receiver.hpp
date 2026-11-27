#pragma once

// One live source received: what LiveSources asks of whatever receives it.
// The app's receivers are in live_receivers.cpp; the tests hand in their own.
// A receiver keeps the source's latest picture and its sound, and says why
// there is none (not on the network, no runtime). A source that stops
// sending keeps its last picture, and is lost until it sends again.

#include <seventhslide/model/live_source.hpp>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

namespace seventhslide::audio {
class AudioSource;
}

namespace seventhslide::app {

class LiveReceiver : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    struct Stats {
        /// The pictures' size; empty before the first.
        QSize size;
        /// As the source declares it; 0 before the first picture.
        double frame_rate = 0.0;
    };

    /// The latest picture; null until the source sends one. The last one
    /// stays when the source stops sending.
    [[nodiscard]] virtual QImage frame() const = 0;
    /// The source's sound, for its stack's strip in the mixer; null when it
    /// has none. It lives as long as the receiver.
    [[nodiscard]] virtual audio::AudioSource* sound() = 0;
    /// Why there is no picture ("Waiting for ... on the network."); empty
    /// once there is one.
    [[nodiscard]] virtual QString problem() const = 0;
    [[nodiscard]] virtual Stats stats() const = 0;
    /// No picture for ``k_live_lost_ms``: none has come since it opened, or
    /// the source has stopped (``frame()`` is its last).
    [[nodiscard]] virtual bool lost() const { return false; }

signals:
    /// ``frame()`` is a new picture.
    void frameChanged();
    /// ``problem()`` changed.
    void problemChanged();
    /// ``lost()`` changed.
    void lostChanged();
};

/// What a receiver is for: the outputs (all of the source), or the Live
/// tab's thumbnail (as little as the source can send for one).
enum class LiveQuality { Full, Preview };

/// Makes a receiver for ``source``; null for a kind this build cannot receive.
using LiveReceiverFactory = std::function<std::unique_ptr<LiveReceiver>(
    const model::LiveSource& source, LiveQuality quality)>;

/// How long the app's receivers wait for a source's next picture before it
/// is lost.
inline constexpr int k_live_lost_ms = 2000;

/// The app's receivers: NDI (through the runtime found at run time), capture
/// devices, DeckLink inputs and streams.
[[nodiscard]] LiveReceiverFactory live_receiver_factory();

} // namespace seventhslide::app
