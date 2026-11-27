#pragma once

// What the enabled destinations are drawn on: one audience window per
// display or desktop window destination, one OffscreenSink per NDI, Blackmagic or virtual
// camera destination, and the ScenePresenter each output's surfaces share -
// the operator's in-app preview of the output among them. Frames are
// presented here and fan out to every surface of their output.
//
// Knows destinations only as they are opened; which ones should be open, and
// on what, is OutputWindowHost's to decide.
// Seam: Qt Gui. Call on the GUI thread.

#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/render/destination.hpp"
#include "seventhslide/render/offscreen_sink.hpp"
#include "seventhslide/render/source_frames.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QScreen;

namespace seventhslide::render {

class OutputWindow;
class ScenePresenter;

class OutputSurfaces final : public QObject {
    Q_OBJECT

public:
    explicit OutputSurfaces(QObject* parent = nullptr);
    ~OutputSurfaces() override;

    /// Show ``dest`` full screen on ``screen``, at its size: move its window
    /// there, or open one.
    void show_window(const Destination& dest, const QString& output_name, QScreen* screen);
    /// Show ``dest`` as a window on the desktop at ``geometry`` (logical),
    /// unless its window is open already: that stays where the operator put
    /// it.
    void show_desktop_window(const Destination& dest, const QString& output_name,
                             QRect geometry);
    /// Hand ``sink`` the output's presentation and ``dest``'s format, and
    /// start it at ``size`` (the output's, before its limit). Takes
    /// ownership; on failure deletes it, sets ``error`` to the sink's own and
    /// returns false.
    [[nodiscard]] bool start_sink(const Destination& dest, OffscreenSink* sink, QSize size,
                                  QString& error);
    /// Give ``dest``'s open window or sink its format (``max_height``,
    /// ``video``) now, without reopening it.
    void apply_format(const Destination& dest);
    /// Close ``dest_id``'s window. False when it had none.
    bool close_window(const QString& dest_id);
    /// Stop and delete ``dest_id``'s sink. False when it had none.
    bool close_sink(const QString& dest_id);
    /// Close every window and sink, and drop every presentation no preview
    /// holds.
    void close_all();

    [[nodiscard]] bool has_window(const QString& dest_id) const;
    [[nodiscard]] bool has_sink(const QString& dest_id) const;
    [[nodiscard]] OffscreenSink* sink(const QString& dest_id) const;
    /// ``dest`` is drawing: its window is open, or its sink is live.
    [[nodiscard]] bool is_live(const Destination& dest) const;
    /// Something draws ``output_id``: a window, or a live sink.
    [[nodiscard]] bool is_open(const QString& output_id) const;
    /// The window of a destination of ``output_id``; null when none is open.
    [[nodiscard]] OutputWindow* window_for_output(const QString& output_id) const;
    /// Screen key -> output id, for every open window over a screen.
    [[nodiscard]] QHash<QString, QString> open_screens() const;

    /// Retitle the windows; ``label`` gives an output id's display name.
    void retitle(const std::function<QString(const QString&)>& label);
    /// Close the windows over the screen ``screen_key`` names (it went
    /// away). A desktop window stays: the system moves it to another.
    void close_windows_on(const QString& screen_key);
    /// Move each window back onto the screen its key names, if it is there.
    /// Desktop windows stay where they are.
    void refit();
    /// Fit each window over ``screen`` to it again (its size or place
    /// changed), and give the ids of their destinations. Not desktop windows.
    QStringList refit_on(QScreen* screen);
    /// Each window above everything, or an ordinary one, by whether it shares
    /// the operator's screen (OutputWindow::apply_stacking).
    void restack();
    /// Drop the last frame and the presentation of a deleted output (a held
    /// one goes with its last release).
    void forget_output(const QString& output_id);

    /// The presentation of ``output_id`` for an in-app preview of it, kept
    /// until the matching ``release_presenter``. The preview draws from the
    /// presenter the output's destinations share, so it shows what they show,
    /// a transition cut short included. Holding it also makes the output
    /// present when it has no destination. The presenter lives until the last
    /// release or until this object goes, whichever is first.
    [[nodiscard]] ScenePresenter* hold_presenter(const QString& output_id);
    void release_presenter(const QString& output_id);

