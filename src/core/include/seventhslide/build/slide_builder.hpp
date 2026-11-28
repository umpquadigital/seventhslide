#pragma once

// SlideBuilder facade - resolve themes -> layout -> per-output runtime.
// Seam: plain C++20 (+ layout measurer); Session owns call sites.

#include "seventhslide/build/bible_layout.hpp"
#include "seventhslide/build/song_layout.hpp"
#include "seventhslide/freeform/compile.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::build {

enum class BuildKind {
    Song,
    Bible,
    Freeform,
    Empty,  // video / image / web page / divider / no content
};

/// Which payload ``build_slides`` fills. Media and dividers build no slides.
[[nodiscard]] constexpr BuildKind build_kind_for(model::ContentKind kind) noexcept
{
    switch (kind) {
    case model::ContentKind::Song:
        return BuildKind::Song;
    case model::ContentKind::Bible:
        return BuildKind::Bible;
    case model::ContentKind::Freeform:
        return BuildKind::Freeform;
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
    case model::ContentKind::Web:
    case model::ContentKind::Live:
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        return BuildKind::Empty;
    }
    return BuildKind::Empty;
}

struct BibleBuildInput {
    int chapter = 0;
    std::vector<build::BibleVerseInput> verses;
};

struct FreeformBuildInput {
    freeform::Document document;
};

/// Explicit rebuild request (Session fills this; no global state).
struct SlideBuildInput {
    BuildKind kind = BuildKind::Empty;
    theme::ThemeSelection selection;
    /// Base output configs (theme libraries + category_defaults). Not mutated.
    std::vector<model::OutputConfig> outputs;
    /// Lyrics and play order to lay out; the title labels virtual title lines
    /// and every field fills the title-overlay tokens. Its ``theme_map`` is
    /// where the theme chips in the play order find their picks.
    model::Song song;
    /// The church's CCLI licence, for the title overlays' ``{ccli-license}``.
    std::string ccli_licence_number;
    BibleBuildInput bible;
    FreeformBuildInput freeform;
};

/// Per-output runtime after one rebuild (structured - no HTML).
struct OutputRuntime {
    /// ``OutputConfig::id``: what a Freeform deck is looked up by.
    std::string output_id;
    /// ``OutputConfig::name``: what frames, the player and the host address.
    std::string name;
    /// The look this build resolved: elements fitted to the canvas, body boxes
    /// and behaviour flags ready for layout and paint. Carries no theme library.
    theme::ResolvedTheme theme;

    /// Song path: groups / lines / map (may be empty for non-song).
    build::LayoutResult layout;
    /// Bible / empty display strings (song paint uses ``layout``).
    std::vector<std::string> slide_texts;
    /// Bible: ``"ch:v"`` citations each ``slide_texts`` entry carries, in order
    /// (``build::BibleSlide::citations``). Empty for other kinds.
    std::vector<std::vector<std::string>> slide_citations;
    std::vector<int> line_to_slide;
    /// The sections the item plays, in order: a song's verse-order entries, a
    /// Freeform document's runs of cues (``freeform::cue_sections``). A verse
    /// played twice running is two entries. What indicators show.
    std::vector<std::string> verse_codes;
    /// Each line's section: an index into ``verse_codes``, or -1 for none.
    std::vector<int> verse_indices;
    /// Resolved theme per segment when theme chips change the look mid-song;
    /// ``slide_style_index`` maps each slide to its segment. Empty -> ``theme``
    /// for every slide (see ``theme_for_slide``).
    std::vector<theme::ResolvedTheme> segment_themes;
    std::vector<int> slide_style_index;
    std::vector<layout::OverlayScene> slide_overlays;
    /// A Freeform slide's own background, by slide. A missing or empty entry
    /// paints the theme's background.
    std::vector<std::optional<layout::BackgroundFill>> slide_bg_overrides;
    /// Freeform compiled slides (text / image / shape objects).
    std::vector<freeform::CompiledSlide> compiled_slides;
    int index = 0;

    [[nodiscard]] int slide_count() const noexcept;
    /// The theme the given slide was laid out with.
    [[nodiscard]] const theme::ResolvedTheme& theme_for_slide(int slide_index) const;
};

struct PlayerLineState {
    int total_lines = 0;
    std::vector<std::string> all_lines;
    std::vector<std::string> all_line_labels;
    /// Parallel to ``all_lines`` for a song: the line with its chords, or
    /// empty when it has none (or is shown in parts).
    std::vector<std::string> all_line_chords;
    int line_cursor = 0;
};

struct SlideBuildResult {
    BuildKind kind = BuildKind::Empty;
    std::vector<OutputRuntime> outputs;
    PlayerLineState player;
};

/// Resolve -> layout -> runtime for all outputs in ``input``.
[[nodiscard]] SlideBuildResult build_slides(const SlideBuildInput& input,
                                            layout::ITextMeasurer& measurer);

/// Same, for ``outputs`` instead of ``input.outputs`` (which is ignored), so a
/// caller that already holds the configs does not copy them into the input.
[[nodiscard]] SlideBuildResult build_slides(const SlideBuildInput& input,
                                            const std::vector<model::OutputConfig>& outputs,
                                            layout::ITextMeasurer& measurer);

/// Pad / truncate ``line_to_slide`` to ``total_lines``.
void pad_line_to_slide(std::vector<int>& line_to_slide, int total_lines, int last_slide_idx);

/// Reset one runtime to a single empty slide with a padded map.
void set_empty_slides(OutputRuntime& rt, int total_lines);

/// Each output's canvas by output id: where every Freeform deck is laid out to paint.
[[nodiscard]] freeform::DeckCanvases output_deck_canvases(
    const std::vector<model::OutputConfig>& outputs);

}  // namespace seventhslide::build
