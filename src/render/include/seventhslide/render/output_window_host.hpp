#pragma once

// Per-output audience sinks.
// Display destinations open fullscreen windows on QScreen; Window
// destinations open ordinary windows on the desktop. Virtual camera
// destinations render offscreen into a v4l2loopback device (Linux). NDI
// destinations render offscreen into an NDI sender, on any platform where the
// operator has installed an NDI runtime. Blackmagic destinations render
// offscreen out a DeckLink card wherever the operator has installed Desktop
// Video. SRT destinations stream, encoded, to a receiver over the network.
// Seam: Qt Gui. Stores nothing; see library::DestinationStore.

#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/render/destination.hpp"
#include "seventhslide/render/destination_registry.hpp"
#include "seventhslide/render/device_catalog.hpp"
#include "seventhslide/render/offscreen_sink.hpp"
#include "seventhslide/render/output_surfaces.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <seventhslide/audio/audio_engine.hpp>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <QSize>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QScreen;

namespace seventhslide::audio {
class DeviceSink;
class Mixer;
}

namespace seventhslide::render {

class OutputWindow;
class ScenePresenter;

/**
 * Owns audience output windows and destination routing.
 *
 * The destinations themselves are ``DestinationRegistry``'s, what they are
 * drawn on ``OutputSurfaces``'; this decides which destinations are open and
 * keeps the two in step as the operator edits them and screens come and go.
 *
 * Destinations survive monitor unplug and ``shutdown``; open windows are
 * runtime-only. Escape on a window, or closing it, closes it and leaves its
 * destination on (``is_closed``): it opens again when the destination is
 * switched off and on, when Live is turned on again (unless it ignores Live),
 * when its output is, or at the next start. An output
 * switched off (``set_outputs_off``) opens none of its destinations, and keeps
 * them for when it is back on; but a screen or device is still one
 * destination's, so one another output takes meanwhile is switched off there,
 * as it would be were the output on. Live off (``set_live``) holds back every
 * destination the same way, except those that ignore Live.
 *
 * The host holds the open library's destinations and never stores them: the
 * owner loads them with ``replace_destinations`` and saves on
 * ``destinationsEdited`` (see library::DestinationStore).
 */
class OutputWindowHost final : public QObject {
    Q_OBJECT

public:
    explicit OutputWindowHost(QObject* parent = nullptr);
    ~OutputWindowHost() override;

    /// The destinations, for the destination list.
    [[nodiscard]] const DestinationRegistry& registry() const noexcept { return registry_; }
    /// The screens and devices they point at, and what to call them.
    [[nodiscard]] const DeviceCatalog& devices() const noexcept { return devices_; }
    /// ``dest`` is drawing: its window is open, or its sink is live.
    [[nodiscard]] bool is_live(const Destination& dest) const { return surfaces_.is_live(dest); }
    /// ``dest_id``'s window was closed (Escape, its close button, the
    /// system) while it stayed enabled: nothing opens it again until it is
    /// switched off and on, Live or its output is turned on again, or the
    /// destinations are loaded again. Never stored.
    [[nodiscard]] bool is_closed(const QString& dest_id) const
    {
        return closed_.contains(dest_id);
    }
    /// What ``dest_id``'s sink reports; empty without one.
    [[nodiscard]] OffscreenSink::Status sink_status(const QString& dest_id) const;
    /// Screen key -> output id, for every open window over a screen.
    [[nodiscard]] QHash<QString, QString> open_screens() const
    {
        return surfaces_.open_screens();
    }
    /// Target key -> output id, for every live destination of ``kind``.
    [[nodiscard]] QHash<QString, QString> live_targets(DestinationKind kind) const;

    /// Re-probe for an NDI runtime the operator installed while we were running.
    void refresh_ndi_runtime();
    /// Re-probe for a DeckLink driver installed while we were running.
    void refresh_bmd_runtime();
    /// Last sink error (enable / bind failures). Empty after success.
    [[nodiscard]] QString last_error() const { return last_error_; }

