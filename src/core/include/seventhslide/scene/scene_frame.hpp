#pragma once

// Audience display contract: the SceneFrame every output paints from.
// Plain C++20 (no Qt).

#include "seventhslide/canvas/box.hpp"
#include "seventhslide/canvas/object.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/geom/shape_path.hpp"
#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/model/content_kind.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/model/transition.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::scene {

/// Content mode for one frame (mirrors live packet ``mode``, without HTML).
enum class SceneMode {
    Empty,
    Song,
    Bible,
    Freeform,
    Image,
    Video,
    /// Sound only: the layer paints nothing, so the stacks behind show through.
    Audio,
    /// A web page, drawn off screen by the app and fitted like an image.
    Web,
    /// A live source (a camera), received by the app and placed like a video.
    Live,
};

[[nodiscard]] std::string_view to_string(SceneMode mode) noexcept;

/// Paint mode. An image folder paints as an image; a divider paints nothing.
[[nodiscard]] constexpr SceneMode scene_mode_for(model::ContentKind kind) noexcept
{
    switch (kind) {
    case model::ContentKind::Song:
        return SceneMode::Song;
    case model::ContentKind::Bible:
        return SceneMode::Bible;
    case model::ContentKind::Freeform:
        return SceneMode::Freeform;
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
        return SceneMode::Image;
    case model::ContentKind::Video:
        return SceneMode::Video;
    case model::ContentKind::Audio:
        return SceneMode::Audio;
    case model::ContentKind::Web:
        return SceneMode::Web;
    case model::ContentKind::Live:
        return SceneMode::Live;
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        return SceneMode::Empty;
    }
    return SceneMode::Empty;
}

/// Background after cascade + per-slide title/bg override (paint layer).
struct SceneBackground {
    layout::BackgroundType type = layout::BackgroundType::Transparent;
    std::string color{layout::k_default_fill_color};
    std::string image;
    /// ``/static/...`` URL when ``type`` is ``Video``. Looping muted bg.
    std::string video;

    [[nodiscard]] bool operator==(const SceneBackground&) const noexcept = default;
};

/// Geometry + typography for the primary lyrics / bible text box.
using SceneTextBoxStyle = canvas::TextBox;

/// Follow-lines role for one visual line.
enum class SceneLineRole {
    Normal,
    Active,
    Follow,
};

/// Where a text line's pieces sit, measured once when the frame is built
/// (``measure_text_content``) so paint only draws.
struct SceneLineMetrics {
    /// Painted width of the line.
    double width = 0.0;
    /// Height of the line box.
    double height = 0.0;
    /// Baseline from the top of the line box: the lyric row of a chord line.
    double baseline = 0.0;
    /// Chord lines: the chord row's baseline from the top of the line box.
    double chord_baseline = 0.0;
    /// Left edge of each run (or, for a chord line, each chord item), from
    /// the line's left edge.
    std::vector<double> x;
    /// Chord lines: left edge of a hyphen in each word a chord split open.
    std::vector<double> hyphens;

    [[nodiscard]] bool operator==(const SceneLineMetrics&) const noexcept = default;
};

/**
 * One visual line inside the primary text box.
 *
 * Prefer ``runs`` for plain / rich lyrics and bible. When ``has_chords``, painters
 * use ``chords`` (structured cells) instead of re-parsing markup.
 */
struct SceneTextLine {
    model::RichLine runs;
    SceneLineRole role = SceneLineRole::Normal;
    /// The type this line is set in where it differs from the box's
    /// (follow-lines' active line and other lines); painted as
    /// ``canvas::painted_type(box, type)``.
    canvas::LineType type;
    /// Verse-gap / packing spacer above this line (px).
    int margin_top_px = 0;
    /// True when this visual row continues the previous logical line (soft wrap).
    bool wrap_continue = false;
    /// True when it does so inside a word cut at the box's edge: no space
    /// between the two when they are read back as text.
    bool wrap_in_word = false;
    /// True when it does so at a break layout chose for balance, which text
    /// read back has to keep: the box's width alone would not make it.
    bool wrap_kept = false;
    layout::ChordLine chords;
    bool has_chords = false;
    /// Filled when the frame is built; paint measures a line that has none.
    std::optional<SceneLineMetrics> metrics;

    [[nodiscard]] bool operator==(const SceneTextLine&) const noexcept = default;
};

struct SceneTextContent {
    SceneTextBoxStyle box;
    /// Its place in the frame's stack, among the objects (``paint_order``):
    /// the z of the theme element it is laid out in.
    int z = 0;
    /// How the box's chord lines draw their chords.
    layout::ChordStyle chords;
    std::vector<SceneTextLine> lines;

    [[nodiscard]] bool empty() const noexcept { return lines.empty(); }
    [[nodiscard]] bool operator==(const SceneTextContent&) const noexcept = default;
};

