#pragma once

// The web pages on any side of any stack, each drawn by its own renderer, and
// their latest pictures for the outputs and the operator's previews.
//
// SessionFacade owns one. After content changes (``sync``) a renderer is
// started for each tab newly shown (``StackRuntime::web_tab``, the page's
// ``SceneWebMedia::source``) and the renderers of tabs no side shows any more
// are stopped. A tab is one runtime's: two stacks showing one page, or a page
// cued beside itself live, are two renderers, each scrolled and followed on
// its own. Each renderer is made at the size its page is laid out for, cued
// or live, so a cued page is laid out as it will show.
//
// Where a tab goes follows the side it is on:
// - on a Program: its pictures to the output host, which hands them to every
//   output's presenter, and its sound on its stack's mixer strip;
// - on a Preview: its pictures to the previews alone, its sound on its
//   stack's cue strip (``audio::cue_strip_id``).
// A take changes only that: the page keeps its scroll, history and anything
// typed. A tab that leaves a Program, for a Preview or for good, keeps its
// pictures on the outputs for the change it goes out with, as the outgoing
// side of that change paints them (SourceRouting).

#include "source_routing.hpp"
#include "web_renderer.hpp"

#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/session/session.hpp>

#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace seventhslide::audio {
class Mixer;
}

namespace seventhslide::app {

class WebSources : public QObject {
    Q_OBJECT

public:
    /// Pages' sound plays on ``mixer``'s stack strips; null leaves pages
    /// silent (tests). ``mixer`` must outlive this.
    WebSources(const session::Session& session, audio::Mixer* mixer, QObject* parent = nullptr);
    ~WebSources() override;

    /// How pages are drawn; without one, a page live shows a black plate.
    /// Renderers already running are kept.
    void set_factory(WebRendererFactory factory);
    [[nodiscard]] bool can_draw() const noexcept { return static_cast<bool>(factory_); }
    /// Where the pictures go; null detaches (tests, headless).
    void set_output_host(render::OutputWindowHost* host);

    /// Start a renderer for every tab on a side of a stack, stop the rest,
    /// and route each by its side.
    void sync();

    /// Every tab's latest picture, live or cued, by source: what the previews
    /// paint.
    [[nodiscard]] const render::SourceFrames& frames() const noexcept
    {
        return routing_.frames();
    }
    /// What the outputs are given: the pictures of the tabs on a Program, and
    /// of those still going out with a change.
    [[nodiscard]] const render::SourceFrames& output_frames() const noexcept
    {
        return routing_.output_frames();
    }
    /// ``source``'s renderer; null when no side shows that tab.
    [[nodiscard]] WebRenderer* renderer(std::string_view source) const;
    /// Number of renderers running (tests).
    [[nodiscard]] std::size_t count() const noexcept { return renderers_.size(); }
    /// The mixer strip ``source``'s sound plays on: its stack's, or its
    /// stack's cue strip while it is cued; empty when none.
    [[nodiscard]] std::string sound_strip(std::string_view source) const
    {
        return routing_.sound_strip(source);
    }

signals:
    /// A page drew a new picture, or a page started or stopped.
    void framesChanged();
    /// A page started, stopped, drew its first picture, followed a link (or
    /// went back), or its ``problem()`` changed.
    void stateChanged();

private:
    void take_frame(const std::string& source);

    const session::Session& session_;
    WebRendererFactory factory_;
    std::map<std::string, std::unique_ptr<WebRenderer>, std::less<>> renderers_;
    /// After the renderers, so it takes their sounds off the mixer before
    /// they go.
    SourceRouting routing_;
};

} // namespace seventhslide::app
