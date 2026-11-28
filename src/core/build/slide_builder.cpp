#include "seventhslide/build/slide_builder.hpp"

#include "seventhslide/build/title_slides.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/slide_grouper.hpp"
#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/model/output_library.hpp"
#include "seventhslide/model/static_media.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/theme/song_theme_pack.hpp"
#include "seventhslide/theme/theme_elements.hpp"
#include "seventhslide/util/find.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace seventhslide::build {
namespace {

[[nodiscard]] std::string image_fill_url(std::string_view stored)
{
    stored = util::trim_view(stored);
    if (stored.empty()) {
        return {};
    }
    if (stored.starts_with("/static/")) {
        return std::string(stored);
    }
    const auto url = model::static_media_url(model::k_images_subdir, stored);
    return url.value_or(std::string{});
}

/// A slide's video background as a ``/static/...`` URL: a library video by
/// name, or a URL already.
[[nodiscard]] std::string video_fill_url(std::string_view stored)
{
    if (stored.starts_with("/static/")) {
        return std::string(stored);
    }
    const auto url = model::static_media_url(model::k_videos_subdir, stored);
    return url.value_or(std::string{});
}

[[nodiscard]] std::optional<layout::BackgroundFill> override_from_slide_background(
    const freeform::SlideBackground& bg)
{
    using freeform::SlideBackgroundKind;
    switch (bg.kind) {
    case SlideBackgroundKind::None: {
        // Nothing of the background theme's: the slide paints no background,
        // and the stacks behind it show.
        layout::BackgroundFill out;
        out.type = layout::BackgroundType::Transparent;
        return out;
    }
    case SlideBackgroundKind::Color: {
        layout::BackgroundFill out;
        out.type = layout::BackgroundType::Color;
        out.color = bg.color.empty() ? std::string(layout::k_default_fill_color) : bg.color;
        return out;
    }
    case SlideBackgroundKind::Image: {
        layout::BackgroundFill out;
        out.type = layout::BackgroundType::Image;
        out.image = image_fill_url(bg.image);
        return out;
    }
    case SlideBackgroundKind::Video: {
        layout::BackgroundFill out;
        out.type = layout::BackgroundType::Video;
        out.video = video_fill_url(bg.video);
        return out;
    }
    }
    return std::nullopt;
}

/// Identity of the output plus the theme resolved for one segment.
struct WorkingOutput {
    std::string id;
    std::string name;
    theme::ResolvedTheme theme;
};

[[nodiscard]] WorkingOutput working_output(const model::OutputConfig& base,
                                           const theme::ThemeSelection& sel)
{
    WorkingOutput working;
    working.id = base.id;
    working.name = base.name;
    working.theme = theme::resolve_theme(base, sel);
    return working;
}

struct ThemeOrderSegment {
    std::string chip_id;
    std::string verse_order;
    theme::ThemeSelection selection;
};

[[nodiscard]] std::vector<ThemeOrderSegment> song_theme_segments(const SlideBuildInput& input)
{
    std::vector<ThemeOrderSegment> out;
    if (theme::verse_order_has_theme_chips(input.song.verse_order)) {
        const auto stored = theme::SongThemeMap::from_json(input.song.theme_map);
        for (auto& seg : theme::split_order_by_theme_chips(input.song.verse_order)) {
            if (seg.verse_order.empty()) {
                continue;
            }
            ThemeOrderSegment w;
            w.chip_id = seg.chip_id;
            w.verse_order = std::move(seg.verse_order);
            w.selection = input.selection;
            w.selection.content_themes = stored.chip(w.chip_id);
            out.push_back(std::move(w));
        }
        return out;
    }
    // Themes are chosen only by chips in the play order: with none, the song
    // inherits everything.
    ThemeOrderSegment w;
    w.verse_order = input.song.verse_order;
    w.selection = input.selection;
    w.selection.content_themes = {};
    out.push_back(std::move(w));
    return out;
}

/**
 * The title design this output paints on a song's title slide, if any.
 *
 * Empty when the theme has no Titles design, and empty when it has one but has
 * title slides switched off - which is a per-output answer: the song's title cue
 * is still there (every output walks one line cursor), that output just paints
 * its ordinary song design on it. ``working`` is the cascaded style, so its
 * ``show_title_slides`` is the theme's switch once the output's own default has
 * been layered under it. A theme with no element model paints the stock Titles
 * design.
 */
[[nodiscard]] layout::OverlayScene title_overlay_for(const model::OutputConfig& base,
                                                     const theme::ThemeSelection& sel,
                                                     const model::Song& song,
                                                     std::string_view ccli_licence,
                                                     const theme::ResolvedTheme& theme)
{
    // No text theme: the stock body still lays the lyrics out, but it is not a
    // title design, so the opening lines may show on the title slide.
    if (theme::resolve_text_theme(base, sel) == nullptr || !theme.behavior.show_title_slides) {
        return {};
    }
    theme::ElementConditions conditions;
    conditions.song_copyright = song.show_copyright;
    const auto boxes = theme::title_boxes(theme.elements, conditions);
    if (boxes.empty()) {
        return {};
    }
    return layout::build_title_overlay_scene(boxes,
                                             layout::song_template_vars(song, ccli_licence));
}

void append_layout_segment(build::LayoutResult& dst, build::LayoutResult src)
{
    const int line_off = static_cast<int>(dst.lines.size());
    const int verse_off = static_cast<int>(dst.verse_codes.size());
    for (auto& ln : src.lines) {
        ln.verse_index += verse_off;
    }
    for (auto& g : src.groups) {
        for (int& idx : g.indices) {
            idx += line_off;
        }
        if (g.owner_line >= 0) {
            g.owner_line += line_off;
        }
    }
    dst.lines.insert(dst.lines.end(),
                     std::make_move_iterator(src.lines.begin()),
                     std::make_move_iterator(src.lines.end()));
    dst.groups.insert(dst.groups.end(),
                      std::make_move_iterator(src.groups.begin()),
                      std::make_move_iterator(src.groups.end()));
    dst.verse_codes.insert(dst.verse_codes.end(),
                           src.verse_codes.begin(),
                           src.verse_codes.end());
    for (const int line : src.row_lines) {
        dst.row_lines.push_back(line >= 0 ? line + line_off : line);
    }
}

void rebuild_line_map(build::LayoutResult& layout)
{
    layout.line_to_slide = layout::build_line_to_slide(layout.groups, layout.lines.size());
}

/// Theme chips layout each run on its own, so follow-lines cannot peek past a
/// chip. Fill leftover box space with later lines (still follow, not active)
/// and refresh title-upcoming from the next lyric group.
void extend_follow_across_theme_segments(
    build::LayoutResult& layout,
    const std::vector<const theme::ResolvedTheme*>& slide_themes,
    const std::vector<layout::OverlayScene>& overlays,
    bool title_mode,
    layout::ITextMeasurer& measurer)
{
    for (std::size_t gi = 0; gi < layout.groups.size(); ++gi) {
        auto& g = layout.groups[gi];
        if (g.is_title || g.indices.empty()) {
            continue;
        }
        if (gi >= slide_themes.size() || slide_themes[gi]->behavior.follow_lines <= 0) {
            continue;
        }
        const auto& theme = *slide_themes[gi];
        const int avail = std::max(1, theme.song.height);

        int used = 0;
        for (std::size_t i = 0; i < g.indices.size(); ++i) {
            const int idx = g.indices[i];
            if (idx < 0 || idx >= static_cast<int>(layout.lines.size())) {
                continue;
            }
            const auto& ln = layout.lines[static_cast<std::size_t>(idx)];
            used += (static_cast<int>(i) < g.active_count) ? ln.active_height_px
                                                           : ln.base_height_px;
        }

        int prev_idx = g.indices.back();
        int next = prev_idx + 1;
        while (next < static_cast<int>(layout.lines.size())) {
            const auto& ln = layout.lines[static_cast<std::size_t>(next)];
            if (ln.is_title) {
                break;
            }
            int h = build::painted_line_height_px(ln, theme, measurer);
            if (build::needs_verse_gap(layout, theme.behavior.fluid_slides, theme.behavior.verse_gap,
                                       next, prev_idx)) {
                h += theme.behavior.verse_gap;
            }
            if (used > 0 && used + h > avail) {
                break;
            }
            g.indices.push_back(next);
            used += h;
            prev_idx = next;
            ++next;
        }
    }

    if (!title_mode) {
        return;
    }
    for (std::size_t gi = 0; gi < layout.groups.size(); ++gi) {
        if (!layout.groups[gi].is_title) {
            continue;
        }
        const bool has_overlay = gi < overlays.size() && !overlays[gi].empty();
        if (gi < slide_themes.size()
            && build::should_show_title_upcoming(true, *slide_themes[gi], has_overlay)) {
            (void)build::show_upcoming_on_title(layout, gi);
        }
    }
}

[[nodiscard]] OutputRuntime make_runtime_shell(const WorkingOutput& working)
{
    OutputRuntime rt;
    rt.output_id = working.id;
    rt.name = working.name;
    rt.theme = working.theme;
    return rt;
}

void build_song(SlideBuildResult& result,
                const SlideBuildInput& input,
                const std::vector<model::OutputConfig>& outputs,
                layout::ITextMeasurer& measurer)
{
    // One working config per theme segment; each slide records which.
    struct Active {
        build::LayoutResult layout;
        std::vector<theme::ResolvedTheme> segment_themes;
        std::vector<int> slide_segment;
        std::vector<layout::OverlayScene> slide_overlays;
    };

    const auto segments = song_theme_segments(input);

    // Each output's theme per segment, resolved once. The first is also its
    // runtime theme; a song with no segments inherits everything.
    std::vector<std::vector<WorkingOutput>> workings(outputs.size());
    std::vector<WorkingOutput> shells;
    shells.reserve(outputs.size());
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        for (const auto& seg : segments) {
            workings[i].push_back(working_output(outputs[i], seg.selection));
        }
        if (!workings[i].empty()) {
            shells.push_back(workings[i].front());
            continue;
        }
        theme::ThemeSelection inherit = input.selection;
        inherit.content_themes = {};
        shells.push_back(working_output(outputs[i], inherit));
    }