/// Where the theme places media on the canvas. ``enabled`` false: the theme
/// has no area for it. A width or height that is not positive means the
/// whole canvas on that axis (see on_canvas).
struct SceneMediaArea {
    bool enabled = true;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    /// Its place in the frame's stack, among the objects (``paint_order``):
    /// the z of the theme's area element.
    int z = 0;

    /// This area on a ``canvas_width`` x ``canvas_height`` frame: an unset
    /// (not positive) width or height becomes the canvas's.
    [[nodiscard]] SceneMediaArea on_canvas(int canvas_width, int canvas_height) const noexcept
    {
        SceneMediaArea out = *this;
        if (out.width <= 0) {
            out.width = canvas_width;
        }
        if (out.height <= 0) {
            out.height = canvas_height;
        }
        return out;
    }

    [[nodiscard]] bool operator==(const SceneMediaArea&) const noexcept = default;
};

struct SceneImageMedia {
    std::string url;
    int index = 0;
    int count = 0;
    SceneMediaArea area;
    /// CSS ``object-fit`` keyword: ``contain``, ``cover``, ``fill``, ...
    std::string fit = "contain";

    [[nodiscard]] bool operator==(const SceneImageMedia&) const noexcept = default;
};

/// A field added here is one render's ``paints_same`` (fade.cpp) has to be
/// told of: whether a change to it is new content. It does not compile until
/// then.
struct SceneVideoMedia {
    /// The source its pictures and clock come by: the player of this one
    /// item (``content_video_key``), whatever else shows the same file.
    std::string source;
    std::string url;
    bool loop = false;
    bool autoplay = true;
    double position_sec = 0.0;
    SceneMediaArea area;

    [[nodiscard]] bool operator==(const SceneVideoMedia&) const noexcept = default;
};

/// A web page on the canvas. The page itself is drawn off screen by the app
/// (one renderer per ``source``), which hands its frames to whatever paints
/// this frame; nothing here loads it.
struct SceneWebMedia {
    /// The renderer this page is drawn by: its runtime's tab
    /// (``web_tab_key``), which every output showing that runtime shares and
    /// no other runtime does.
    std::string source;
    std::string url;
    /// The size the page is laid out at, in pixels, before it is fitted.
    int width = 0;
    int height = 0;
    double zoom = 1.0;
    SceneMediaArea area;
    /// CSS ``object-fit`` keyword, as for an image.
    std::string fit = "contain";

    [[nodiscard]] bool operator==(const SceneWebMedia&) const noexcept = default;
};

/// A live source on the canvas, placed by the theme's Video area and
/// contained. Its pictures come from the app's receiver for ``source``.
struct SceneLiveMedia {
    /// ``model::live_source_key``: ``ndi:HOST (Camera 1)``.
    std::string source;
    std::string name;
    SceneMediaArea area;

    [[nodiscard]] bool operator==(const SceneLiveMedia&) const noexcept = default;
};

/// What a design preview shows in a media area for what plays there at
/// showtime (``build_theme_draft_frame``).
struct SceneMediaStandIn {
    /// Colour bars across the whole area, fitted to its size and shape (a
    /// picture's or a video's area); without them, the output's black plate.
    bool bars = false;
    /// Which media the area is for, in its middle.
    layout::OverlayBox label;

    [[nodiscard]] bool operator==(const SceneMediaStandIn&) const noexcept = default;
};

/// Which page ``url`` is, drawn at ``width`` x ``height`` and ``zoom``: two
/// with the same key are the same page (a page sent again keeps its tab by
/// it; a web thumbnail is filed by it).
[[nodiscard]] std::string web_source_key(std::string_view url, int width, int height, double zoom);
/// ``SceneWebMedia::source`` for the page drawn in tab ``tab``
/// (``StackRuntime::web_tab``).
[[nodiscard]] std::string web_tab_key(std::uint64_t tab);

/// The source a video object's pictures come by (4.7's 6): one player for
/// the clip at ``url``, whichever objects, decks and outputs show it, so a
/// deck copied to another output is not heard twice.
[[nodiscard]] std::string video_object_key(std::string_view url);
/// The source a video item's pictures come by: its player's, one per item
/// (``StackRuntime::media_instance``), so two stacks showing videos each show
/// their own, and an item going plays out beside the one coming.
[[nodiscard]] std::string content_video_key(std::uint64_t media_instance);

using canvas::ClockMode;

/// An object's build in the change into its frame: what it does,
/// and when, from the change's start. An exit's object is on the frame for
/// it, and gone once it has played.
struct SceneBuild {
    canvas::AnimationEffect effect = canvas::AnimationEffect::Fade;
    model::TransitionDirection direction = model::TransitionDirection::Left;
    int start_ms = 0;
    int duration_ms = 0;

    [[nodiscard]] canvas::AnimationKind kind() const noexcept
    {
        return canvas::animation_kind(effect);
    }
    [[nodiscard]] bool operator==(const SceneBuild&) const noexcept = default;
};