    /// Add a destination to the output ``output_id``. ``type`` is a kind's
    /// name, and the kind must be available (``destination_kind_available``).
    /// Returns the new id, or empty when it cannot be added.
    QString add_destination(const QString& output_id, const QString& type);
    bool remove_destination(const QString& dest_id);
    /// Move a destination up (``step`` < 0) or down its output's list.
    bool move_destination(const QString& dest_id, int step);
    bool set_destination_enabled(const QString& dest_id, bool enabled);
    /// Send while enabled whether Live is on or off. With Live off, an
    /// enabled destination starts or stops at once.
    bool set_destination_ignore_live(const QString& dest_id, bool ignore);
    bool set_destination_screen(const QString& dest_id, const QString& screen_key);
    /// Bind a virtual-camera destination to a v4l2loopback device path.
    bool set_destination_camera(const QString& dest_id, const QString& device_path);
    /// Set the NDI source name receivers will see. Must be unique among this
    /// app's NDI destinations.
    bool set_destination_ndi_name(const QString& dest_id, const QString& source_name);
    /// NDI and Blackmagic: send a transparent background so a switcher can key
    /// the output. On Blackmagic this also turns on the card's own keyer.
    bool set_destination_alpha(const QString& dest_id, bool alpha);
    /// NDI, Blackmagic and virtual cameras (where
    /// ``destination_kind_carries_audio``): send the output's sound from the
    /// Audio Mixer with its pictures. A live sink starts or stops its sound
    /// without stopping the pictures. Refused off while the destination
    /// sends sound alone: it would send nothing.
    bool set_destination_audio(const QString& dest_id, bool audio);
    /// How the destination is drawn: no taller than ``max_height`` (0: as
    /// its output is; ``destination_kind_sizable`` kinds only), and with
    /// ``video`` off as sound alone (``destination_kind_sends_sound_alone``
    /// kinds only), which turns ``audio`` on. Takes effect at once, without
    /// reopening it: receivers stay connected.
    bool set_destination_format(const QString& dest_id, int max_height, bool video);
    /// SRT: how it streams and to where. Checked (``stream::validate``); a
    /// running stream starts again with them.
    bool set_destination_stream(const QString& dest_id, const stream::StreamSettings& settings);
    /// The Audio Mixer whose output mixes the destinations send. Null takes
    /// it back from every live sink: call that before the mixer goes. Not
    /// owned.
    void set_audio_mixer(audio::Mixer* mixer);
    /// The outputs (by id) whose sound a live destination is sending now.
    [[nodiscard]] QStringList outputs_sending_audio() const;
    /// Display or Window: play the output's mix on sound device ``device_id`` (an
    /// ``audio::playback_devices`` id; empty for none) while its window is
    /// open. A device that is not there stays silent.
    bool set_destination_audio_device(const QString& dest_id, const QString& device_id);
    /// The sound system displays' devices open on: the system's, or the null
    /// device for tests.
    void set_sound_backend(audio::AudioEngine::Backend backend);
    /// Display ``dest_id``'s sound is playing on its device now.
    [[nodiscard]] bool display_sound_playing(const QString& dest_id) const;
    /// Bind a Blackmagic destination to a DeckLink device.
    bool set_destination_bmd_device(const QString& dest_id, const QString& device_id);
    /// Set the broadcast video mode a Blackmagic destination outputs.
    bool set_destination_bmd_mode(const QString& dest_id, const QString& mode_key);
    /// Operator-facing title (independent of QScreen identity ``label``).
    bool set_destination_name(const QString& dest_id, const QString& name);
    /// The outputs' display names, by output id: window titles, the default
    /// NDI source name and "used by" labels. Destinations follow their output
    /// by id, so a rename changes only what these show.
    void set_output_names(QHash<QString, QString> names_by_id);
    /// Drop destinations and windows for a deleted output.
    void remove_output(const QString& output_id);
    /// The outputs switched off, by id; the rest are on. Closes what the
    /// newly off ones have open, and opens nothing: ``restore`` does.
    void set_outputs_off(QSet<QString> output_ids);
    [[nodiscard]] bool output_is_off(const QString& output_id) const
    {
        return outputs_off_.contains(output_id);
    }
    /// Live: on, every enabled destination sends; off, only those that
    /// ignore Live do. Off closes the rest at once, and keeps them enabled;
    /// on opens nothing: ``restore`` does. This computer's alone, and never
    /// stored: on here, and the app starts with it off.
    void set_live(bool on);
    [[nodiscard]] bool live() const noexcept { return live_; }
    /// Turning Live off would stop a destination that is sending now.
    [[nodiscard]] bool live_off_stops_any() const;

