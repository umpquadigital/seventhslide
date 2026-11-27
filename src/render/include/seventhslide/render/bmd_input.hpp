#pragma once

// Receiving a DeckLink input: a card's SDI or HDMI input, its picture and its
// sound. The driver (Desktop Video) is loaded at run time by the SDK's own
// dispatch, as for output (bmd_runtime.hpp); without it there are no inputs
// and nothing is received. No SDK header is included here: its types stay
// inside src/render. Seam: Qt Gui (the pictures are QImages), the DeckLink
// driver.

#include <seventhslide/audio/stream_source.hpp>
#include <seventhslide/render/bmd_runtime.hpp>

#include <QImage>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace seventhslide::render {

/// Capture-capable DeckLink sub-devices, freshly listed. Empty without the
/// driver or a card.
[[nodiscard]] std::vector<BmdDeviceInfo> bmd_list_input_devices();

/// 8-bit 4:2:2 YUV as DeckLink delivers it (``2vuy``: U Y0 V Y1), video range,
/// as an RGB32 picture, in the matrix ``video_yuv_matrix(height)`` names
/// (pixel_pack.hpp).
[[nodiscard]] QImage bmd_uyvy_to_image(const std::uint8_t* data, int width, int height,
                                       int row_bytes);

/**
 * One DeckLink input, by its device's name. The input format is detected
 * from the signal and followed when it changes; pictures arrive on the
 * driver's thread, the latest kept, and the sound (48 kHz stereo) pushed to
 * ``sound()``.
 */
class BmdReceiver {
public:
    using FrameReady = std::function<void()>;

    struct Stats {
        QSize size;
        double frame_rate = 0.0;
        std::uint64_t frames = 0;
    };

    /// The sound goes to ``sound`` when given (one that outlives this
    /// receiver, so a stack's strip can keep it across receivers), else to
    /// its own.
    BmdReceiver(QString device_name, FrameReady on_frame, audio::StreamSource* sound = nullptr);
    ~BmdReceiver();
    BmdReceiver(const BmdReceiver&) = delete;
    BmdReceiver& operator=(const BmdReceiver&) = delete;

    /// The input was found and is capturing.
    [[nodiscard]] bool available() const noexcept;
    /// Why not, or why there are no pictures (no signal); empty while there are.
    [[nodiscard]] QString problem() const;
    [[nodiscard]] QImage frame() const;
    [[nodiscard]] Stats stats() const;
    [[nodiscard]] audio::StreamSource& sound() noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace seventhslide::render