/// A canvas object on the frame: a laid-out text box, or a picture, video,
/// shape or clock as it paints. Not title-slide overlay chrome.
struct SceneObject {
    int z = 0;
    std::string id;
    model::Transition transition;
    /// Its builds in the change into this frame, in the order they start;
    /// none when it simply shows.
    std::vector<SceneBuild> builds;
    /// A video object whose video runs: one that starts on show, or on a
    /// click that has come.
    bool plays = false;
    std::optional<layout::OverlayBox> text;
    std::optional<canvas::Object> object;

    /// Its last build takes it off: once the change is over, it is gone.
    [[nodiscard]] bool exits() const noexcept
    {
        return !builds.empty() && builds.back().kind() == canvas::AnimationKind::Exit;
    }
    [[nodiscard]] bool operator==(const SceneObject&) const noexcept = default;
};

struct SceneFrame;

/// A composed frame, read-only, shared by everything that shows it: the
/// present pass, each output's presenter (each of its stacks a layer there),
/// the host's copy for destinations that open later, and the operator
/// previews. A frame is never changed once composed, so they share one rather
/// than copying it.
using SharedSceneFrame = std::shared_ptr<const SceneFrame>;

/// The stacks under a frame's, back to front, each its stack's frame as
/// composed. No entry is ever null: each is a frame from ``share_frame``.
/// Two are equal when their frames are, shared or not.
struct StackedFrames : std::vector<SharedSceneFrame> {
    using std::vector<SharedSceneFrame>::vector;

    [[nodiscard]] bool operator==(const StackedFrames& other) const;
};

/**
 * One output's current paint state.
 *
 * Not a slide deck: the scene builder selects the active index from
 * ``OutputRuntime`` and emits one frame. Freeze holds a prior ``SceneFrame``.
 *
 * A field added here is one render's ``paints_same`` (fade.cpp) has to be
 * told of: whether a change to it is new content. It does not compile until
 * then.
 */
struct SceneFrame {
    /// ``OutputConfig::id``: survives a rename, so state held per output
    /// (a freeze) follows the output rather than its name.
    std::string output_id;
    /// ``OutputConfig::name``: what windows and the operator UI address.
    std::string output_name;
    int canvas_width = geom::k_reference_canvas.width;
    int canvas_height = geom::k_reference_canvas.height;
    SceneMode mode = SceneMode::Empty;
    int slide_index = 0;
    /// The stack this frame is the picture of. Each stack on an output
    /// changes by its own transition, so the presenter follows a
    /// stack from one frame to the next by this.
    std::string stack_id;
    std::string transition_key;

    bool is_blank = false;
    bool is_frozen = false;

    SceneBackground background;
    /// Primary lyrics / bible box; nullopt when overlay-only, media, empty, or blank.
    std::optional<SceneTextContent> text;
    /// Title-slide overlay boxes.
    layout::OverlayScene overlay;
    /// Canvas objects for the active slide: the theme's elements (copyright,
    /// reference, indicator, clocks, shapes, images) and a Freeform deck's own.
    std::vector<SceneObject> objects;

    std::optional<SceneImageMedia> image;
    std::optional<SceneVideoMedia> video;
    std::optional<SceneWebMedia> web;
    std::optional<SceneLiveMedia> live;
    /// What stands in the media area for what plays there at showtime,
    /// painted at the area's place in the stack: over the output's plate,
    /// colour bars or none, then its label. Only a theme design preview has
    /// one (``build_theme_draft_frame``), never a frame that plays.
    std::optional<SceneMediaStandIn> media_stand_in;

    /// Lower stacks, back-to-front. Empty for a single occupied stack.
    StackedFrames stacked_below;

    /// How the body (lyrics, passage, overlay) changes to this frame.
    model::Transition content_transition;
    /// When the last of its objects' builds is done, from the change's start
    /// (0: none). The change runs this long, or its transition's length.
    int builds_end_ms = 0;
    /// How the background changes to this frame.
    model::Transition background_transition;

    [[nodiscard]] bool operator==(const SceneFrame&) const = default;
};

[[nodiscard]] inline SharedSceneFrame share_frame(SceneFrame frame)
{
    return std::make_shared<const SceneFrame>(std::move(frame));
}

inline bool StackedFrames::operator==(const StackedFrames& other) const
{
    return std::equal(begin(), end(), other.begin(), other.end(),
                      [](const SharedSceneFrame& a, const SharedSceneFrame& b) {
                          return a == b || *a == *b;
                      });
}

/// Add to ``urls`` the library files ``frame`` paints from, as ``/static/...``
/// URLs: its background, its picture or video, its objects' and their fills',
/// and the frames below it.
void note_frame_media(const SceneFrame& frame, std::set<std::string>& urls);

}  // namespace seventhslide::scene
