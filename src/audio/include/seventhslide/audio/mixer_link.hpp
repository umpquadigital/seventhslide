#pragma once

// A sink's membership of the mixer: which mixer and output it takes the mix
// of, whether that is wanted, and whether it is taking it now. The sink
// decides when it is ready (a sender open, a device started) and joins; it
// leaves before it lets go of what push() writes to. Every destination that
// carries sound (NDI, DeckLink, ...) keeps one.
// Seam: plain C++20 (no Qt). Call from one thread (the GUI's).

#include "seventhslide/audio/mixer.hpp"

#include <string>
#include <utility>

namespace seventhslide::audio {

class MixerLink {
public:
    explicit MixerLink(OutputAudioSink& sink)
        : sink_(sink)
    {
    }
    ~MixerLink() { leave(); }

    MixerLink(const MixerLink&) = delete;
    MixerLink& operator=(const MixerLink&) = delete;

    /// Take output ``output_id``'s mix from ``mixer`` when ``on``. Leaves
    /// first when anything changed; the sink joins again when it is ready.
    /// False when nothing changed.
    bool set(Mixer* mixer, std::string output_id, bool on)
    {
        if (mixer == mixer_ && output_id == output_id_ && on == on_) {
            return false;
        }
        leave();
        mixer_ = mixer;
        output_id_ = std::move(output_id);
        on_ = on;
        return true;
    }

    /// Given a mixer and an output, and switched on.
    [[nodiscard]] bool wanted() const noexcept
    {
        return mixer_ != nullptr && on_ && !output_id_.empty();
    }
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] bool joined() const noexcept { return joined_; }

    /// Become one of the mixer's sinks, when wanted. From here push() may
    /// be called on the mixer's clock thread.
    void join()
    {
        if (joined_ || !wanted()) {
            return;
        }
        mixer_->add_sink(output_id_, &sink_);
        joined_ = true;
    }

    /// Stop being one. Waits out a block in progress: after this push() is
    /// not called again.
    void leave()
    {
        if (!joined_) {
            return;
        }
        mixer_->remove_sink(&sink_);
        joined_ = false;
    }

private:
    OutputAudioSink& sink_;
    Mixer* mixer_ = nullptr;
    std::string output_id_;
    bool on_ = false;
    bool joined_ = false;
};

} // namespace seventhslide::audio