    /// Native canvas size of the last frame of ``output_id``; the reference
    /// canvas before the first.
    [[nodiscard]] QSize canvas_size_for(const QString& output_id) const;
    /// ``canvas_size_for``, clamped to what a virtual camera accepts.
    [[nodiscard]] QSize camera_size_for(const QString& output_id) const;

    void present(const QString& output_id, scene::SharedSceneFrame frame);
    void present_all(const std::vector<scene::SharedSceneFrame>& frames);
    void prefetch_static_urls(const std::vector<std::string>& urls, int edge);

    void set_static_root(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& static_root() const noexcept
    {
        return static_root_;
    }
    /// Pictures arrived: every output drawn without one of them draws again.
    void forget_missing_pictures(const std::vector<std::string>& urls);
    void set_background_video_frame(QImage frame);
    /// ``provider``'s sources' latest pictures (``"content"``, ``"web"``,
    /// ``"live"``, ``"video"``): each provider hands in its own, and every
    /// output is given them all.
    void set_source_frames(std::string_view provider, const SourceFrames& frames);
    void set_font_faces(const layout::QtFontFaces* faces);

signals:
    /// Escape on a destination's window, or a close not this object's.
    void closeRequested(const QString& destination_id);
    /// A desktop window was moved or resized (OutputWindow::placeChanged).
    void windowPlaceChanged(const QString& destination_id, QRect geometry);
    /// What ``destination_id``'s sink reports changed on its own.
    void sinkStatusChanged(const QString& destination_id);

private:
    struct Sink {
        OffscreenSink* sink = nullptr;
        QString output_id;
    };

    /**
     * The presentation every destination on ``output_id`` draws from, created
     * on first use: audience windows, NDI, virtual cameras, Blackmagic.
     *
     * Sharing it is what keeps two NDI feeds of one output from compositing the
     * same scene twice and decoding the same background images twice - and what
     * keeps a window and an NDI receiver agreeing on whether a transition runs.
     */
    [[nodiscard]] ScenePresenter* presenter_for(const QString& output_id);
    /// Drop the presentation for ``output_id`` once nothing draws from it and
    /// no preview holds it.
    void release_presenter_if_unused(const QString& output_id);
    [[nodiscard]] bool has_output_window(const QString& output_id) const;
    /// Live offscreen sinks (every type) whose destination feeds ``output_id``.
    [[nodiscard]] QVector<OffscreenSink*> live_sinks(const QString& output_id) const;
    /// Stop ``win`` drawing and delete it once control is back in the event
    /// loop (see the definition for why never at once).
    void retire_window(OutputWindow* win);
    /// Keep the computer awake while anything presents (macOS; elsewhere
    /// nothing yet). Call after a window or sink opens or closes.
    void update_presenting() const;

    QHash<QString, OutputWindow*> windows_;  // dest id -> window
    /// Windows handed to deleteLater; the destructor deletes any still pending.
    QVector<QPointer<OutputWindow>> retiring_;
    QHash<QString, Sink> sinks_;  // dest id -> vcam / NDI / Blackmagic sink
    QHash<QString, ScenePresenter*> presenters_;  // output id -> shared presentation
    QHash<QString, int> holds_;  // output id -> previews holding its presenter
    QHash<QString, scene::SharedSceneFrame> last_frames_;  // output id -> last present
    QImage last_bg_video_frame_;
    /// Each provider's pictures, and all of them together.
    std::map<std::string, SourceFrames, std::less<>> provided_frames_;
    SourceFrames last_source_frames_;
    std::filesystem::path static_root_;
    /// Every output's presenter draws pictures from this one cache, so a
    /// picture shown on several is decoded once.
    std::shared_ptr<BackgroundImageCache> pictures_ = std::make_shared<BackgroundImageCache>();
    const layout::QtFontFaces* font_faces_ = nullptr;
};

}  // namespace seventhslide::render
