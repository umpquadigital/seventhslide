#pragma once

// One place an output is sent: a screen, a window on the desktop, an NDI
// source, a DeckLink output, a virtual camera or an SRT stream. The record and what each kind is.
// Seam: Qt Core.

#include <seventhslide/stream/stream_settings.hpp>

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <array>
#include <optional>

namespace seventhslide::render {

/// What a destination sends its output to.
/// ``Window`` is an ordinary window on the desktop, which the operator moves
/// and resizes; ``Display`` covers a whole screen.
enum class DestinationKind { Display, Ndi, Bmd, Vcam, Srt, Window };

/// Every kind, in ``DestinationKind`` order.
inline constexpr std::array<DestinationKind, 6> k_destination_kinds{
    DestinationKind::Display, DestinationKind::Ndi, DestinationKind::Bmd,
    DestinationKind::Vcam,    DestinationKind::Srt, DestinationKind::Window};

/// The kind's name in stored records and in QML: "display", "ndi", "bmd",
/// "vcam", "srt", "window".
[[nodiscard]] QString destination_kind_name(DestinationKind kind);
/// The kind ``name`` names; nullopt for none.
[[nodiscard]] std::optional<DestinationKind> destination_kind_from_name(const QString& name);
/// What new destinations of the kind are called: "Display" for "Display 1".
[[nodiscard]] QString destination_kind_title(DestinationKind kind);
/// Served by an OffscreenSink rather than a window.
[[nodiscard]] bool destination_kind_offscreen(DestinationKind kind);
/// Drawn in a window of its own (a display's over its screen, or a desktop
/// window), whose sound can play on a device (``audio_device``).
[[nodiscard]] bool destination_kind_windowed(DestinationKind kind);
/// This build and machine can add one. Display and Window are always live. Virtual camera
/// is live on Linux. NDI is live wherever an NDI runtime is installed (see
/// ndi_runtime.hpp) - SeventhSlide never ships one. Blackmagic is live
/// wherever Desktop Video is installed (see bmd_runtime.hpp) - note that is the
/// DRIVER being present, not a card being attached: a driver with no card is
/// an enabled kind whose destination reports its device missing, the same
/// shape as a virtual camera with no loopback device. SRT is live where
/// FFmpeg was built with it (which encoders open is asked when one starts).
/// Probed at run time.
[[nodiscard]] bool destination_kind_available(DestinationKind kind);
/// This build can have a destination of this kind at all, whatever is
/// installed: not a virtual camera off Linux. One it cannot is not offered
/// to add; one that came from another computer is still listed, missing.
[[nodiscard]] bool destination_kind_built(DestinationKind kind);
/// A destination of this kind can send its output's sound ("Send audio").
[[nodiscard]] bool destination_kind_carries_audio(DestinationKind kind);
/// A destination of this kind can be drawn smaller than its output (its
/// ``max_height``). Not Blackmagic: the card's video mode decides its size.
[[nodiscard]] bool destination_kind_sizable(DestinationKind kind);
/// A destination of this kind can be sent as sound alone (its ``video``
/// off). NDI only.
[[nodiscard]] bool destination_kind_sends_sound_alone(DestinationKind kind);

/// The size a destination draws at, given the size it would draw at in full
/// (the output's canvas for a sink, the screen in pixels for a window) and
/// its ``max_height``: ``full`` when that is 0 or no lower, otherwise the same
/// shape that tall, rounded to even sides as video wants. Never larger than
/// ``full``: there is nothing more to send.
[[nodiscard]] QSize destination_send_size(QSize full, int max_height);

/// Where a desktop window destination opens (logical, its contents without
/// the system's frame): ``bounds`` ("x,y,w,h", where the operator left it)
/// while enough of it is on one of ``screens`` (their available areas) to be
/// taken hold of again, no larger than that screen; otherwise a window in the shape of ``canvas``, half
/// as wide as ``home`` (the operator's screen's available area) and at most
/// 960 wide, in its middle.
[[nodiscard]] QRect desktop_window_geometry(const QString& bounds, QSize canvas,
                                            const QList<QRect>& screens, QRect home);
/// "x,y,w,h" of ``r``, as ``Destination::bounds`` keeps it.
[[nodiscard]] QString bounds_text(QRect r);

struct Destination {
    QString id;
    /// ``OutputConfig::id`` of the output it shows.
    QString output_id;
    DestinationKind kind = DestinationKind::Display;
    bool enabled = false;
    /// Sends while it is enabled whether Live is on or off
    /// (``OutputWindowHost::set_live``).
    bool ignore_live = false;
    /// What it sends to, by kind: a screen key for a display, a /dev/video*
    /// path for a virtual camera, the source name for NDI, a DeckLink
    /// device id for Blackmagic, and for SRT where its stream goes or waits
    /// (``stream::address_text`` of ``stream``).
    QString target_key;
    QString name;  // operator title (inspector Name)
    /// The target's own name when last seen, not the operator title: a
    /// screen's QScreen::name() (to find it again after an unplug), or a
    /// camera's or DeckLink device's label. NDI keeps the source name.
    QString label;
    /// "x,y,w,h" logical: a display's screen when last seen, a desktop
    /// window's own place and size, as the operator left it.
    QString bounds;
    /// bmd only: the broadcast video mode. Empty = let the card pick.
    /// A DeckLink output is locked to a mode, so unlike every other sink
    /// the device alone does not say what to send.
    QString mode_key;
    bool alpha = false;  // ndi/bmd: send BGRA over a transparent background
    /// ndi / bmd / vcam: send the output's sound from the Audio Mixer with
    /// its pictures (a virtual camera as a microphone beside it). On unless
    /// turned off.
    bool audio = true;
    /// The tallest it is drawn (where ``destination_kind_sizable``), in the
    /// shape it would have in full; see ``destination_send_size``. 0 matches
    /// the output.
    int max_height = 0;
    /// ndi: send the picture. Off sends the output's sound alone, and then
    /// ``audio`` is on.
    bool video = true;
    /// srt: how it streams, and to where.
    stream::StreamSettings stream;
    /// display, window: the sound device (a ``audio::playback_devices`` id) its
    /// output's mix plays on while its window is open, such as the
    /// projector's own HDMI audio. Empty for none.
    QString audio_device;
};

}  // namespace seventhslide::render