    /// Every destination as a record naming its output's id in "output": what
    /// ``replace_destinations`` reads back.
    [[nodiscard]] nlohmann::json destination_records() const { return registry_.records(); }
    /// Swap in another library's destinations. Tears down every window and sink
    /// first and opens nothing; ``restore`` does. A record of no known kind
    /// is skipped.
    void replace_destinations(const nlohmann::json& records);

    /// Destroy open windows and sinks; keep the destinations.
    void shutdown();
    /// A desktop window moved a moment ago: announce it now
    /// (``destinationsEdited``) rather than once it settles. Call before the
    /// owner stops saving to the library open now.
    void flush_edits();
    /// The operator's window moved to another screen: each output window
    /// above everything or an ordinary one again (OutputWindow::apply_stacking).
    void restack();

    /// The application's Quit: ``shutdown`` first, as Qt then asks every
    /// window to close and an audience window refuses a close not its
    /// host's. Public so a test can hand it a Quit without quitting.
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Open every enabled destination whose screen or device is present.
    void restore();

    /// C++ present path (Session -> frames). Fans out to live windows and camera sinks.
    void present(const QString& output_id, scene::SharedSceneFrame frame)
    {
        surfaces_.present(output_id, std::move(frame));
    }
    /// Present each frame to the output its ``output_id`` names.
    void present_all(const std::vector<scene::SharedSceneFrame>& frames)
    {
        surfaces_.present_all(frames);
    }
    /// The presentation an in-app preview of ``output_id`` draws; see
    /// OutputSurfaces::hold_presenter.
    [[nodiscard]] ScenePresenter* hold_presenter(const QString& output_id)
    {
        return surfaces_.hold_presenter(output_id);
    }
    void release_presenter(const QString& output_id) { surfaces_.release_presenter(output_id); }
    /// Decode-ahead for image playlists (next/prev). Runs on open canvases.
    void prefetch_static_urls(const std::vector<std::string>& urls, int edge = 0)
    {
        surfaces_.prefetch_static_urls(urls, edge);
    }

    /// Root for ``/static/...`` images (the library media folder).
    void set_static_root(std::filesystem::path root) { surfaces_.set_static_root(std::move(root)); }
    /// Pictures ``urls`` arrived: every output draws again what lacked one.
    void forget_missing_pictures(const std::vector<std::string>& urls)
    {
        surfaces_.forget_missing_pictures(urls);
    }
    [[nodiscard]] const std::filesystem::path& static_root() const noexcept
    {
        return surfaces_.static_root();
    }

    /// Theme background video frames. Cover-fit in paint.
    /// ``provider``'s sources' latest pictures, to every output (see
    /// OutputSurfaces and ScenePresenter).
    void set_source_frames(std::string_view provider, const SourceFrames& frames)
    {
        surfaces_.set_source_frames(provider, frames);
    }
    void set_background_video_frame(QImage frame)
    {
        surfaces_.set_background_video_frame(std::move(frame));
    }
    /// Shared font face cache for bold/italic paint (not owned).
    void set_font_faces(const layout::QtFontFaces* faces) { surfaces_.set_font_faces(faces); }