    // Whether the song has a title slide at all is the song's call - the slide
    // is a line every output shares, so it cannot exist on some of them only.
    // What each output *paints* there is its own theme's call: see
    // ``title_overlay_for`` and the Titles element context.
    const bool title_mode = build::title_slide_mode_active(input.song.verse_order);

    // Every output laid out together per segment: they share the rows the
    // player's cursor walks, and each line is measured once per output.
    std::vector<std::vector<build::LayoutResult>> segment_layouts;
    segment_layouts.reserve(segments.size());
    for (std::size_t s = 0; s < segments.size(); ++s) {
        std::vector<const theme::ResolvedTheme*> themes;
        themes.reserve(outputs.size());
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            themes.push_back(&workings[i][s].theme);
        }
        segment_layouts.push_back(build::compute_song_layouts(
            themes, measurer, input.song.lyrics, segments[s].verse_order, title_mode));
    }

    std::vector<Active> actives;
    actives.reserve(shells.size());
    for (std::size_t i = 0; i < shells.size(); ++i) {
        const auto& base = outputs[i];
        Active a;
        a.segment_themes.reserve(segments.size());

        for (std::size_t s = 0; s < segments.size(); ++s) {
            const auto& seg = segments[s];
            auto& working = workings[i][s];
            auto part = std::move(segment_layouts[s][i]);
            auto overlay = title_overlay_for(base, seg.selection, input.song,
                                             input.ccli_licence_number, working.theme);
            const bool has_overlay = !overlay.empty();
            if (title_mode) {
                build::apply_title_slide_layout(
                    part, working.theme, seg.verse_order, true, has_overlay);
            }
            auto part_overlays = layout::assign_overlay_to_title_slides(part.groups, overlay);
            const int segment = static_cast<int>(a.segment_themes.size());
            a.slide_segment.insert(a.slide_segment.end(), part.groups.size(), segment);
            a.slide_overlays.insert(a.slide_overlays.end(),
                                    std::make_move_iterator(part_overlays.begin()),
                                    std::make_move_iterator(part_overlays.end()));
            append_layout_segment(a.layout, std::move(part));
            a.segment_themes.push_back(std::move(working.theme));
        }
        if (a.slide_overlays.size() < a.layout.groups.size()) {
            a.slide_overlays.resize(a.layout.groups.size());
        }
        std::vector<const theme::ResolvedTheme*> slide_themes;
        slide_themes.reserve(a.slide_segment.size());
        for (const int segment : a.slide_segment) {
            slide_themes.push_back(&a.segment_themes[static_cast<std::size_t>(segment)]);
        }
        extend_follow_across_theme_segments(
            a.layout, slide_themes, a.slide_overlays, title_mode, measurer);
        rebuild_line_map(a.layout);
        actives.push_back(std::move(a));
    }

    // Every output has the plan's rows. Each row reads (Line Controller,
    // remote, conversions) as the line that shows exactly that row: a part
    // where some output splits the line, not the whole line one with room
    // shows across several rows.
    const std::size_t rows = actives.empty() ? 0 : actives.front().layout.row_lines.size();
    std::vector<const build::LayoutLine*> row_source(rows, nullptr);
    for (const bool exact : {true, false}) {
        for (const auto& a : actives) {
            const auto& row_lines = a.layout.row_lines;
            std::vector<int> covers(a.layout.lines.size(), 0);
            for (const int line : row_lines) {
                if (line >= 0) {
                    ++covers[static_cast<std::size_t>(line)];
                }
            }
            for (std::size_t r = 0; r < rows && r < row_lines.size(); ++r) {
                const int line = row_lines[r];
                if (row_source[r] != nullptr || line < 0
                    || (exact && covers[static_cast<std::size_t>(line)] != 1)) {
                    continue;
                }
                row_source[r] = &a.layout.lines[static_cast<std::size_t>(line)];
            }
        }
    }

    const int total_lines = static_cast<int>(rows);
    result.player.total_lines = total_lines;
    result.player.line_cursor = 0;
    std::vector<int> row_verse_indices;
    row_verse_indices.reserve(rows);
    result.player.all_lines.reserve(rows);
    result.player.all_line_labels.reserve(rows);
    // Every row of the line list is markup; the title is plain text.
    const std::string title_text = model::escape_markup_text(
        input.song.title.empty() ? "Title slide" : input.song.title);
    for (const auto* ln : row_source) {
        if (ln == nullptr) {  // not reachable: a row exists because some output shows it
            result.player.all_lines.emplace_back();
            result.player.all_line_labels.emplace_back();
            result.player.all_line_chords.emplace_back();
            row_verse_indices.push_back(-1);
            continue;
        }
        const bool title_row = title_mode && parse::is_title_code(ln->label);
        result.player.all_lines.push_back(title_row ? title_text : ln->text);
        // Only where the chords sit on exactly the words the row reads.
        const bool chords_fit = !title_row && !ln->chord_text.empty()
            && layout::strip_chords(ln->chord_text) == result.player.all_lines.back();
        result.player.all_line_chords.push_back(chords_fit ? ln->chord_text : std::string());
        result.player.all_line_labels.push_back(ln->label);
        row_verse_indices.push_back(ln->verse_index);
    }

    for (std::size_t i = 0; i < shells.size(); ++i) {
        OutputRuntime rt = make_runtime_shell(shells[i]);
        auto& a = actives[i];
        rt.layout = std::move(a.layout);
        rt.verse_codes = rt.layout.verse_codes;
        rt.verse_indices = row_verse_indices;
        rt.line_to_slide = build::row_to_slide(rt.layout);
        rt.segment_themes = std::move(a.segment_themes);
        rt.slide_style_index = std::move(a.slide_segment);
        rt.slide_overlays = std::move(a.slide_overlays);
        if (rt.slide_overlays.size() < rt.layout.groups.size()) {
            rt.slide_overlays.resize(rt.layout.groups.size());
        }
        result.outputs.push_back(std::move(rt));
    }
}

