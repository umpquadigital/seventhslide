#pragma once

// An output's sound as a microphone other apps can pick: a PipeWire node of
// class Audio/Source, named after the output ("SeventhSlide Main"). A virtual
// camera carries it, so Zoom, OBS or a browser take the sound with the
// picture. The mixer pushes the output's mix on its clock; PipeWire pulls on
// the graph's through a DriftBuffer.
//
// Linux only, and only where libpipewire was found at build time
// (``pipewire_available``); elsewhere it never starts.
// Seam: plain C++20 (no Qt); PipeWire stays in the .cpp.

#include "seventhslide/audio/drift_buffer.hpp"
#include "seventhslide/audio/mixer.hpp"
#include "seventhslide/audio/mixer_link.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace seventhslide::audio {

/// This build has PipeWire, and a PipeWire server answers.
[[nodiscard]] bool pipewire_available();

class PipeWireSource final : public OutputAudioSink {
public:
    PipeWireSource();
    ~PipeWireSource() override;

    PipeWireSource(const PipeWireSource&) = delete;
    PipeWireSource& operator=(const PipeWireSource&) = delete;

    /// Offer output ``output_id``'s mix from ``mixer`` as a microphone
    /// called ``name``. Starting again replaces it. False when PipeWire is
    /// not there.
    bool start(Mixer& mixer, std::string output_id, std::string_view name);
    /// Leave the mixer, then take the microphone away.
    void stop();
    [[nodiscard]] bool running() const noexcept;
    /// The node's name in PipeWire (what ``pw-record --target`` takes);
    /// empty when stopped.
    [[nodiscard]] std::string node_name() const;

    /// The mixer's clock thread.
    void push(const float* frames, std::size_t count) override;

    /// Frames an app has taken since it started (tests).
    [[nodiscard]] std::uint64_t frames_sent() const noexcept;

private:
    struct Impl;
    DriftBuffer drift_;
    std::unique_ptr<Impl> impl_;
    /// Last: it leaves the mixer before the rest goes.
    MixerLink link_{*this};
};

} // namespace seventhslide::audio
