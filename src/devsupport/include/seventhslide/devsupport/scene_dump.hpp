#pragma once

// Layout dump: a song's layout as structured JSON (``seventhslide.scene_dump``
// v1), compared against the oracle fixtures by the tests and written by the
// seventhslide-layout-dump tool. Not part of the app.

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/build/song_layout.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::devsupport {

inline constexpr std::string_view k_scene_dump_schema = "seventhslide.scene_dump";
inline constexpr int k_scene_dump_schema_version = 1;

/// Behaviour and background of a resolved theme, for a layout dump.
[[nodiscard]] nlohmann::json serialize_resolved_style(const theme::ResolvedTheme& theme);

[[nodiscard]] nlohmann::json serialize_slide_group(const layout::SlideGroup& g);

/**
 * Build one output's layout dump record from a finished ``LayoutResult``.
 *
 * ``overlays`` / ``bg`` are optional per-slide flags (default: all false).
 * ``total_lines`` pads ``line_to_slide`` when another output has more lines
 *; 0 -> use ``result.lines.size()``.
 */
[[nodiscard]] nlohmann::json serialize_output_layout(
    std::string_view name,
    int canvas_width,
    int canvas_height,
    const theme::ResolvedTheme& theme,
    const build::LayoutResult& result,
    const std::vector<bool>& overlay_nonempty = {},
    const std::vector<bool>& bg_override_present = {},
    int total_lines = 0);

struct SceneDumpInput {
    std::string item_type = "song";
    int item_index = 0;
    std::string title;
    std::string lyrics;
    std::string verse_order;
    std::optional<std::int64_t> song_id;
    std::string fixture_id;
    std::string notes;
};

/**
 * Layout one song for ``oc`` and emit a full scene dump document
 * (``oracle`` = ``"native"``).
 *
 * When ``title_mode`` and empty verse_order, prepends a virtual title.
 * ``has_title_overlay`` / ``title_overlay`` drive upcoming-lines and flags.
 */
[[nodiscard]] nlohmann::json build_song_scene_dump(
    const SceneDumpInput& input,
    std::string_view name,
    int canvas_width,
    int canvas_height,
    const theme::ResolvedTheme& theme,
    layout::ITextMeasurer& measurer,
    bool title_mode = false,
    bool has_title_overlay = false,
    const layout::OverlayScene* title_overlay = nullptr);

/**
 * Load an oracle fixture JSON, re-layout with native engine, return native dump.
 * Uses ``outputs[0]`` style + canvas from the fixture.
 */
[[nodiscard]] nlohmann::json native_scene_dump_from_oracle_fixture(
    const nlohmann::json& oracle_doc,
    layout::ITextMeasurer& measurer);

/// Validate schema shape.
[[nodiscard]] std::vector<std::string> validate_scene_document(const nlohmann::json& doc);

/// Per-slide flags for a dump: which slides carry a title overlay, and which
/// a background of their own.
[[nodiscard]] std::vector<bool> overlay_nonempty_flags(const build::OutputRuntime& rt);
[[nodiscard]] std::vector<bool> bg_override_present_flags(const build::OutputRuntime& rt);

}  // namespace seventhslide::devsupport
