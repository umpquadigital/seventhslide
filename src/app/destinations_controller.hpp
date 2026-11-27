#pragma once

// The destinations each output paints to (display, NDI, virtual camera,
// Blackmagic) and the runtimes behind them, for the output dialog.
//
// render::OutputWindowHost owns the sinks; this is its QML adapter. Every
// change bumps ``changed`` so an open dialog reloads, and asks the application
// (outputStateChanged, repaintRequested, liveStateChanged) to re-sync and
// repaint as far as that change needs.

#include "display_list_model.hpp"

#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class DestinationsController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// Displays a display destination can be bound to.
    Q_PROPERTY(seventhslide::app::DisplayListModel* displays READ displays CONSTANT)
    /// Live: the enabled destinations send. Off, only those that ignore
    /// Live do (OutputWindowHost::set_live). False with no host.
    Q_PROPERTY(bool liveOn READ liveOn NOTIFY liveOnChanged)

public:
    /// ``session`` holds the outputs the destinations belong to.
    explicit DestinationsController(const session::Session& session, QObject* parent = nullptr);

    /// The host whose sinks these are; null detaches (tests, headless).
    void set_output_host(render::OutputWindowHost* host);

    [[nodiscard]] DisplayListModel* displays() noexcept { return &displays_; }
    [[nodiscard]] bool liveOn() const;

    /// Re-read the displays the host can see.
    Q_INVOKABLE void refreshDisplays();
    /// Destinations of the output ``output_id``.
    Q_INVOKABLE QVariantList list(const QString& output_id);
    /// Add a destination to the output ``output_id``. ``type`` must be
    /// available. Returns {id} or {error}.
    Q_INVOKABLE QVariantMap add(const QString& output_id, const QString& type);
    /// Remove one destination. Empty = ok.
    Q_INVOKABLE QString remove(const QString& dest_id);
    /// Move a destination up (``step`` < 0) or down its output's list.
    Q_INVOKABLE QString move(const QString& dest_id, int step);
    /// Enable (send) or disable (stop) one destination.
    Q_INVOKABLE QString setEnabled(const QString& dest_id, bool enabled);
    /// Turn Live on (the enabled destinations open) or off (all but those
    /// that ignore it stop).
    Q_INVOKABLE void setLiveOn(bool on);
    /// Turning Live off now would stop something sending.
    Q_INVOKABLE bool liveOffStops() const;
    /// Send while enabled whether Live is on or off. Empty return = success.
    Q_INVOKABLE QString setIgnoreLive(const QString& dest_id, bool ignore);
    /// Bind a display destination to ``screen_key``.
    Q_INVOKABLE QString setScreen(const QString& dest_id, const QString& screen_key);
    /// Bind a virtual-camera destination to a v4l2loopback device path.
    Q_INVOKABLE QString setCamera(const QString& dest_id, const QString& device_path);
    /// Operator-facing destination title.
    Q_INVOKABLE QString setName(const QString& dest_id, const QString& name);
    /// Type catalog for the Add menu ({type, available}).
    Q_INVOKABLE QVariantList types() const;
    /// v4l2loopback devices ({key, name, label, assignedOutput}).
    Q_INVOKABLE QVariantList virtualCameras() const;
    /// The NDI source name receivers see. Empty return = success.
    Q_INVOKABLE QString setNdiName(const QString& dest_id, const QString& source_name);
    /// NDI only: send a transparent background for downstream keying.
    Q_INVOKABLE QString setAlpha(const QString& dest_id, bool alpha);
    /// NDI, Blackmagic and virtual cameras (a row's ``audioCapable``): send
    /// the output's sound (from the Audio Mixer) with its pictures. Empty
    /// return = success.
    Q_INVOKABLE QString setAudio(const QString& dest_id, bool audio);
    /// How a destination is drawn: no taller than ``max_height`` (0: as its
    /// output is; a row's ``sizable``), and with ``video`` false as sound
    /// alone (a row's ``soundAloneCapable``). Empty return = success.
    Q_INVOKABLE QString setFormat(const QString& dest_id, int max_height, bool video);
    /// SRT: change how it streams. ``changes`` holds what changed, by a
    /// row's ``stream`` names (mode, host, port, preset, codec, bitrateKbps)
    /// and "passphrase" ("" for none). Empty return = success.
    Q_INVOKABLE QString setStream(const QString& dest_id, const QVariantMap& changes);
    /// The video codecs an SRT stream can use here ({key, available, encoder,
    /// hardware}). Empty while this computer's encoders are being tried, on a
    /// worker (opening one can take a second); ``streamCodecsReady`` follows.
    Q_INVOKABLE QVariantList streamCodecs() const;
    /// What SRT destination ``dest_id`` streams at for a picture ``width``
    /// x ``height``: {auto (Automatic's bitrate, kb/s), latencyMs}.
    Q_INVOKABLE QVariantMap streamRates(const QString& dest_id, int width, int height) const;
    /// The sizes a destination of an output ``width`` x ``height`` can be
    /// drawn at, largest first ({w, h}): the output's, then each usual height
    /// under it in the output's shape.
    Q_INVOKABLE QVariantList sizeOptions(int width, int height) const;
    /// Display: the sound device its output's mix plays on ("" for none).
    /// Empty return = success.
    Q_INVOKABLE QString setAudioDevice(const QString& dest_id, const QString& device_id);
    /// What display ``dest_id``'s sound can play on ({key, label,
    /// suggested}), "None" first and the likeliest devices next.
    Q_INVOKABLE QVariantList soundDevices(const QString& dest_id) const;
    /// {available, version, path, message} for the separately-installed NDI runtime.
    Q_INVOKABLE QVariantMap ndiRuntimeInfo() const;
    /// Re-probe for an NDI runtime installed while SeventhSlide was running.
    Q_INVOKABLE QVariantMap refreshNdiRuntime();
    /// Bind a Blackmagic destination to a DeckLink device. Empty return = ok.
    Q_INVOKABLE QString setBmdDevice(const QString& dest_id, const QString& device_id);
    /// The broadcast video mode a Blackmagic destination outputs.
    Q_INVOKABLE QString setBmdMode(const QString& dest_id, const QString& mode_key);
    /// Output-capable DeckLink devices ({key, name, label, assignedOutput, ...}).
    Q_INVOKABLE QVariantList blackmagicDevices() const;
    /// Video modes a device can output ({key, name, width, height, fps}).
    Q_INVOKABLE QVariantList blackmagicModes(const QString& device_id) const;
    /// Re-probe for a DeckLink driver installed while SeventhSlide was running.
    Q_INVOKABLE QVariantMap refreshBmdRuntime();

    /// A destination changed outside these calls (the host itself, a restore).
    void notify_changed();

