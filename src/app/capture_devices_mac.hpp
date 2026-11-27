#pragma once

// macOS: the cameras and capture cards on this computer, from AVFoundation
// (capture_devices_mac.mm). FFmpeg's avfoundation input cannot list them;
// it opens one by its place in its own list, which this one matches.

#include <string>
#include <vector>

namespace seventhslide::app {

struct MacCamera {
    /// AVFoundation's localizedName. Two cameras of a model have the same
    /// one, and it may hold a ':' (which ends a device name for FFmpeg) or
    /// start with a digit (which FFmpeg reads as a place in its list), so it
    /// is shown, never opened by.
    std::string name;
    /// Its place in FFmpeg's avfoundation list of video devices: what to
    /// open it by, as the URL ("0", "1", ...).
    int index = -1;
    /// AVFoundation's uniqueID: the same from one start to the next.
    std::string unique_id;
    /// The fastest rate its current format runs at (frames a second), 0
    /// when it says none.
    double frame_rate = 0.0;
};

/// The video capture devices now, in no lasting order. The first time, asks
/// the person for the camera (the system shows its own question once), so
/// opening one later is allowed.
[[nodiscard]] std::vector<MacCamera> mac_cameras();

/// Whether the person has allowed the camera. Until then FFmpeg's
/// avfoundation input gets no frames and waits for the first without end.
[[nodiscard]] bool mac_camera_allowed();

}  // namespace seventhslide::app
