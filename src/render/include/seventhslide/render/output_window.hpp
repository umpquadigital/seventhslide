#pragma once

// Audience window for one output's destination: full screen for a display,
// or an ordinary window on the desktop, which the operator moves and resizes.
//
// It draws from the output's shared ScenePresenter through a PresenterView,
// exactly as NDI, Blackmagic and virtual camera destinations do: one
// composite per size for every destination on the output, one image cache,
// one decision about whether a transition runs.
//
// A QQuickWindow rather than a QRasterWindow: on Linux a sibling raster window
// next to the operator's RHI QQuickWindow often maps and stays black. It is
// created only after the QML engine exists.

#include "seventhslide/scene/scene_frame.hpp"

#include <QColor>
#include <QImage>
#include <QQuickWindow>
#include <QRect>
#include <QString>


class QScreen;

namespace seventhslide::render {

class PresenterView;
class ScenePresenter;

class OutputWindow final : public QQuickWindow {
    Q_OBJECT

public:
    /// ``output_id`` is the output it shows (``OutputConfig::id``);
    /// ``output_name`` only titles the window.
    OutputWindow(QString output_id, QString output_name, QScreen* screen,
                 QString destination_id);
    /// A window on the desktop with the system's frame, at ``geometry``
    /// (logical, its contents without the frame).
    OutputWindow(QString output_id, QString output_name, QRect geometry, QString destination_id);
    ~OutputWindow() override;

    /// An ordinary window on the desktop rather than over a whole screen: it
    /// claims no screen, and is never moved or stacked by its host.
    [[nodiscard]] bool desktop() const noexcept { return desktop_; }

    [[nodiscard]] QString output_id() const { return output_id_; }
    /// Retitle after the output is renamed.
    void set_output_name(QString name);
    /// The screen it covers; empty for a desktop window.
    [[nodiscard]] QString screen_key() const { return screen_key_; }

    /// Place fullscreen on ``screen`` (exit fullscreen -> move -> re-enter).
    void move_to_screen(QScreen* screen);

    /// Show it covering its screen. Elsewhere the system's full screen; on
    /// macOS a borderless window over the whole screen, above the menu bar
    /// and the Dock, as its full screen would give the output a Space of its
    /// own (with a slide in, and with Displays have separate Spaces off,
    /// every other screen blacked out).
    void show_full_screen();
    /// Show a desktop window where it was placed, without taking the keys
    /// from the operator's window (a click on it does).
    void show_on_desktop();
    /// macOS: over the menu bar and the Dock on an audience screen, an
    /// ordinary window on the operator's (DeviceCatalog::operator_screen).
    /// Again when either window changes screen. Elsewhere, and for a desktop
    /// window, nothing.
    void apply_stacking();

    /// Let it close: the host is taking it down. Until then a close (Alt+F4,
    /// the close button, the system closing it) is turned into
    /// ``closeRequested``, so the host knows it went (see
    /// OutputWindowHost::is_closed) rather than holding a window that is not
    /// there.
    void allow_close() noexcept { may_close_ = true; }

    /// Shared with NDI / vcam / Blackmagic on this output. Not owned.
    void set_presenter(ScenePresenter* presenter);
    /// Its presenter was given a new frame: take the title and letterbox
    /// colour from it and show the new picture without waiting a tick.
    void presentation_changed();
    [[nodiscard]] ScenePresenter* presenter() const noexcept;

    /// Draw no taller than ``max_height`` device pixels (0: the screen's
    /// own), and stretch that to fill the screen.
    void set_max_height(int max_height);

    /// What the window shows, composited at the size it draws at.
    [[nodiscard]] QImage snapshot();

signals:
    /// Escape, or a close not the host's - the host closes it, and leaves
    /// its destination on.
    void closeRequested(const QString& destination_id);
    /// A desktop window was moved or resized to ``geometry`` (logical, as
    /// the constructor takes it).
    void placeChanged(const QString& destination_id, QRect geometry);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void exposeEvent(QExposeEvent* event) override;

private:
    /// The frame the presenter is showing, if any.
    [[nodiscard]] const scene::SceneFrame* current_frame() const noexcept;
    void apply_letterbox_color();
    void update_title();
    void sync_view_geometry();
    [[nodiscard]] QSize logical_size() const;

    QString output_id_;
    QString output_name_;
    QString destination_id_;
    QString screen_key_;
    QColor letterbox_{Qt::black};
    PresenterView* view_ = nullptr;
    bool may_close_ = false;
    bool desktop_ = false;
};

}  // namespace seventhslide::render