void build_bible(SlideBuildResult& result,
                 const SlideBuildInput& input,
                 const std::vector<model::OutputConfig>& outputs,
                 layout::ITextMeasurer& measurer)
{
    std::vector<WorkingOutput> workings;
    workings.reserve(outputs.size());
    for (const auto& base : outputs) {
        workings.push_back(working_output(base, input.selection));
    }

    std::vector<const theme::ResolvedTheme*> bible_themes;
    for (const auto& working : workings) {
        if (working.theme.bible.shown) {
            bible_themes.push_back(&working.theme);
        }
    }
    const auto plan = build::plan_bible_chunks(input.bible.chapter,
                                               input.bible.verses,
                                               bible_themes,
                                               measurer);
    const int total_lines = static_cast<int>(plan.plain_lines.size());

    result.player.total_lines = total_lines;
    result.player.all_lines = plan.plain_lines;
    result.player.all_line_labels = plan.line_labels;
    result.player.line_cursor = 0;

    for (const auto& working : workings) {
        OutputRuntime rt = make_runtime_shell(working);

        if (!working.theme.bible.shown) {
            set_empty_slides(rt, total_lines);
            result.outputs.push_back(std::move(rt));
            continue;
        }

        auto slides = build::build_bible_slides_for_output(working.theme, plan, measurer);
        rt.slide_texts.reserve(slides.slides.size());
        rt.slide_citations.reserve(slides.slides.size());
        for (auto& slide : slides.slides) {
            rt.slide_texts.push_back(std::move(slide.text));
            rt.slide_citations.push_back(std::move(slide.citations));
        }
        rt.line_to_slide = std::move(slides.line_to_slide);
        pad_line_to_slide(rt.line_to_slide, total_lines,
                          std::max(0, static_cast<int>(rt.slide_texts.size()) - 1));
        result.outputs.push_back(std::move(rt));
    }
}

