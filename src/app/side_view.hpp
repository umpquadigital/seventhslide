#pragma once

// One side of the focused stack as a well of the Controller shows it and
// works it: its lines or pictures, its video or audio and their clock, a web
// page, a live source; and the commands that move within that side alone.
// QML: ``session.program`` (what is live) and ``session.preview`` (what is
// cued, on a stack with a Preview). The web remote reads the Program's,
// under the tab its device shows (session_remote_bridge.cpp).
//
// What goes live, the service selection and the outputs' blank and freeze
// are LiveController's; a stack's own blank and freeze are StackController's.
// A change announces liveChanged, as theirs do, and the application follows.

#include "line_map_model.hpp"

#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include <memory>

namespace seventhslide::app {

class LiveSources;
class WebRenderer;
class WebSources;

class SideView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session makes one for each side a well shows")

    /// It shows a Preview (cued), not a Program (live).
    Q_PROPERTY(bool preview READ preview CONSTANT)
    /// The side is there (the focused stack has it) and holds something.
    Q_PROPERTY(bool occupied READ occupied NOTIFY focusedStackChanged)
    /// The rundown item the side shows (``ServiceItem::item_id``); -1 for
    /// library content, or nothing.
    Q_PROPERTY(qint64 serviceItemId READ serviceItemId NOTIFY focusedStackChanged)
    /// The side's lines (or an image folder's pictures), as the well lists them.
    Q_PROPERTY(seventhslide::app::LineMapModel* lines READ lines CONSTANT)
    Q_PROPERTY(int lineCursor READ lineCursor NOTIFY focusedStackChanged)
    Q_PROPERTY(int totalLines READ totalLines NOTIFY focusedStackChanged)
    /// The build step on the current slide, from 0.
    Q_PROPERTY(int currentStep READ currentStep NOTIFY focusedStackChanged)
    Q_PROPERTY(bool imageMode READ imageMode NOTIFY focusedStackChanged)
    /// A video: there is a picture to preview.
    Q_PROPERTY(bool videoMode READ videoMode NOTIFY mediaStateChanged)
    /// Video or audio: the transport drives it.
    Q_PROPERTY(bool mediaTransport READ mediaTransport NOTIFY mediaStateChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY mediaStateChanged)
    /// The video or audio clock (seconds); 0 with none.
    Q_PROPERTY(double mediaPositionSec READ mediaPositionSec NOTIFY mediaPositionChanged)
    Q_PROPERTY(double mediaDurationSec READ mediaDurationSec NOTIFY mediaStateChanged)
    /// The image-playlist file at the current index (empty outside image mode).
    Q_PROPERTY(QString currentImageFilename READ currentImageFilename NOTIFY focusedStackChanged)
    /// Simple Freeform: the well shows slide thumbs.
    Q_PROPERTY(bool freeformGallery READ freeformGallery NOTIFY focusedStackChanged)
    /// A web page.
    Q_PROPERTY(bool webMode READ webMode NOTIFY focusedStackChanged)
    /// Its renderer: another page changes it, and the address bar drops what
    /// was typed for the page before.
    Q_PROPERTY(QString webSource READ webSource NOTIFY focusedStackChanged)
    /// Why the page is not showing; empty when it is, or outside web mode.
    Q_PROPERTY(QString webProblem READ webProblem NOTIFY webStateChanged)
    /// The page has not drawn its first picture yet.
    Q_PROPERTY(bool webLoading READ webLoading NOTIFY webStateChanged)
    /// The address the page is on now (after any link followed); empty
    /// outside web mode.
    Q_PROPERTY(QString webUrl READ webUrl NOTIFY webStateChanged)
    /// The page has a page to go back to.
    Q_PROPERTY(bool webCanGoBack READ webCanGoBack NOTIFY webStateChanged)
    /// A live source (a camera).
    Q_PROPERTY(bool liveSourceMode READ liveSourceMode NOTIFY focusedStackChanged)
    /// Why it is not showing (waiting for it on the network); empty when it is.
    Q_PROPERTY(QString liveSourceProblem READ liveSourceProblem NOTIFY liveSourceStateChanged)
    /// It has sent no picture for a moment: the outputs hold its last.
    Q_PROPERTY(bool liveSourceLost READ liveSourceLost NOTIFY liveSourceStateChanged)

public:
    SideView(session::Session& session, session::StackSide side, QObject* parent = nullptr);
    ~SideView() override;

    [[nodiscard]] session::StackSide side() const noexcept { return side_; }
    [[nodiscard]] bool preview() const noexcept { return side_ == session::StackSide::Preview; }
    [[nodiscard]] bool occupied() const;
    /// The runtime it shows: ``side`` of the focused stack (under the web
    /// remote's ``run_in_view``, of the view's). Null for a Preview the
    /// stack does not have.
    [[nodiscard]] const session::StackRuntime* runtime() const;