signals:
    /// A destination was added, removed or changed; the output dialog reloads.
    void changed();
    /// Which outputs are open, and on which screen, may have moved: the
    /// application pushes that into its output list.
    void outputStateChanged();
    /// Repaint every output now (a sink was just enabled).
    void repaintRequested();
    /// Live came on: the application opens the enabled destinations.
    void restoreRequested();
    void liveOnChanged();
    /// This computer's encoders are known: ``streamCodecs`` lists them now.
    void streamCodecsReady();
    /// Live state QML reads from the session changed.
    void liveStateChanged();
    /// What a running destination reports changed on its own (receivers, a
    /// stream's connection): its row says it, the list is otherwise as it
    /// was.
    void statusChanged();

private:
    /// Try this computer's encoders on a worker, once, unless they are known.
    void probe_encoders_soon() const;

    /// What a change has to republish afterwards. A rename changes only a
    /// label; enabling one has to push output state to the sink before
    /// anything can be painted to it; turning one off pushes state but paints
    /// nothing.
    enum class Refresh { None, Present, Sync, SyncAndPresent };
    /// The shared body of every set* call: refuse without a host, run
    /// ``apply``, and on failure report the host's own message where it has
    /// one, else ``failure``.
    [[nodiscard]] QString apply_change(const QString& dest_id,
                                       const std::function<bool()>& apply,
                                       const QString& failure,
                                       Refresh refresh);

    /// The output ``output_id``, or null.
    [[nodiscard]] const model::OutputConfig* find_output(const QString& output_id) const;

    const session::Session& session_;
    render::OutputWindowHost* host_ = nullptr;
    /// The host's liveChanged, to ``liveOnChanged``.
    QMetaObject::Connection live_connection_;
    DisplayListModel displays_;
    /// Encoders are being tried (``probe_encoders_soon``).
    mutable bool probing_encoders_ = false;
};

}  // namespace seventhslide::app