[[nodiscard]] const freeform::CompiledDeck* find_compiled_deck(
    const freeform::CompiledPlayback& compiled,
    std::string_view output_id)
{
    return util::find_by(compiled.decks, output_id, &freeform::CompiledDeck::output_id);
}


void apply_compiled_playback(SlideBuildResult& result,
                             const SlideBuildInput& input,
                             const std::vector<model::OutputConfig>& outputs,
                             const freeform::CompiledPlayback& compiled)
{
    result.player.total_lines = static_cast<int>(compiled.lines.size());
    result.player.all_lines = compiled.lines;
    result.player.all_line_labels = compiled.line_labels;
    result.player.line_cursor = 0;

    theme::ThemeSelection sel = input.selection;
    // A library song or bible retargeted through a Freeform build is resolved
    // as Freeform: it takes no theme. A rundown row keeps its own kind so the
    // service's picks for that kind still apply.
    if (sel.kind != model::ContentKind::Freeform && !sel.from_service) {
        sel.kind = model::ContentKind::Freeform;
    }
    // Freeform fill is per-slide: neither the item's themes nor a song's
    // theme chips apply.
    sel.content_themes = {};

    const int total_lines = result.player.total_lines;
    for (const auto& base : outputs) {
        auto working = working_output(base, sel);
        OutputRuntime rt = make_runtime_shell(working);
        rt.verse_codes.clear();
        rt.verse_indices.clear();

        const auto* deck = find_compiled_deck(compiled, working.id);
        if (deck == nullptr) {
            set_empty_slides(rt, total_lines);
            result.outputs.push_back(std::move(rt));
            continue;
        }

        rt.line_to_slide = deck->line_to_slide;
        pad_line_to_slide(rt.line_to_slide, total_lines,
                          std::max(0, static_cast<int>(deck->slides.size()) - 1));
        if (!compiled.verse_codes.empty()) {
            rt.verse_codes = compiled.verse_codes;
            rt.verse_indices = compiled.verse_indices;
            rt.verse_indices.resize(static_cast<std::size_t>(std::max(0, total_lines)), -1);
        }
        rt.compiled_slides = deck->slides;
        rt.slide_texts.assign(deck->slides.size(), "");
        if (rt.slide_texts.empty()) {
            rt.slide_texts.emplace_back();
        }
        rt.slide_overlays.clear();
        rt.slide_bg_overrides.clear();
        rt.slide_overlays.reserve(deck->slides.size());
        rt.slide_bg_overrides.reserve(deck->slides.size());
        for (const auto& slide : deck->slides) {
            // Freeform text boxes are objects, not theme overlay: they belong to
            // the z-ordered object stack so they can sit above or below a shape.
            // ``slide_overlays`` stays empty here and keeps its one job -
            // song / bible title-slide overlays.
            rt.slide_overlays.emplace_back();
            rt.slide_bg_overrides.push_back(override_from_slide_background(slide.background));
        }
        if (rt.slide_overlays.empty()) {
            rt.slide_overlays.emplace_back();
        }
        if (rt.slide_bg_overrides.size() < rt.slide_overlays.size()) {
            rt.slide_bg_overrides.resize(rt.slide_overlays.size());
        }
        result.outputs.push_back(std::move(rt));
    }
}