    [[nodiscard]] bool is_open(const QString& output_id) const
    {
        return surfaces_.is_open(output_id);
    }
    [[nodiscard]] OutputWindow* window(const QString& output_id) const
    {
        return surfaces_.window_for_output(output_id);
    }

signals:
    void changed();
    /// What a running destination reports changed on its own: receivers came
    /// or went, a stream connected or lost its receiver (see
    /// ``sink_status``). The destinations themselves are unchanged.
    void sinkStatusChanged();
    /// The operator changed the destination list or a destination's settings.
    void destinationsEdited();
    /// Live was turned on or off (``set_live``).
    void liveChanged();

private slots:
    void on_screen_added(QScreen* screen);
    void on_screen_geometry_changed(QScreen* screen);
    void on_screen_removed(QScreen* screen);
    void on_window_close_requested(const QString& destination_id);
    void on_window_place_changed(const QString& destination_id, QRect geometry);

private:
    /// Follow ``screen``'s geometry changes (on_screen_geometry_changed).
    void watch_screen(QScreen* screen);
    /// Disable other enabled destinations of ``kind`` on the same target (a screen,
    /// a camera device, a DeckLink sub-device): each can serve one destination.
    void disable_others_on_target(DestinationKind kind, const QString& target_key,
                                  const QString& keep_id);
    /// Finish an edit to ``dest``: with ``reopen``, start it again so the edit
    /// takes effect, disabling it if that fails; then announce the edit.
    /// Returns false when it could not reopen.
    bool commit(Destination& dest, bool reopen);
    /// The destination list or a destination's settings changed.
    void announce_edit();
    bool open_dest_window(Destination& dest);
    /// Open Window destination ``dest`` where it was left
    /// (``desktop_window_geometry``).
    void open_desktop_window(Destination& dest);
    bool start_vcam_sink(Destination& dest);
    bool start_ndi_sink(Destination& dest);
    /// Hand ``dest``'s sink, when it has one, the mixer.
    void apply_audio(const Destination& dest);
    /// Play each display's sound on its device while its window is open,
    /// and on nothing else. A device that would not open is tried again
    /// only when ``retry`` (a restore) or when its choice changes.
    void sync_display_sounds(bool retry = false);
    bool start_bmd_sink(Destination& dest);
    bool start_srt_sink(Destination& dest);
    /// Neither its output being off nor Live being off holds ``dest`` back:
    /// enabled, it may open.
    [[nodiscard]] bool may_open(const Destination& dest) const;
    /// ``dest`` has a window or a sink open.
    [[nodiscard]] bool is_running(const Destination& dest) const;
    /// Open ``dest``: its window, or its sink. False when it cannot.
    bool start_destination(Destination& dest);
    /// Tear down whatever ``dest_id`` runs, window or sink.
    void destroy_destination(const QString& dest_id);
    void restore_enabled_windows();

    DestinationRegistry registry_;
    DeviceCatalog devices_;
    OutputSurfaces surfaces_;
    QSet<QString> outputs_off_;
    /// Destinations whose window was closed while they stayed on
    /// (``is_closed``).
    QSet<QString> closed_;
    /// A desktop window moved: its place is saved once it settles.
    QTimer place_save_;
    /// Screens came, went or changed a moment ago: the system is moving
    /// desktop windows off a screen gone, or back, and where it puts them is
    /// not where the operator left them.
    QTimer screens_settling_;
    bool live_ = true;
    audio::Mixer* mixer_ = nullptr;
    audio::AudioEngine::Backend sound_backend_ = audio::AudioEngine::Backend::System;
    /// A display's sound, by destination id, and the device it was opened on.
    struct DisplaySound {
        QString device;
        std::unique_ptr<audio::DeviceSink> sink;
    };
    std::map<QString, DisplaySound> display_sounds_;
    QString last_error_;
};

}  // namespace seventhslide::render