    [[nodiscard]] LineMapModel* lines() const noexcept { return lines_.get(); }
    [[nodiscard]] qint64 serviceItemId() const;
    [[nodiscard]] int lineCursor() const;
    [[nodiscard]] int totalLines() const;
    [[nodiscard]] int currentStep() const;
    /// How many build steps the current slide has.
    [[nodiscard]] int step_count() const;
    [[nodiscard]] bool imageMode() const;
    [[nodiscard]] bool videoMode() const;
    [[nodiscard]] bool mediaTransport() const;
    [[nodiscard]] bool mediaPlaying() const;
    [[nodiscard]] double mediaPositionSec() const;
    [[nodiscard]] double mediaDurationSec() const;
    [[nodiscard]] QString currentImageFilename() const;
    [[nodiscard]] bool freeformGallery() const;
    /// Changes whenever the side's built slides do (``StackRuntime::build_id``):
    /// what keys its slides' thumbnails. 0 with nothing built.
    [[nodiscard]] quint64 build_id() const;

    [[nodiscard]] bool webMode() const;
    /// The page's renderer (``SceneWebMedia::source``); empty outside web mode.
    [[nodiscard]] QString webSource() const;
    [[nodiscard]] QString webProblem() const;
    [[nodiscard]] bool webLoading() const;
    [[nodiscard]] QString webUrl() const;
    [[nodiscard]] bool webCanGoBack() const;
    /// Where the pages are drawn; set once by the facade.
    void set_web_sources(WebSources* sources);

    [[nodiscard]] bool liveSourceMode() const;
    /// The source's key (``ndi:HOST (Camera 1)``); empty outside live source
    /// mode.
    [[nodiscard]] QString live_source_key() const;
    [[nodiscard]] QString liveSourceProblem() const;
    [[nodiscard]] bool liveSourceLost() const;
    /// Where live sources are received; set once by the facade.
    void set_live_sources(LiveSources* sources);

    // --- moving within the side ---
    Q_INVOKABLE bool nextSlide();
    Q_INVOKABLE bool prevSlide();
    Q_INVOKABLE bool jumpToLine(int line_index);
    /// ``line_index`` at build step ``step``, shown as built.
    Q_INVOKABLE bool jumpToStep(int line_index, int step);

    // --- its video or audio. False when it has none. ---
    Q_INVOKABLE bool mediaPlay();
    Q_INVOKABLE bool mediaPause();
    Q_INVOKABLE bool mediaSeek(double position_sec);
    Q_INVOKABLE bool mediaRestart();

    // --- its web page ---
    /// Load the page again. False outside web mode.
    Q_INVOKABLE bool reloadWeb();
    /// Take the page back to the page before. False when there is none.
    Q_INVOKABLE bool webGoBack();
    /// Send the page to the address ``text`` (as the Browser tab takes one:
    /// ``example.com``, a full address, or a file's path), as a link followed
    /// there would: Previous comes back from it. Returns why it refused, or
    /// an empty string.
    Q_INVOKABLE QString webGoTo(const QString& text);

    /// The side's slides changed: list them again, or only what moved.
    void reload_lines();
    void notify_lines_moved();

signals:
    /// The application's signals of these names, forwarded: the properties
    /// above notify on them.
    void focusedStackChanged();
    void mediaStateChanged();
    /// The clock moved: every playback tick, a seek, and with every
    /// mediaStateChanged.
    void mediaPositionChanged();
    /// ``webProblem`` and the page's address may have changed.
    void webStateChanged();
    /// ``liveSourceProblem`` may have changed.
    void liveSourceStateChanged();
    /// The side's video or audio was sought: the picture held there is
    /// drawn again (the application follows; nothing went live).
    void sought();
    /// Something moved. ``rebuildModels``: rows were added or removed.
    void liveChanged(bool rebuildModels);
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);

private:
    [[nodiscard]] session::StackRuntime* runtime_mut();
    /// The page's renderer; null outside web mode, or when the page is not
    /// drawn.
    [[nodiscard]] WebRenderer* web_renderer() const;
    /// ``move`` on the side's runtime, as entry point ``entry``: false with
    /// no runtime, or when it refuses. Announces what moved.
    template <typename Move>
    bool act(const char* entry, Move move);

    session::Session& session_;
    session::StackSide side_;
    std::unique_ptr<LineMapModel> lines_;
    WebSources* web_sources_ = nullptr;
    LiveSources* live_sources_ = nullptr;
};

}  // namespace seventhslide::app
