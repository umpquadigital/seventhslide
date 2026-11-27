#pragma once

// Layout -> SceneFrame: structured paint frames from OutputRuntime.
// Plain C++20 (no Qt).

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/scene/chrome_text.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/theme/resolved_theme.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::scene {

/// Token values / media / transition carried beside the rebuild result.
struct SceneExtras {
    std::string transition_key;
    /// Token values for song text elements (``authors``, ``copyright``,
    /// ``ccli-number``, ``ccli-license``, ``song-title``, ``key``, ...). Empty
    /// only when no song is playing, which blanks every ``{-token}`` line.
    ///
    /// A song that opted out of its copyright still publishes these: the toggle
    /// hides the elements marked ``ShowWhen::SongCopyrightOn``, so a title
    /// design using ``{song-title}`` or ``{authors}`` for its own reasons is
    /// left alone. See ``song_copyright``.
    std::map<std::string, std::string> copyright_vars;
    /// The live song's "Display copyright on output" toggle. False when nothing
    /// or no song is playing.
    bool song_copyright = false;
    /// Token values for Bible reference elements. Empty when nothing biblical
    /// is playing.
    std::map<std::string, std::string> bible_ref_vars;
    std::optional<SceneImageMedia> image;
    std::optional<SceneVideoMedia> video;
    std::optional<SceneWebMedia> web;
    std::optional<SceneLiveMedia> live;
};

/**
 * Explicit inputs for one output's current frame.
 *
 * ``slide_index`` < 0 -> use ``runtime.index``. Canvas is structural (not part
 * of the resolved style), so callers pass it from ``OutputConfig``.
 * ``measurer`` enables box-wrap / highlight re-wrap when layout fragments are
 * missing or the active follow-line font size differs from the box font.
 */
struct SceneFrameRequest {
    const build::OutputRuntime& runtime;
    SceneMode mode = SceneMode::Empty;
    int canvas_width = geom::k_reference_canvas.width;
    int canvas_height = geom::k_reference_canvas.height;
    int slide_index = -1;
    SceneExtras extras;
    layout::ITextMeasurer* measurer = nullptr;
    /// < 0 -> first line mapped to ``slide_index``. Freeform indicators follow this cue.
    int line_index = -1;
    /// The slide's build step: what is shown, and, with
    /// ``animate_step``, what plays on the way in. A step past a slide's
    /// last shows it fully built.
    int step = 0;
    bool animate_step = false;
    /// The step changed within the slide: the slide does not come in again,
    /// only its builds play.
    bool within_slide = false;
    /// How the frame comes in, in place of what its theme or slide says, for
    /// the content and the background alike: a take's own transition.
    std::optional<model::Transition> transition;
};

[[nodiscard]] SceneMode scene_mode_from_build_kind(build::BuildKind kind) noexcept;

/**
 * Every ``{token}`` a themed text element can fill on this frame.
 *
 * Seeded empty for every built-in name, so a template line asking for something
 * the current item has not got (``{-authors}`` on a passage, ``{-songbook}`` on
 * a song with no songbook) drops that line.
 *
 * The per-song copyright toggle is *not* expressed here: it hides whole
 * elements (``theme::ShowWhen``), so the tokens stay filled for everything else
 * on the canvas that happens to use them.
 */
[[nodiscard]] std::map<std::string, std::string> frame_token_pool(const SceneExtras& extras);

/// What a design preview says in the box of a picture element with no picture
/// chosen, sized for ``canvas``.
struct PictureStandIn {
    std::string_view text;
    geom::CanvasSize canvas;
};

/**
 * The theme's own canvas objects for one frame.
 *
 * Bound elements are skipped: the body boxes and media rectangles were
 * projected onto the resolved output's typed fields before layout ran, because
 * the packer and the media painters need them long before a frame exists. What is left - text, shapes,
 * images, clocks, indicators - has any number of instances and paints as
 * ordinary scene objects, beside Freeform's own.
 *
 * ``skip_static_text`` leaves authored text boxes out: in playback the Titles
 * context paints those through the overlay, which layout consults. ``suppress_id``
 * drops one element - the theme designer paints the box it is editing itself.
 * ``conditions`` carries the live facts an element's ``show_when`` tests.
 *
 * A picture element with no picture chosen paints nothing. On a design
 * preview, ``picture_stand_in`` gives the label (``stand_in_label``) that
 * stands in its box instead, with its id and place in the stack, so it can be
 * seen and picked.
 */
[[nodiscard]] std::vector<SceneObject> build_theme_element_objects(
    const std::vector<theme::Element>& elements,
    int slide_index,
    int slide_count,
    const std::map<std::string, std::string>& tokens,
    const std::vector<IndicatorItem>& indicator_items = {},
    bool skip_static_text = false,
    std::string_view suppress_id = {},
    const theme::ElementConditions& conditions = {},
    const std::optional<PictureStandIn>& picture_stand_in = std::nullopt);

/// Put ``frame``'s picture, video, page and live source where ``theme`` puts
/// them: each one's area, its place in the stack included, and the fit of a
/// picture or page. A frame that plays and a design preview are placed by
/// this one function, so the designer shows the area the output paints.
void place_frame_media(SceneFrame& frame, const theme::ResolvedTheme& theme);

/// Grey words in the middle of ``at`` (canvas px), sized for ``canvas``: what
/// a design preview shows for something that exists only at showtime (what
/// plays in a media area, a picture not chosen yet).
[[nodiscard]] layout::OverlayBox stand_in_label(std::string_view text,
                                                const canvas::BoxRect& at,
                                                geom::CanvasSize canvas);

/// Background layer ``oc`` paints, or ``fill`` when a slide brings its own. A
/// type other than image / video carries neither path.
[[nodiscard]] SceneBackground background_of(
    const theme::ResolvedTheme& theme,
    const std::optional<layout::BackgroundFill>& fill = std::nullopt);

/// Lyrics text box of a resolved theme.
[[nodiscard]] SceneTextBoxStyle song_box(const theme::ResolvedTheme& theme);

/// Bible text box of a resolved theme.
[[nodiscard]] SceneTextBoxStyle bible_box(const theme::ResolvedTheme& theme);

/**
 * The painted rows of one song slide (``group_index`` into ``layout.groups``):
 * follow-line roles and colours, verse gaps, and re-wraps at the paint size.
 *
 * The single owner of song layout -> rows: the audience frame and the theme
 * designer's sample both go through here. An out-of-range group yields the box
 * with no lines; ``show_lyrics`` is the caller's decision.
 */
[[nodiscard]] SceneTextContent build_song_slide_text(const build::LayoutResult& layout,
                                                     const theme::ResolvedTheme& theme,
                                                     int group_index,
                                                     layout::ITextMeasurer* measurer);

/// Build one paint frame (song / bible / freeform / empty / blank).
[[nodiscard]] SceneFrame build_scene_frame(const SceneFrameRequest& req);

/// A video object a slide shows (4.7's 6): what the app plays it by.
struct SceneObjectVideo {
    /// ``video_object_key``: the source its pictures are handed out as.
    std::string key;
    /// In the media folder's videos.
    std::string filename;
    bool loop = false;
    bool mute = false;
    /// It runs (on show, or its click has come); else it waits on its
    /// first picture.
    bool plays = false;
    /// Which showing this is: another (the next slide with the clip, the
    /// item played again) starts it over. Set by the stack.
    std::uint64_t run = 0;

    /// ``other`` shows the same clip: it runs if either does, and is heard
    /// unless both are muted.
    void merge(const SceneObjectVideo& other) noexcept
    {
        plays = plays || other.plays;
        mute = mute && other.mute;
    }
    [[nodiscard]] bool operator==(const SceneObjectVideo&) const noexcept = default;
};

/// The video objects ``rt``'s slide ``slide_index`` shows at build ``step``.
[[nodiscard]] std::vector<SceneObjectVideo> object_videos_at(const build::OutputRuntime& rt,
                                                             int slide_index,
                                                             int step);

/// Clear content layers; keep background. Used by blank and by freeze+blank overlay.
void apply_scene_blank(SceneFrame& frame);

/**
 * Wrap one bible slide's text into the visual rows paint will draw.
 *
 * The single owner of bible text -> rows: the audience frame and the theme
 * editor's preview both go through here, so the box the editor shows is the box
 * the audience gets. ``citations`` is the slide's ``BibleSlide::citations``: the
 * markers rows budget for and style when verse numbers are shown. ``measurer``
 * may be null (no wrap - one row per newline).
 */
[[nodiscard]] SceneTextContent build_bible_text_content(std::string_view slide_text,
                                                        const std::vector<std::string>& citations,
                                                        const theme::ResolvedTheme& theme,
                                                        layout::ITextMeasurer* measurer);

/// The style ``theme`` paints a verse number in.
[[nodiscard]] model::TextStyle bible_citation_look(const theme::ResolvedTheme& theme);

}  // namespace seventhslide::scene
