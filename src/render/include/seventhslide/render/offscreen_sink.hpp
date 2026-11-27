#pragma once

// Shared base for offscreen audience sinks (destinations that render into a
// transport instead of onto a QScreen): v4l2loopback virtual cameras, NDI
// senders, Blackmagic DeckLink cards and SRT streams.
// Seam: Qt Gui only. Transport specifics live in the subclass.

#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/render/scene_presenter.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

class QTimer;

namespace seventhslide::audio {
class Mixer;
}

namespace seventhslide::render {

/// Round down to an even dimension, floored at 2. Most video transports
/// (YUYV 4:2:2, NDI UYVY) cannot carry odd widths.
[[nodiscard]] int even_dim(int n);

/**
 * SceneFrame -> offscreen transport.
 *
 * Owns the transport and the delivery cadence; what to show and how to draw it
 * belongs to a ScenePresenter. A ~30 fps timer keeps re-delivering the last
 * frame so receivers see a live stream on a static slide.
 *
 * A sink makes its own presenter, so one works standalone. Destinations that
 * share an output are given a shared presenter instead (see
 * ``set_presenter``), and then the scene is composited once for all of them
 * rather than once each - and its background images decoded once.
 *
 * Transitions come from the presenter, using the same fade state machine and
 * easing curve audience windows use, so a cross-fade looks the same on an NDI
 * receiver as it does in the operator preview.
 *
 * Subclasses implement only the transport: ``on_start`` / ``on_stop`` /
 * ``deliver``. ``deliver`` is told whether the image actually changed, so a
 * sink with an expensive encode (YUYV) can cache its encoded buffer and skip
 * the work on keepalive ticks.
 *
 * The picture is asked of the presenter without waiting (``request``): its
 * thread composites, and ``deliver`` is called back on this thread a moment
 * later. One request is in flight at a time; a tick meanwhile asks again
 * once it lands.
 *
 * Lifetime: a subclass destructor MUST call ``stop()`` itself. The base
 * destructor cannot, because ``on_stop()`` is virtual and the subclass is
 * already gone by then.
 */
class OffscreenSink : public QObject {
    Q_OBJECT

public:
    /// The keepalive's cadence unless a sink has its own: 33 ms, about 30 a
    /// second.
    static constexpr int k_keepalive_ms = 33;

    ~OffscreenSink() override;

    /// Open the transport at (or near) ``size``. ``target`` is the sink key:
    /// a device path for virtual cameras, a source name for NDI.
    bool start(const QString& target, QSize size);
    void stop();

    [[nodiscard]] bool is_live() const noexcept { return live_; }
    [[nodiscard]] QString target() const { return target_; }
    [[nodiscard]] QString last_error() const { return last_error_; }
    [[nodiscard]] QSize output_size() const noexcept { return size_; }

    /// Show ``frame``. Starts a cross-fade when the theme asks for one and the
    /// outgoing frame is compatible; otherwise snaps.
    void present(scene::SharedSceneFrame frame);
    /// The presenter was given a new frame by its owner (a shared presenter):
    /// follow its canvas size and deliver.
    void presentation_changed();
    void set_background_video_frame(QImage frame);
    /// The sources' latest pictures: videos, pages, live sources (see
    /// ScenePresenter::set_source_frames).
    void set_source_frames(const SourceFrames& frames);
    void set_static_root(std::filesystem::path root);
    void set_font_faces(const layout::QtFontFaces* faces) noexcept
    {
        presenter_->set_font_faces(faces);
    }
    /// Test hook: freeze the fade clocks and drive progress directly, so a
    /// transition can be inspected without waiting on the wall clock.
    void set_fade_progress_for_test(double t);

    [[nodiscard]] bool is_fading() const noexcept { return presenter_->is_fading(); }
    /// A picture has been asked for and not delivered yet.
    [[nodiscard]] bool delivering() const noexcept { return in_flight_; }

    /// What the destination list shows about a running sink. A transport
    /// leaves the fields it has nothing to say about at their defaults.
    struct Status {
        /// Receivers connected (NDI); -1 when unknown.
        int receivers = -1;
        /// Program / preview tally from a downstream switcher (NDI).
        bool on_program = false;
        bool on_preview = false;
        /// The broadcast mode a fixed-format transport opened (Blackmagic).
        QString mode_name;
        /// The transport's own keyer is on (Blackmagic).
        bool keying = false;
        /// A stream's (SRT): where it is ("waiting", "sending", ... as
        /// stream::state_name says), the encoder it opened and whether that
        /// is hardware, its bitrate, and why it is retrying or failed.
        QString stream_state;
        QString encoder;
        bool hardware_encoder = false;
        int bitrate_kbps = 0;
        QString error;
    };
    /// What the sink says now. A sink whose status can change on its own (a
    /// receiver connecting) says so with ``statusChanged``.
    [[nodiscard]] virtual Status status() const { return {}; }

    /// Send an alpha channel over a transparent background, so a downstream
    /// switcher can key the output. A transport that cannot carry alpha
    /// ignores it. When it takes effect is the transport's: NDI on the next
    /// frame, Blackmagic on the next start.
    virtual void set_alpha_enabled(bool /*on*/) {}

