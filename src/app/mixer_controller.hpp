#pragma once

// The Audio Mixer: every stack's and output's level, mute and Listen, kept
// for the open library on this computer. Listen plays on the computer's
// default sound device.
//
// Owns the engine (src/audio) that plays and mixes all the app's sound, and
// gives it the stacks and outputs there are (sync, after every change).
// Settings are the open library's, read from the AudioMixStore beside the
// destinations when the library opens:
//
// - a library opened on this computer for the first time starts with its
//   first output on Listen (LibraryMix::first_use), saved at once so the rule
//   never applies to it again;
// - every change is saved half a second after the last one, and at once when
//   the library closes or the app quits;
// - a stack or output deleted from the library loses its strip, so one made
//   later with the same id starts at the defaults.
//
// SessionFacade owns one and exposes it as `session.mixer`.

#include "mixer_channel_model.hpp"
#include "open_library.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/library/audio_mix_store.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <string_view>

class QTimer;

namespace seventhslide::audio {
class AudioEngine;
}

namespace seventhslide::app {

class MixerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns the one Audio Mixer: session.mixer")

    /// The Stacks tab: a strip per stack, in the stacks' order.
    Q_PROPERTY(seventhslide::app::MixerChannelModel* stacks READ stacks CONSTANT)
    /// The Outputs tab: a strip per output, in the Outputs panel's order.
    Q_PROPERTY(seventhslide::app::MixerChannelModel* outputs READ outputs CONSTANT)
    /// The panel's tab: "stacks" or "outputs". Kept on this computer.
    Q_PROPERTY(QString shownTab READ shownTab WRITE setShownTab NOTIFY shownTabChanged)

public:
    /// How long after the last change the settings are written.
    static constexpr int k_save_delay_ms = 500;
    /// How often the meters read the engine while one shows.
    static constexpr int k_meter_interval_ms = 33;

    MixerController(session::Session& session, OpenLibrary& library, QObject* parent = nullptr);
    /// Writes what is not saved yet and stops the engine.
    ~MixerController() override;

    [[nodiscard]] audio::AudioEngine& engine() noexcept { return *engine_; }
    [[nodiscard]] MixerChannelModel* stacks() noexcept { return stacks_; }
    [[nodiscard]] MixerChannelModel* outputs() noexcept { return outputs_; }

    /// The library ``library_id`` has opened (its stacks and outputs are the
    /// session's): load its mixer, or start it as new, and set every strip.
    /// An empty id (an in-memory database) keeps the settings unsaved.
    void open_library(std::string library_id);
    /// Save what is pending and let the library's settings go.
    void close_library();
    /// Give the engine the stacks and outputs there are now and what each
    /// output carries, refresh the tabs, and forget the strips of stacks and
    /// outputs that were deleted. Call after any change.
    void sync();
    /// Write a pending change now.
    void flush();

    /// Play Listen on this computer's default sound device. Until this is
    /// called the engine runs on its timer, so tests and headless runs make
    /// no sound.
    void start_listen_device();

    /// ``kind`` is "stack" or "output". Each returns why it refused, or "".
    /// A level above 0 dB is 0 dB; below the fader's floor it is silence;
    /// NaN is refused.
    Q_INVOKABLE [[nodiscard]] QString setGainDb(const QString& kind, const QString& id, double db);
    Q_INVOKABLE [[nodiscard]] QString setMuted(const QString& kind, const QString& id, bool muted);
    Q_INVOKABLE [[nodiscard]] QString setListen(const QString& kind, const QString& id, bool listen);
    /// The fader's taper, for the view: the level at a position (0 to 1). A
    /// strip's own position is its row's (``FaderPositionRole``).
    Q_INVOKABLE [[nodiscard]] double faderDb(double position) const;
    /// How far up a meter ``db`` reaches, 0 to 1 (for its scale's marks).
    Q_INVOKABLE [[nodiscard]] double meterPosition(double db) const;

    /// A strip's meter now; silence for one not in the mixer.
    [[nodiscard]] audio::MeterReading meter(audio::StripKind kind, std::string_view id) const;
    /// A meter showing on screen: while any is, ``metersTick`` fires every
    /// k_meter_interval_ms. Each add is matched by a remove.
    void add_meter_view();
    void remove_meter_view();
    [[nodiscard]] bool meters_running() const;

    [[nodiscard]] QString shownTab() const { return shown_tab_; }
    void setShownTab(const QString& tab);

    /// What the open library's mixer holds (tests).
    [[nodiscard]] const library::LibraryMix& library_mix() const noexcept { return mix_; }

signals:
    void errorOccurred(const QString& message);
    void shownTabChanged();
    /// Time for the meters showing to read the engine again.
    void metersTick();

private:
    /// Change one strip's settings with ``edit``; "" or why not.
    template <typename Edit>
    QString change_strip(const QString& kind, const QString& id, Edit edit);
    /// The store, when settings are kept for the open library.
    [[nodiscard]] library::AudioMixStore* store_for_library() noexcept;
    void schedule_save();
    /// Write the open library's mixer; false (reported) on failure.
    bool save_now();

    session::Session& session_;
    OpenLibrary& library_;
    std::unique_ptr<audio::AudioEngine> engine_;
    MixerChannelModel* stacks_ = nullptr;
    MixerChannelModel* outputs_ = nullptr;
    QTimer* save_timer_ = nullptr;
    QTimer* meter_timer_ = nullptr;
    int meter_views_ = 0;
    QString shown_tab_;

    /// The library whose settings ``mix_`` holds; empty for none kept.
    std::string library_id_;
    library::LibraryMix mix_;
    /// What the engine was last given.
    audio::Routing routing_;
    /// The next sync sets every strip, not only new ones: a library opened.
    bool set_every_strip_ = true;
};

} // namespace seventhslide::app
