#pragma once

// The live sources (cameras) on any side of any stack, each received by its
// own receiver, and their latest pictures for the outputs and the operator's
// previews.
//
// SessionFacade owns one. After content changes (``sync``) a receiver is
// started for each source newly shown and the receivers of sources no side
// shows any more are stopped. A source is filed by its device
// (``model::live_source_key``), which has one reader: the same source on two
// stacks, or cued and live, shares one receiver, and a take moves nothing.
// Its pictures go to the output host (as the "live" provider's) while it is
// on a Program, and stay a moment for the change that takes it off
// (SourceRouting); cued alone, they are the previews' only. Its one sound plays
// on the strip of a stack it is live on while it is live anywhere, else on
// the cue strip of a stack it is cued on (``audio::cue_strip_id``), staying
// on the strip it has while that still holds. WebSources does the same for
// web pages, by tab.

#include "live_receiver.hpp"
#include "source_routing.hpp"

#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/session/session.hpp>

#include <QObject>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::audio {
class Mixer;
}

namespace seventhslide::app {

class LiveSources : public QObject {
    Q_OBJECT

public:
    /// Sources' sound plays on ``mixer``'s stack strips; null leaves them
    /// silent (tests). ``mixer`` must outlive this.
    LiveSources(const session::Session& session, audio::Mixer* mixer, QObject* parent = nullptr);
    ~LiveSources() override;

    /// How sources are received.
    void set_factory(LiveReceiverFactory factory);
    /// Where the pictures go; null detaches (tests, headless).
    void set_output_host(render::OutputWindowHost* host);

    /// Start a receiver for every source on a side of a stack, stop the
    /// rest, and route each by the sides it is on.
    void sync();

    /// Every source's latest picture, live or cued, by key: what the
    /// previews paint.
    [[nodiscard]] const render::SourceFrames& frames() const noexcept
    {
        return routing_.frames();
    }
    /// What the outputs are given: the pictures of the sources on a Program,
    /// and of those still going out with a change.
    [[nodiscard]] const render::SourceFrames& output_frames() const noexcept
    {
        return routing_.output_frames();
    }
    /// ``source``'s receiver; null when no side shows it.
    [[nodiscard]] LiveReceiver* receiver(std::string_view source) const;
    [[nodiscard]] std::size_t count() const noexcept { return receivers_.size(); }
    /// The sources received now, by key.
    [[nodiscard]] std::vector<std::string> keys() const;
    /// The mixer strip ``source``'s sound plays on: a stack's, or a stack's
    /// cue strip while it is only cued; empty when none.
    [[nodiscard]] std::string sound_strip(std::string_view source) const
    {
        return routing_.sound_strip(source);
    }

signals:
    /// ``source`` is about to be received: whatever else holds it (a device
    /// opens for one reader) lets go now.
    void aboutToReceive(const QString& source);
    /// A source sent a new picture, or a source started or stopped.
    void framesChanged();
    /// A source started, stopped, or its ``problem()`` or ``lost()`` changed.
    void stateChanged();

private:
    void take_frame(const std::string& source);

    const session::Session& session_;
    LiveReceiverFactory factory_;
    std::map<std::string, std::unique_ptr<LiveReceiver>, std::less<>> receivers_;
    /// After the receivers, so it takes their sounds off the mixer before
    /// they go.
    SourceRouting routing_;
};

} // namespace seventhslide::app