    /// Send output ``output_id``'s mix from ``mixer`` with the pictures (the
    /// destination's "Send audio"). Null, or ``on`` false, sends pictures
    /// only. Takes effect at once on a live sink. ``mixer`` must outlive the
    /// sink, or be taken back first (``set_audio(nullptr, ...)``). A
    /// transport that carries no sound ignores it.
    virtual void set_audio(audio::Mixer* /*mixer*/, std::string /*output_id*/, bool /*on*/) {}
    /// Sending sound now: live, given a mixer, switched on, and able to.
    [[nodiscard]] virtual bool sending_audio() const { return false; }

    /// Draw no taller than ``max_height`` (0: the output's own size), in the
    /// output's shape: see ``destination_send_size``. The transport's own
    /// limits apply after (``negotiate_size``). A live sink resizes at once.
    void set_max_height(int max_height);

    /// Send pictures (on by default). Off draws and delivers nothing, so a
    /// transport that carries sound sends that alone. Takes effect at once
    /// on a live sink.
    void set_video_enabled(bool on);

    /// Draw from ``presenter`` instead of this sink's own. Not owned, and it
    /// must outlive the sink. Set before ``start`` so the first composite is
    /// already shared.
    void set_presenter(ScenePresenter* presenter);
    [[nodiscard]] ScenePresenter& presenter() noexcept { return *presenter_; }

signals:
    /// ``status`` changed on its own: receivers came or went, a stream
    /// connected or lost its receiver.
    void statusChanged();

protected:
    explicit OffscreenSink(QObject* parent = nullptr);

    /// Open the transport. ``target()`` is already set; ``size`` is the
    /// negotiated size. Set ``last_error`` on failure. May call
    /// ``set_output_size`` if the transport picks a different size.
    virtual bool on_start(QSize size) = 0;

    /// Close the transport. Must be safe to call when never started.
    virtual void on_stop() = 0;

    /// Hand one rendered frame to the transport, on the GUI thread.
    /// ``changed`` is false on keepalive ticks where the image is byte-for-byte
    /// the one passed last time. The image is the presenter's: a sink that
    /// keeps it (to convert it on a thread of its own) makes the presenter
    /// paint the next one into its spare buffer, so it lets go once done.
    virtual void deliver(const QImage& image, bool changed) = 0;

    /// Retarget a live transport to a new canvas size. The default accepts any
    /// size (NDI carries per-frame dimensions); sinks that must renegotiate a
    /// fixed format override this. Returning false keeps the previous size
    /// rather than dropping the stream.
    virtual bool on_resize(QSize size);

    /// The size this sink can actually carry for a requested canvas size.
    [[nodiscard]] virtual QSize negotiate_size(QSize want) const;

    /// Background for letterbox bars and for the no-frame fill. Sinks that
    /// carry alpha may return a transparent color.
    [[nodiscard]] virtual QColor letterbox_color() const { return QColor(Qt::black); }

    /// Keepalive cadence.
    [[nodiscard]] virtual int tick_interval_ms() const { return k_keepalive_ms; }

    void set_last_error(QString text) { last_error_ = std::move(text); }
    void set_output_size(QSize size) { size_ = size; }
    void mark_dirty() { dirty_ = true; }

    /// Ask for the picture and deliver it when it comes. No-op when not
    /// live, or with video off.
    void tick();

private:
    /// What a picture was asked for with (``tick``).
    struct Asked {
        std::uint64_t run = 0;
        std::uint64_t revision = 0;
        bool changed = false;
        QSize size;
    };
    /// The picture ``tick`` asked for has come: deliver it, unless the sink
    /// stopped, started again or changed size meanwhile.
    void take_picture(const Asked& asked, QImage image);

    /// The size to draw at for the full size ``full_size_``: limited by
    /// ``max_height_``, then as the transport can carry it.
    [[nodiscard]] QSize wanted_size() const;
    /// A live sink moves to ``wanted_size`` when that changed.
    void follow_size();
    /// The keepalive timer runs while live and sending pictures.
    void sync_timer();

    QTimer* timer_ = nullptr;
    bool live_ = false;
    bool dirty_ = true;
    QString target_;
    QString last_error_;
    QSize size_{geom::k_reference_canvas.width, geom::k_reference_canvas.height};
    /// What it would draw at in full: the size it was started at, then each
    /// presented frame's canvas.
    QSize full_size_{geom::k_reference_canvas.width, geom::k_reference_canvas.height};
    int max_height_ = 0;
    bool video_ = true;

    /// Presentation revision last delivered, so a sink knows the picture moved
    /// without keeping dirty state of its own.
    std::uint64_t seen_revision_ = 0;
    /// A picture is on its way (``tick``), and another tick came meanwhile.
    bool in_flight_ = false;
    bool again_ = false;
    /// Counts starts and stops, so a picture asked for before one is dropped.
    std::uint64_t run_ = 0;

    /// Used unless a shared one is installed; ``presenter_`` points at whichever
    /// is in force and is never null.
    std::unique_ptr<ScenePresenter> own_presenter_;
    ScenePresenter* presenter_ = nullptr;
};

}  // namespace seventhslide::render
