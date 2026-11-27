#pragma once

// Runtime discovery of the Blackmagic DeckLink driver (Desktop Video).
//
// SeventhSlide never ships, links, or redistributes any Blackmagic binary. The
// operator installs Desktop Video themselves; the vendored SDK dispatch
// (third_party/decklink) dlopen's it at run time, and we light up the
// Blackmagic destination type only when it is actually there. Everything here
// is safe to call on a machine that has never heard of DeckLink.
//
// NOTE: no SDK header is included here, deliberately - the SDK include path is
// PRIVATE to seventhslide_render. Video modes cross this boundary as opaque
// string keys, not as BMDDisplayMode.

#include <QString>

#include <vector>

namespace seventhslide::render {

/// One DeckLink sub-device (output-capable ones for the destinations,
/// capture-capable ones for the Live tab).
struct BmdDeviceInfo {
    /// Stable key persisted on the destination. Device handle where the driver
    /// offers one, else persistent ID, else the display name.
    QString id;
    QString name;   ///< GetDisplayName - distinguishes sub-devices ("... (1)")
    QString model;  ///< GetModelName
    QString label;  ///< Operator-facing, "name - model"
    bool can_output = false;
    bool can_capture = false;
    bool supports_internal_key = false;
    bool supports_external_key = false;
};

/// One video mode an output-capable device can be set to.
struct BmdDisplayModeInfo {
    QString key;   ///< Opaque persisted key (the mode's four-character code)
    QString name;  ///< "1080i59.94" as the driver names it
    int width = 0;
    int height = 0;
    long long frame_duration = 0;  ///< e.g. 1001
    long long time_scale = 0;      ///< e.g. 30000
    bool interlaced = false;

    /// Frames per second, 0 when the driver reported no usable rate.
    [[nodiscard]] double fps() const noexcept;
    /// Keepalive cadence for this mode, clamped to something sane. The
    /// OffscreenSink base assumes ~30 fps; interlaced 50 Hz modes do not.
    [[nodiscard]] int tick_interval_ms() const noexcept;
};

/// True when the DeckLink driver was found and the API could be instantiated.
/// Cached after the first probe; call ``bmd_runtime_refresh()`` to look again.
/// NOTE: this says the DRIVER is present, not that a card is attached - see
/// ``bmd_list_devices()``. The two states are surfaced differently in the UI.
[[nodiscard]] bool bmd_runtime_available();

/// Driver API version ("16.4.0"), empty when unavailable.
[[nodiscard]] QString bmd_runtime_version();

/// Absolute path of the loaded driver library, empty when unavailable or when
/// the platform cannot report it. Support logs only.
[[nodiscard]] QString bmd_runtime_path();

/// Operator-facing explanation of why Blackmagic is greyed out. Never empty.
[[nodiscard]] QString bmd_missing_message();

/// Operator-facing explanation for "driver installed, but no card". Never empty.
[[nodiscard]] QString bmd_no_device_message();

/// Re-probe. Lets an operator install Desktop Video without restarting.
/// A driver that is already loaded is kept as-is.
void bmd_runtime_refresh();

/// Output-capable devices, freshly enumerated (never cached - cards are
/// hot-pluggable and profiles reshuffle sub-devices). Empty when the driver is
/// absent or no card is attached.
[[nodiscard]] std::vector<BmdDeviceInfo> bmd_list_devices();

/// Video modes ``device_id`` can output. Empty when the device is gone.
[[nodiscard]] std::vector<BmdDisplayModeInfo> bmd_list_display_modes(const QString& device_id);

}  // namespace seventhslide::render
