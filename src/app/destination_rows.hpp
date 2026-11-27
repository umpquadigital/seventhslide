#pragma once

// The output dialog's rows for render::OutputWindowHost: its destinations, the
// screens, cameras and DeckLink devices they can point at, the kinds that can
// be added, and what the NDI and Blackmagic probes found. Each row is a
// QVariantMap with QML's field names; render answers in its own types.

#include <seventhslide/stream/stream_settings.hpp>

#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace seventhslide::audio {
struct DeviceInfo;
}

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

/// An SRT destination's settings as the editor shows them: {mode, host,
/// port, preset, codec, bitrateKbps, encrypted, address, reachAt (waiting:
/// the srt:// addresses a receiver on the network can open)}. Not the
/// passphrase, only whether there is one.
[[nodiscard]] QVariantMap stream_row(const stream::StreamSettings& settings);
/// ``base`` with what ``changes`` gives (stream_row's names, plus
/// "passphrase"); nullopt for a mode, preset or codec of no known name.
/// Not checked otherwise: see stream::validate.
[[nodiscard]] std::optional<stream::StreamSettings>
stream_settings_from(const QVariantMap& changes, stream::StreamSettings base);
/// The video codecs an SRT destination can be given: {key, available,
/// encoder, hardware}, the encoder being the one it would use. Empty until
/// this computer's encoders have been tried (stream::known_encoders): it
/// never waits for that.
[[nodiscard]] QVariantList stream_codec_rows();
/// What ``settings`` streams at for a picture ``size``: {auto (the bitrate
/// Automatic picks, kb/s), latencyMs}.
[[nodiscard]] QVariantMap stream_bitrate_row(const stream::StreamSettings& settings, QSize size);

/// The "add destination" menu: each row is {type, available, built}; built
/// false is a kind this system cannot have at all (``destination_kind_built``).
/// A virtual camera's also has the {message} for when it has no device.
[[nodiscard]] QVariantList destination_type_rows();
/// Destinations of the output ``output_id`` (empty = every output).
[[nodiscard]] QVariantList destination_rows(const render::OutputWindowHost& host,
                                            const QString& output_id = {});
/// The sizes a destination of an output ``canvas`` big can be drawn at,
/// largest first ({w, h}): the output's, then each usual height under it, as
/// ``render::destination_send_size`` makes them. Empty for no canvas.
[[nodiscard]] QVariantList destination_size_rows(QSize canvas);
/// Screens a display destination can show on ({key, label, assignedOutput, ...}).
[[nodiscard]] QVariantList display_rows(const render::OutputWindowHost& host);
/// v4l2loopback devices ({key, name, label, assignedOutput}); with no host,
/// no assignedOutput.
[[nodiscard]] QVariantList virtual_camera_rows(const render::OutputWindowHost* host);
/// Output-capable DeckLink devices ({key, name, model, label,
/// supportsInternalKeying, supportsExternalKeying, assignedOutput}); with no
/// host, no assignedOutput. Freshly enumerated.
[[nodiscard]] QVariantList blackmagic_device_rows(const render::OutputWindowHost* host);
/// Video modes ``device_id`` can output ({key, name, width, height, fps,
/// interlaced}).
[[nodiscard]] QVariantList blackmagic_mode_rows(const QString& device_id);
/// The NDI runtime: {available, version, path, message}; message is empty
/// when it is available.
[[nodiscard]] QVariantMap ndi_runtime_row();
/// The Blackmagic driver: {available, version, path, deviceCount, message}.
/// The message tells the three states apart: no driver, driver but no card,
/// or ready (empty).
[[nodiscard]] QVariantMap bmd_runtime_row();
/// What a display destination's sound can play on, for its dropdown
/// ({key, label, suggested}): none (key and label "") first, then ``devices``, those
/// whose names share words with ``screen_label`` (likely the screen's own
/// HDMI audio) ahead of the rest.
[[nodiscard]] QVariantList sound_device_rows(const QString& screen_label,
                                             const std::vector<audio::DeviceInfo>& devices);

}  // namespace seventhslide::app
