#pragma once

// miniaudio's view of the playback devices, shared by the engine (the Listen
// device) and the device sinks (a display's sound). Private to src/audio:
// miniaudio stays out of the public headers.

#include "seventhslide/audio/device_sink.hpp"

#include <miniaudio.h>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::audio::ma {

/// Open ``context`` for ``backend``: the system's sound, or the null device.
bool init_context(ma_context& context, AudioEngine::Backend backend);

/// The id a setting keeps for a device: the system's own name for it where
/// the backend has one (a PulseAudio sink, an ALSA device), else its name.
std::string device_key(ma_backend backend, const ma_device_info& info);

/// Every playback device ``context`` sees.
std::vector<DeviceInfo> playback_devices(ma_context& context);

/// Device ``key``'s id in ``context``; false when it is not there.
bool find_playback_device(ma_context& context, std::string_view key, ma_device_id& out);

/// How a playback device opens on ``context``: the mixer's format, and a
/// buffer the backend plays without dropouts. The callback may ask for any
/// number of frames.
ma_device_config playback_config(const ma_context& context);

} // namespace seventhslide::audio::ma