void build_freeform(SlideBuildResult& result,
                    const SlideBuildInput& input,
                    const std::vector<model::OutputConfig>& outputs,
                    layout::ITextMeasurer& measurer)
{
    // A deck keeps the canvas it was designed on; each output paints its deck
    // mapped onto its own canvas. Copy the document only when a deck moves.
    const auto canvases = output_deck_canvases(outputs);
    // Only these outputs' decks are read below; laying out the others (a
    // thumbnail builds one output of a deck made for several) is wasted.
    std::set<std::string, std::less<>> wanted;
    for (const auto& oc : outputs) {
        wanted.insert(oc.id);
    }
    if (freeform::decks_fit_canvases(input.freeform.document, canvases)) {
        apply_compiled_playback(result, input, outputs,
                                freeform::compile(input.freeform.document, &measurer, &wanted));
        return;
    }
    auto fitted = input.freeform.document;
    freeform::fit_decks_to_canvases(fitted, canvases);
    apply_compiled_playback(result, input, outputs, freeform::compile(fitted, &measurer, &wanted));
}

void build_empty(SlideBuildResult& result,
                 const SlideBuildInput& input,
                 const std::vector<model::OutputConfig>& outputs)
{
    result.player = {};
    for (const auto& base : outputs) {
        // Image / video / divider all land here; only the media context has
        // anything to fold, and a divider simply has no areas to place.
        auto working = working_output(base, input.selection);
        OutputRuntime rt = make_runtime_shell(working);
        // Cleared: no slides (media / divider).
        rt.slide_texts.clear();
        rt.line_to_slide.clear();
        rt.slide_overlays.clear();
        rt.slide_bg_overrides.clear();
        result.outputs.push_back(std::move(rt));
    }
}

} // namespace

