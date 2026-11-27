#pragma once

// Theme designer draft preview: SceneFrame from in-memory style +
// fixture sample content. When a measurer is supplied, sample text is laid out
// with the same song/bible packing used for live slides (wrap + fit-to-box).
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::scene {

/// What the designer stage previews. A text theme's ``Song``, ``Bible``,
/// ``Media`` and ``Titles`` designs; ``Behavior``, the song design behind the
/// wrap and follow-lines controls; a background theme's fill (``Background``).
enum class DraftContext {
    Song,
    Bible,
    Image,
    Video,
    Web,
    Live,
    Behavior,
    Titles,
    Background,
};

/// The context QML names: a text theme context's name (``song``, ``bible``,
/// ``image``, ``video``, ``web``, ``live``, ``titles``),
/// ``behavior`` or ``bg``.
[[nodiscard]] std::optional<DraftContext> draft_context_from_string(
    std::string_view context) noexcept;

/// The design context ``context`` shows (``Behavior`` the Song's); none for
/// a background's fill.
[[nodiscard]] std::optional<theme::ElementContext> element_context_of(
    DraftContext context) noexcept;

/**
 * Build a paint frame for the theme designer stage.
 *
 * Text-theme callers should pass ``isolate_text_preview_style`` (transparent
 * surface). The ``Background`` context is the fill alone: no sample body, no chrome -
 * the clock included, since it is a text-theme element.
 *
 * Sample content is fixed fixtures (We Have This Hope / Revelation 14:6 NKJV),
 * not live service items. ``Behavior`` uses a follow-lines song sample so
 * highlight/dim show up.
 *
 * When ``measurer`` is non-null, song/bible samples run through
 * ``compute_song_layout`` / bible chunk packing so wrap and vertical fit match
 * live output. Without a measurer, lines are emitted unwrapped (unit tests).
 *
 * A media design (``Image``, ``Video``, ``Web``, ``Live``) is a frame in that
 * mode, its area placed as a frame that plays is (``place_frame_media``), with
 * a ``media_stand_in`` naming what plays there at showtime. Painted, every
 * element is at its own place in the stack, as on the output: the sample body
 * and the stand-in too.
 *
 * ``designer_stage`` marks the live designer preview rather than a gallery
 * thumbnail: a picture element with no picture yet gets a stand-in so it can
 * be seen and picked, and ``suppress_id`` leaves out the one element the
 * designer is drawing itself (the box whose text is being typed).
 */
[[nodiscard]] SceneFrame build_theme_draft_frame(
    const nlohmann::json& style,
    DraftContext context,
    int canvas_width = geom::k_reference_canvas.width,
    int canvas_height = geom::k_reference_canvas.height,
    std::string_view output_name = "preview",
    layout::ITextMeasurer* measurer = nullptr,
    bool designer_stage = false,
    std::string_view suppress_id = {});


/// Drop background-surface keys so a text theme previews on transparency
/// (gallery thumbs checkerboard that; the designer stage paints the grid in QML),
/// not a partner song background.
[[nodiscard]] nlohmann::json isolate_text_preview_style(nlohmann::json text_style);

}  // namespace seventhslide::scene