int OutputRuntime::slide_count() const noexcept
{
    if (!compiled_slides.empty()) {
        return static_cast<int>(compiled_slides.size());
    }
    if (!layout.groups.empty()) {
        return static_cast<int>(layout.groups.size());
    }
    if (!slide_texts.empty()) {
        return static_cast<int>(slide_texts.size());
    }
    return 0;
}

void pad_line_to_slide(std::vector<int>& line_to_slide, int total_lines, int last_slide_idx)
{
    if (total_lines < 0) {
        total_lines = 0;
    }
    const int last = std::max(0, last_slide_idx);
    while (static_cast<int>(line_to_slide.size()) < total_lines) {
        line_to_slide.push_back(last);
    }
    if (static_cast<int>(line_to_slide.size()) > total_lines) {
        line_to_slide.resize(static_cast<std::size_t>(total_lines));
    }
}

void set_empty_slides(OutputRuntime& rt, int total_lines)
{
    rt.layout = {};
    rt.slide_texts = {""};
    rt.slide_citations.clear();
    rt.line_to_slide.assign(static_cast<std::size_t>(std::max(0, total_lines)), 0);
    rt.verse_codes.clear();
    rt.verse_indices.clear();
    rt.slide_overlays = {layout::OverlayScene{}};
    rt.slide_bg_overrides.clear();
    rt.compiled_slides.clear();
    rt.index = 0;
}

freeform::DeckCanvases output_deck_canvases(const std::vector<model::OutputConfig>& outputs)
{
    freeform::DeckCanvases out;
    for (const auto& oc : outputs) {
        out[oc.id] = model::output_canvas(oc);
    }
    return out;
}

SlideBuildResult build_slides(const SlideBuildInput& input, layout::ITextMeasurer& measurer)
{
    return build_slides(input, input.outputs, measurer);
}

SlideBuildResult build_slides(const SlideBuildInput& input,
                              const std::vector<model::OutputConfig>& outputs,
                              layout::ITextMeasurer& measurer)
{
    SlideBuildResult result;
    result.kind = input.kind;

    switch (input.kind) {
    case BuildKind::Song:
        if (outputs.empty()) {
            build_empty(result, input, outputs);
            result.kind = BuildKind::Song;
        } else {
            build_song(result, input, outputs, measurer);
        }
        break;
    case BuildKind::Bible:
        build_bible(result, input, outputs, measurer);
        break;
    case BuildKind::Freeform:
        build_freeform(result, input, outputs, measurer);
        break;
    case BuildKind::Empty:
        build_empty(result, input, outputs);
        break;
    }
    return result;
}

const theme::ResolvedTheme& OutputRuntime::theme_for_slide(int slide_index) const
{
    if (slide_index >= 0 && slide_index < static_cast<int>(slide_style_index.size())) {
        const int segment = slide_style_index[static_cast<std::size_t>(slide_index)];
        if (segment >= 0 && segment < static_cast<int>(segment_themes.size())) {
            return segment_themes[static_cast<std::size_t>(segment)];
        }
    }
    return theme;
}

}  // namespace seventhslide::build
