#include <seventhslide/scene/scene_builder.hpp>

#include "seventhslide/build/bible_layout.hpp"

#include "seventhslide/freeform/paint.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/model/transition.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/scene/chrome_text.hpp"
#include "seventhslide/scene/text_metrics.hpp"
#include "seventhslide/theme/theme_elements.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace seventhslide::scene {

SceneBackground background_of(const theme::ResolvedTheme& theme,
                              const std::optional<layout::BackgroundFill>& fill)
{
    SceneBackground bg;
    if (fill) {
        bg.type = fill->type;
        bg.color = fill->color;
        bg.image = fill->image;
        bg.video = fill->video;
    } else {
        bg.type = theme.background.type;
        bg.color = theme.background.color;
        bg.image = theme.background.image;
        bg.video = theme.background.video;
    }
    switch (bg.type) {
    case layout::BackgroundType::Video:
        bg.image.clear();
        break;
    case layout::BackgroundType::Image:
        bg.video.clear();
        break;
    case layout::BackgroundType::Transparent:
    case layout::BackgroundType::Color:
        bg.image.clear();
        bg.video.clear();
        break;
    }
    return bg;
}

namespace {

/// ``box`` as painted: line height and letter spacing within what paint draws.
[[nodiscard]] SceneTextBoxStyle painted_box(SceneTextBoxStyle box)
{
    box.line_height = layout::clamp_css_line_height(box.line_height);
    box.letter_spacing = layout::clamp_letter_spacing_px(box.letter_spacing);
    return box;
}

/// Where the theme puts a picture, video, page or live source on the frame.
/// The fit is the caller's: only some media take one.
[[nodiscard]] SceneMediaArea scene_area_of(const theme::MediaRect& rect) noexcept
{
    // Every field bound by name: one added to MediaRect stops this compiling
    // until it is copied here or left out on purpose.
    [[maybe_unused]] const auto& [enabled, x, y, width, height, z, fit] = rect;
    return {.enabled = rect.enabled,
            .x = rect.x,
            .y = rect.y,
            .width = rect.width,
            .height = rect.height,
            .z = rect.z};
}

/// The text theme context a frame of ``mode`` is designed and transitions
/// in; none for a Freeform (it has its own), an empty output, or audio, which
/// paints nothing and so cuts what its stack showed, as a stack cleared does.
[[nodiscard]] std::optional<theme::ElementContext> content_context_of(SceneMode mode) noexcept
{
    switch (mode) {
    case SceneMode::Song:
        return theme::ElementContext::Song;
    case SceneMode::Bible:
        return theme::ElementContext::Bible;
    case SceneMode::Image:
        return theme::ElementContext::Image;
    case SceneMode::Video:
        return theme::ElementContext::Video;
    case SceneMode::Web:
        return theme::ElementContext::Web;
    case SceneMode::Live:
        return theme::ElementContext::Live;
    case SceneMode::Audio:
    case SceneMode::Freeform:
    case SceneMode::Empty:
        break;
    }
    return std::nullopt;
}

}  // namespace

SceneTextBoxStyle song_box(const theme::ResolvedTheme& theme)
{
    return painted_box(theme.song);
}

SceneTextBoxStyle bible_box(const theme::ResolvedTheme& theme)
{
    return painted_box(theme.bible);
}

void place_frame_media(SceneFrame& frame, const theme::ResolvedTheme& theme)
{
    if (frame.image) {
        frame.image->area = scene_area_of(theme.image);
        frame.image->fit = theme.image.fit;
    }
    if (frame.video) {
        frame.video->area = scene_area_of(theme.video);
    }
    if (frame.web) {
        frame.web->area = scene_area_of(theme.web);
        frame.web->fit = theme.web.fit;
    }
    // A live source fills the Video area, as a video does.
    if (frame.live) {
        frame.live->area = scene_area_of(theme.live);
    }
}

layout::OverlayBox stand_in_label(std::string_view text,
                                  const canvas::BoxRect& at,
                                  geom::CanvasSize canvas)
{
    // The size the designer's stand-ins have always had: 36 px on the
    // reference canvas.
    constexpr int k_reference_size = 36;
    const auto mapping = geom::CanvasMapping::stretch(geom::k_reference_canvas, canvas);
    layout::OverlayBox box;
    static_cast<canvas::BoxRect&>(box) = at;
    box.font_family = std::string(canvas::k_default_font_family);
    box.font_size = std::max(1, mapping.font_size(k_reference_size));
    box.font_color = "#aaaaaa";
    box.text_align = "center";
    box.vertical_align = "middle";
    box.lines.push_back(model::parse_inline_markup(text));
    return box;
}

namespace {

void append_runs_line(SceneTextContent& content,
                      std::string_view text,
                      SceneLineRole role,
                      const canvas::LineType& type,
                      int margin_top_px,
                      bool wrap_continue = false)
{
    SceneTextLine line;
    line.runs = model::parse_inline_markup(text);
    line.role = role;
    line.type = type;
    line.margin_top_px = margin_top_px;
    line.wrap_continue = wrap_continue;
    content.lines.push_back(std::move(line));
}

/// ``fragments``, the rows of the line ``source`` (its text, markup or plain),
/// each a visual line. A row that starts where ``source`` has no space between
/// it and the row before continues a word cut between them.
void append_visual_fragments(SceneTextContent& content,
                             const std::vector<std::string>& fragments,
                             std::string_view source,
                             SceneLineRole role,
                             const canvas::LineType& type,
                             int margin_top_px)
{
    const std::string whole = model::plain_text(model::parse_inline_markup(source));
    std::size_t at = 0;
    for (std::size_t f = 0; f < fragments.size(); ++f) {
        append_runs_line(content,
                         fragments[f],
                         role,
                         type,
                         f == 0 ? margin_top_px : 0,
                         f > 0);
        const std::size_t after_row = at;
        while (at < whole.size() && util::is_space(whole[at])) {
            ++at;
        }
        content.lines.back().wrap_in_word = f > 0 && at == after_row && at < whole.size();
        at += model::plain_text(content.lines.back().runs).size();
    }
}

[[nodiscard]] int text_avail_w(const SceneTextBoxStyle& box) noexcept
{
    return std::max(1, box.width);
}

/// The font a line in ``type`` over ``box`` is measured with.
[[nodiscard]] layout::FontStyle box_font_style(const SceneTextBoxStyle& box,
                                               const canvas::LineType& type)
{
    const auto painted = canvas::painted_type(box, type);
    layout::FontStyle st;
    st.family = painted.family;
    st.size_px = static_cast<double>(std::max(1, painted.size));
    st.bold = painted.bold;
    st.italic = painted.italic;
    st.letter_spacing = box.letter_spacing;
    return st;
}

void append_wrapped_text(SceneTextContent& content,
                         std::string_view text,
                         SceneLineRole role,
                         const canvas::LineType& type,
                         int margin_top_px,
                         const layout::RunMeasurer& measure,
                         const SceneTextBoxStyle& box)
{
    const auto fragments = layout::wrap_marked_text_to_width(
        text, measure, static_cast<double>(text_avail_w(box)));
    append_visual_fragments(content, fragments, text, role, type, margin_top_px);
}

void append_wrapped_text(SceneTextContent& content,
                         std::string_view text,
                         SceneLineRole role,
                         const canvas::LineType& type,
                         int margin_top_px,
                         layout::ITextMeasurer* measurer,
                         const SceneTextBoxStyle& box)
{
    if (!measurer || text.empty()) {
        append_runs_line(content, text, role, type, margin_top_px);
        return;
    }
    append_wrapped_text(content, text, role, type, margin_top_px,
                        layout::bind_run_measurer(*measurer, box_font_style(box, type)), box);
}

/// ``chords`` as the rows it wraps to in ``content``'s box at the size it is
/// painted at (``wrap_chord_line``, as song layout counted them), each a chord
/// row over its words. Unwrapped without a measurer.
void append_chord_rows(SceneTextContent& content,
                       const layout::ChordLine& chords,
                       SceneLineRole role,
                       const canvas::LineType& type,
                       int margin_top_px,
                       layout::ITextMeasurer* measurer)
{
    layout::ChordRows wrapped;
    if (measurer != nullptr) {
        wrapped = layout::wrap_chord_line(chords, box_font_style(content.box, type),
                                          content.chords, *measurer,
                                          static_cast<double>(text_avail_w(content.box)));
    }
    if (wrapped.rows.empty()) {
        wrapped.line = chords;
        wrapped.rows.push_back({0, chords.items.size()});
    }
    for (std::size_t r = 0; r < wrapped.rows.size(); ++r) {
        SceneTextLine line;
        line.chords = layout::chord_line_part(wrapped.line, wrapped.rows[r]);
        line.runs = layout::chord_items_lyric(line.chords.items);
        line.role = role;
        line.type = type;
        line.margin_top_px = r == 0 ? margin_top_px : 0;
        line.wrap_continue = r > 0;
        line.wrap_in_word = wrapped.continues_word(r);
        line.has_chords = true;
        content.lines.push_back(std::move(line));
    }
}

void append_layout_line(SceneTextContent& content,
                        const build::LayoutLine& ln,
                        SceneLineRole role,
                        const canvas::LineType& type,
                        const canvas::LineType& baked,
                        int margin_top_px,
                        layout::ITextMeasurer* measurer,
                        const SceneTextBoxStyle& box)
{
    if (ln.has_chord_layout) {
        append_chord_rows(content, ln.chords, role, type, margin_top_px, measurer);
        return;
    }

    // Layout baked the fragments in ``baked``'s font (the other lines', under
    // follow-lines); a line painted in another font is wrapped again at it.
    // Follow leftovers can also come from a later theme chip; wrap with this
    // slide's box so leftover room is filled at the current font.
    const bool type_rewrap =
        measurer != nullptr && box_font_style(box, type) != box_font_style(box, baked);
    const bool follow_rewrap = measurer != nullptr && role == SceneLineRole::Follow;
    if (type_rewrap || follow_rewrap) {
        append_wrapped_text(content, ln.text, role, type, margin_top_px, measurer, box);
        return;
    }

    if (!ln.wrap_fragments.empty()) {
        const auto first_row = content.lines.size();
        append_visual_fragments(content, ln.wrap_fragments, ln.text, role, type, margin_top_px);
        if (ln.evened) {
            for (auto r = first_row + 1; r < content.lines.size(); ++r) {
                content.lines[r].wrap_kept = true;
            }
        }
        return;
    }

    // Layout did not bake fragments for this line: wrap at the box here.
    append_wrapped_text(content, ln.text, role, type, margin_top_px, measurer, box);
}

}  // namespace

SceneTextContent build_song_slide_text(const build::LayoutResult& layout,
                                       const theme::ResolvedTheme& theme,
                                       int group_index,
                                       layout::ITextMeasurer* measurer)
{
    SceneTextContent content;
    content.box = song_box(theme);
    content.z = theme.song.z;
    content.chords = theme.song.chords;
    if (group_index < 0 || group_index >= static_cast<int>(layout.groups.size())) {
        return content;
    }

    const auto& group = layout.groups[static_cast<std::size_t>(group_index)];
    const int follow = theme.behavior.follow_lines;
    // Under follow-lines the active line and the other lines are set in their
    // own types; layout measured in the other lines' (build::song_layout).
    static const canvas::LineType k_box_type;
    const canvas::LineType& others = follow > 0 ? theme.song.other_lines : k_box_type;
    const canvas::LineType& active = follow > 0 ? theme.song.active_line : k_box_type;

    for (std::size_t i = 0; i < group.indices.size(); ++i) {
        const int idx = group.indices[i];
        if (idx < 0 || idx >= static_cast<int>(layout.lines.size())) {
            continue;
        }
        const auto& ln = layout.lines[static_cast<std::size_t>(idx)];

        SceneLineRole role = SceneLineRole::Normal;
        const canvas::LineType* type = &others;
        const bool leftover =
            group.active_count < static_cast<int>(group.indices.size());
        if (follow > 0 || leftover) {
            const bool is_active = static_cast<int>(i) < group.active_count;
            role = is_active ? SceneLineRole::Active : SceneLineRole::Follow;
            if (is_active) {
                type = &active;
            }
        }

        int margin = 0;
        if (i > 0
            && build::needs_verse_gap(layout, theme.behavior.fluid_slides,
                                      theme.behavior.verse_gap, idx,
                                      group.indices[i - 1])) {
            margin = theme.behavior.verse_gap;
        }

        append_layout_line(content, ln, role, *type, others, margin, measurer, content.box);
    }
    return content;
}

namespace {

[[nodiscard]] std::optional<SceneTextContent> build_song_text(
    const build::OutputRuntime& rt,
    const theme::ResolvedTheme& theme,
    int slide_index,
    layout::ITextMeasurer* measurer)
{
    // Same contract as the passage box: the body switches off, the chrome
    // (copyright / indicator / clock) and the background stay.
    if (!theme.song.shown) {
        return std::nullopt;
    }
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.layout.groups.size())) {
        return std::nullopt;
    }
    auto content = build_song_slide_text(rt.layout, theme, slide_index, measurer);
    if (content.lines.empty()) {
        return std::nullopt;
    }
    return content;
}

[[nodiscard]] std::optional<SceneTextContent> build_bible_text(
    const build::OutputRuntime& rt,
    const theme::ResolvedTheme& theme,
    int slide_index,
    layout::ITextMeasurer* measurer)
{
    if (!theme.bible.shown) {
        return std::nullopt;
    }
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.slide_texts.size())) {
        return std::nullopt;
    }
    const auto slot = static_cast<std::size_t>(slide_index);
    const std::string& raw = rt.slide_texts[slot];
    if (raw.empty()) {
        return std::nullopt;
    }
    // Runtimes built outside ``build_bible`` may carry text without citations.
    static const std::vector<std::string> k_no_citations;
    const auto& citations =
        slot < rt.slide_citations.size() ? rt.slide_citations[slot] : k_no_citations;
    auto content = build_bible_text_content(raw, citations, theme, measurer);
    if (content.lines.empty()) {
        return std::nullopt;
    }
    return content;
}

[[nodiscard]] layout::OverlayScene overlay_at(const build::OutputRuntime& rt, int slide_index)
{
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.slide_overlays.size())) {
        return {};
    }
    return rt.slide_overlays[static_cast<std::size_t>(slide_index)];
}

/// Whether video object ``obj`` runs once ``plan``'s step has played: on
/// show, or once its click has come.
[[nodiscard]] bool video_plays(const canvas::Object& obj,
                               const std::string& id,
                               const freeform::StepPlan& plan)
{
    if (obj.type != canvas::ObjectType::Video) {
        return false;
    }
    return obj.video.start == canvas::VideoStart::OnShow || plan.playing_after.contains(id);
}

/// A slide's objects at build step ``step``: those shown once it has played,
/// and with ``animate`` those it plays too, each with its build, starting
/// ``after_ms`` into the change (once the slide is in). ``builds_end_ms`` is
/// when the last is done; 0 when nothing plays.
[[nodiscard]] std::vector<SceneObject> objects_at(const build::OutputRuntime& rt,
                                                  int slide_index,
                                                  int step,
                                                  bool animate,
                                                  int after_ms,
                                                  int& builds_end_ms)
{
    builds_end_ms = 0;
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.compiled_slides.size())) {
        return {};
    }
    const auto& slide = rt.compiled_slides[static_cast<std::size_t>(slide_index)];
    const int last = static_cast<int>(slide.steps.size()) - 1;
    // Past this slide's last step (another output's slide has more): fully
    // built, and nothing plays.
    if (step > last) {
        step = last;
        animate = false;
    }
    const auto& plan = slide.steps[static_cast<std::size_t>(std::max(0, step))];
    std::vector<SceneObject> out;
    for (const auto& obj : slide.objects) {
        if (obj.indicator) {
            continue;  // joins the stack with its words (append_resolved_indicators)
        }
        std::vector<SceneBuild> builds;
        if (animate) {
            for (const auto& play : plan.plays) {
                if (play.object_id != obj.id) {
                    continue;
                }
                builds.push_back({.effect = play.animation.effect,
                                  .direction = play.animation.direction,
                                  .start_ms = after_ms + play.start_ms,
                                  .duration_ms = play.animation.duration_ms});
                builds_end_ms = std::max(builds_end_ms, after_ms + play.end_ms());
            }
        }
        if (!plan.shown_after.contains(obj.id) && builds.empty()) {
            continue;
        }
        SceneObject so;
        so.z = obj.z;
        so.id = obj.id;
        so.transition = obj.transition;
        so.text = obj.text;
        so.object = obj.object;
        if (!so.text && !so.object) {
            continue;
        }
        so.builds = std::move(builds);
        so.plays = so.object && video_plays(*so.object, so.id, plan);
        out.push_back(std::move(so));
    }
    return out;
}

/// An indicator's box with its line for this frame, labels ``gap`` spaces
/// apart; no line when there is nothing to show.
[[nodiscard]] layout::OverlayBox indicator_overlay(layout::OverlayBox box,
                                                   const std::vector<IndicatorItem>& items,
                                                   double gap)
{
    if (!items.empty()) {
        box.lines.push_back(indicator_line(items, gap));
    }
    return box;
}

void append_resolved_indicators(SceneFrame& frame, const build::OutputRuntime& rt, int line_index)
{
    if (frame.slide_index < 0
        || frame.slide_index >= static_cast<int>(rt.compiled_slides.size())) {
        return;
    }
    const auto& slide = rt.compiled_slides[static_cast<std::size_t>(frame.slide_index)];
    for (const auto& obj : slide.objects) {
        if (!obj.indicator) {
            continue;
        }
        const auto items = resolve_indicator_items(
            {.codes = rt.verse_codes,
             .line_section = rt.verse_indices,
             .line_to_slide = rt.line_to_slide},
            line_index, frame.slide_index, rt.slide_count(),
            obj.indicator->fallback == freeform::IndicatorFallback::Blank);
        auto box = indicator_overlay(obj.indicator->box, items, obj.indicator->gap);
        box.id = obj.id;
        if (box.lines.empty()) {
            continue;
        }
        // An indicator is a Freeform object like any other; its text just
        // resolves this late. It joins the stack so it keeps its z.
        SceneObject so;
        so.z = obj.z;
        so.id = obj.id;
        so.transition = obj.transition;
        so.text = std::move(box);
        // Its frame, when it has one.
        so.object = obj.object;
        frame.objects.push_back(std::move(so));
    }
    std::stable_sort(frame.objects.begin(), frame.objects.end(),
                     [](const SceneObject& a, const SceneObject& b) { return a.z < b.z; });
}

[[nodiscard]] std::optional<layout::BackgroundFill> bg_at(
    const build::OutputRuntime& rt,
    int slide_index)
{
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.slide_bg_overrides.size())) {
        return std::nullopt;
    }
    return rt.slide_bg_overrides[static_cast<std::size_t>(slide_index)];
}

/// The themed span a shown citation paints as (size / color / font / rise / gap).
[[nodiscard]] model::TextStyle bible_citation_span_style(const theme::ResolvedTheme& theme,
                                                         const build::BibleCitationStyle& cite,
                                                         int body_font_size)
{
    model::TextStyle vs;
    vs.bold = cite.bold;
    vs.italic = cite.italic;
    vs.font_family = cite.font_family;
    vs.color = theme.bible.verse_color;
    vs.margin_right_pct = cite.margin_right_pct;
    vs.margin_for_space = true;
    vs.size_pct = build::bible_citation_size_pct(cite, body_font_size);

    const auto& valign = theme.bible.verse_valign;
    if (valign == "middle") {
        vs.baseline_rise_pct = std::max(0, (100 - vs.size_pct) / 2);
    } else if (valign == "baseline") {
        vs.baseline_rise_pct = 0;
    } else {
        // "super" (default): CSS vertical-align:super.
        vs.baseline_rise_pct = model::k_super_rise_pct;
    }
    return vs;
}

/**
 * Rebuild ``runs`` with every marker as its own ``cite`` span.
 *
 * ``markers`` index ``plain_text(runs)``. A marker's layout space is dropped
 * because the citation's right margin is the painted gap. Body text around a
 * marker keeps its span's style.
 */
[[nodiscard]] model::RichLine restyle_citation_markers(
    const model::RichLine& runs,
    std::string_view plain,
    const std::vector<build::BibleCitationMarker>& markers,
    const model::TextStyle& cite)
{
    model::RichLine out;
    std::size_t mi = 0;
    std::size_t span_start = 0;
    for (const auto& span : runs) {
        const std::size_t span_end = span_start + span.text.size();
        const auto push_body = [&](std::size_t from, std::size_t to) {
            if (to <= from) {
                return;
            }
            model::TextSpan piece = span;
            piece.text = span.text.substr(from - span_start, to - from);
            out.push_back(std::move(piece));
        };
        std::size_t pos = span_start;
        while (pos < span_end) {
            while (mi < markers.size() && markers[mi].end() <= pos) {
                ++mi;
            }
            if (mi == markers.size() || markers[mi].offset >= span_end) {
                push_body(pos, span_end);
                break;
            }
            const auto& m = markers[mi];
            push_body(pos, m.offset);
            if (pos <= m.offset) {
                out.push_back(model::TextSpan{
                    .text = std::string(plain.substr(m.offset, m.length)), .style = cite});
            }
            pos = std::min(span_end, m.end());
        }
        span_start = span_end;
    }
    return out;
}

/// Restyle every shown ``citations`` marker on every row as a citation span.
void apply_bible_citation_spans(SceneTextContent& content,
                                const theme::ResolvedTheme& theme,
                                const build::BibleCitationStyle& cite,
                                const std::vector<std::string>& citations)
{
    if (!cite.shown || citations.empty()) {
        return;
    }
    const model::TextStyle span_style =
        bible_citation_span_style(theme, cite, content.box.font_size);
    for (auto& line : content.lines) {
        const std::string plain = model::plain_text(line.runs);
        const auto markers = build::find_bible_citation_markers(plain, citations);
        if (!markers.empty()) {
            line.runs = restyle_citation_markers(line.runs, plain, markers, span_style);
        }
    }
}

/**
 * Slide position an element's "Show on" rule counts.
 *
 * A song's slide list interleaves two designs: title groups paint from the
 * Titles context, every other group from Song. Counting raw item slides would
 * make "First 1" in the Song design land on a slide that design never paints -
 * the designer sets 1, sees nothing, and has to know to say 2. So each context
 * counts only the slides it actually paints: "First 1" is the first lyric
 * slide, whether or not the song opens with a title.
 *
 * Non-song modes have one design for the whole item, so they count item slides.
 */
struct ContextSlidePos {
    int index = 0;
    int count = 1;
};

[[nodiscard]] ContextSlidePos context_slide_pos(const build::OutputRuntime& rt,
                                                int slide_index,
                                                bool song_mode,
                                                bool title_slide)
{
    if (!song_mode || rt.layout.groups.empty()) {
        return {slide_index, rt.slide_count()};
    }
    int index = 0;
    int count = 0;
    for (std::size_t i = 0; i < rt.layout.groups.size(); ++i) {
        if (rt.layout.groups[i].is_title != title_slide) {
            continue;
        }
        if (static_cast<int>(i) < slide_index) {
            ++index;
        }
        ++count;
    }
    return {index, std::max(1, count)};
}

}  // namespace

std::map<std::string, std::string> frame_token_pool(const SceneExtras& extras)
{
    std::map<std::string, std::string> pool;
    for (const auto name : layout::k_template_variables) {
        pool[layout::token_key(name)] = "";
    }
    for (const auto name : k_bible_ref_variables) {
        pool[layout::token_key(name)] = "";
    }
    for (const auto& [k, v] : extras.copyright_vars) {
        pool[layout::token_key(k)] = v;
    }
    for (const auto& [k, v] : extras.bible_ref_vars) {
        pool[layout::token_key(k)] = v;
    }
    return pool;
}

std::vector<SceneObject> build_theme_element_objects(
    const std::vector<theme::Element>& elements,
    int slide_index,
    int slide_count,
    const std::map<std::string, std::string>& tokens,
    const std::vector<IndicatorItem>& indicator_items,
    bool skip_static_text,
    std::string_view suppress_id,
    const theme::ElementConditions& conditions,
    const std::optional<PictureStandIn>& picture_stand_in)
{
    std::vector<SceneObject> out;
    for (const auto& el :
         theme::visible_elements(elements, slide_index, slide_count, conditions)) {
        const auto& o = el.object;
        if (!suppress_id.empty() && o.id == suppress_id) {
            continue;
        }
        if (skip_static_text && o.type == freeform::ObjectType::Text
            && el.binding == theme::ElementBinding::Static) {
            continue;
        }
        SceneObject so;
        so.z = o.z;
        so.id = o.id;
        so.transition = o.transition;
        switch (el.binding) {
        case theme::ElementBinding::Lyrics:
        case theme::ElementBinding::BibleText:
        case theme::ElementBinding::VideoArea:
        case theme::ElementBinding::ImageArea:
        case theme::ElementBinding::WebArea:
            continue;  // projected onto the typed body fields before layout
        case theme::ElementBinding::Countdown:
        case theme::ElementBinding::Indicator:
        case theme::ElementBinding::Static:
            break;
        }
        switch (o.type) {
        case freeform::ObjectType::Text: {
            auto box = freeform::text_box_from_object(o, tokens);
            box.id = o.id;
            bool has_text = false;
            for (const auto& ln : box.lines) {
                if (!model::plain_text(ln).empty()) {
                    has_text = true;
                    break;
                }
            }
            if (!has_text) {
                continue;  // template resolved to nothing (song opted out, ...)
            }
            so.text = std::move(box);
            if (canvas::paints_frame(o)) {
                so.object = freeform::painted_object(o);
            }
            break;
        }
        case freeform::ObjectType::Indicator: {
            auto box = indicator_overlay(freeform::indicator_box(o), indicator_items,
                                         o.indicator_gap);
            box.id = o.id;
            if (box.lines.empty()) {
                continue;
            }
            so.text = std::move(box);
            if (canvas::paints_frame(o)) {
                so.object = freeform::painted_object(o);
            }
            break;
        }
        case freeform::ObjectType::Clock:
            so.object = freeform::painted_object(o);
            so.object->clock_mode = el.binding == theme::ElementBinding::Countdown
                                        ? ClockMode::Countdown
                                        : ClockMode::WallClock;
            break;
        case freeform::ObjectType::Image:
        case freeform::ObjectType::Video: {
            auto painted = freeform::painted_object(o);
            if (canvas::object_media_url(painted)) {
                so.object = std::move(painted);
            } else if (o.type == freeform::ObjectType::Image && picture_stand_in) {
                so.text = stand_in_label(picture_stand_in->text, o, picture_stand_in->canvas);
                so.text->rotation = o.rotation;
                so.text->id = o.id;
            } else {
                continue;
            }
            break;
        }
        case freeform::ObjectType::Shape:
            so.object = freeform::painted_object(o);
            break;
        case freeform::ObjectType::Table:
            continue;  // painted once tables land
        }
        out.push_back(std::move(so));
    }
    return out;
}

model::TextStyle bible_citation_look(const theme::ResolvedTheme& theme)
{
    return bible_citation_span_style(theme, build::bible_citation_style(theme),
                                     bible_box(theme).font_size);
}

SceneTextContent build_bible_text_content(std::string_view slide_text,
                                          const std::vector<std::string>& citations,
                                          const theme::ResolvedTheme& theme,
                                          layout::ITextMeasurer* measurer)
{
    SceneTextContent content;
    content.box = bible_box(theme);
    content.z = theme.bible.z;
    const auto cite = build::bible_citation_style(theme);
    const layout::WidthMeasurer measure = measurer != nullptr
        ? build::bible_text_measurer(box_font_style(content.box, {}), cite, citations, *measurer,
                                     static_cast<double>(text_avail_w(content.box)))
        : layout::WidthMeasurer{};

    std::size_t start = 0;
    while (start <= slide_text.size()) {
        const std::size_t end = slide_text.find('\n', start);
        const std::string_view piece = end == std::string_view::npos
            ? slide_text.substr(start)
            : slide_text.substr(start, end - start);
        if (measure && !piece.empty()) {
            // Verse text is plain, measured with its citations.
            auto rows = layout::wrap_plain_text_to_width(
                piece, measure, static_cast<double>(text_avail_w(content.box)));
            if (rows.size() <= 1) {
                rows = {std::string(piece)};
            }
            append_visual_fragments(content, rows, piece, SceneLineRole::Normal, {}, 0);
        } else {
            append_runs_line(content, piece, SceneLineRole::Normal, {}, 0);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    apply_bible_citation_spans(content, theme, cite, citations);
    return content;
}

SceneMode scene_mode_from_build_kind(build::BuildKind kind) noexcept
{
    switch (kind) {
    case build::BuildKind::Song:
        return SceneMode::Song;
    case build::BuildKind::Bible:
        return SceneMode::Bible;
    case build::BuildKind::Freeform:
        return SceneMode::Freeform;
    case build::BuildKind::Empty:
        return SceneMode::Empty;
    }
    return SceneMode::Empty;
}

std::vector<SceneObjectVideo> object_videos_at(const build::OutputRuntime& rt, int slide_index, int step)
{
    std::vector<SceneObjectVideo> out;
    if (slide_index < 0 || slide_index >= static_cast<int>(rt.compiled_slides.size())) {
        return out;
    }
    const auto& slide = rt.compiled_slides[static_cast<std::size_t>(slide_index)];
    const int last = static_cast<int>(slide.steps.size()) - 1;
    const auto& plan = slide.steps[static_cast<std::size_t>(std::clamp(step, 0, last))];
    for (const auto& obj : slide.objects) {
        if (!obj.object || obj.object->type != canvas::ObjectType::Video
            || !plan.shown_after.contains(obj.id)) {
            continue;
        }
        const auto url = canvas::object_media_url(*obj.object);
        if (!url || obj.object->filename.empty()) {
            continue;
        }
        out.push_back({.key = video_object_key(*url),
                       .filename = obj.object->filename,
                       .loop = obj.object->video.loop,
                       .mute = obj.object->video.mute,
                       .plays = video_plays(*obj.object, obj.id, plan)});
    }
    return out;
}

SceneFrame build_scene_frame(const SceneFrameRequest& req)
{
    const auto& rt = req.runtime;
    const int slide_index = req.slide_index >= 0 ? req.slide_index : rt.index;

    SceneFrame frame;
    frame.output_id = rt.output_id;
    frame.output_name = rt.name;
    const auto canvas = geom::canvas_or_reference(req.canvas_width, req.canvas_height);
    frame.canvas_width = canvas.width;
    frame.canvas_height = canvas.height;
    frame.mode = req.mode;
    frame.slide_index = std::max(0, slide_index);
    frame.transition_key = req.extras.transition_key;

    const theme::ResolvedTheme& theme = rt.theme_for_slide(slide_index);
    frame.background = background_of(theme, bg_at(rt, slide_index));
    // Each content type changes by its own context's transition; an
    // empty output (cleared) cuts.
    if (const auto ctx = content_context_of(req.mode)) {
        frame.content_transition = theme.behavior.transition(*ctx);
    }
    frame.background_transition =
        model::transition_from_fade(theme.background.enable_fade, theme.background.fade_duration);
    // A Freeform slide carries its own transitions, nothing of a theme's:
    // a background transition it leaves unset is a cut.
    if (slide_index >= 0 && slide_index < static_cast<int>(rt.compiled_slides.size())) {
        const auto& cs = rt.compiled_slides[static_cast<std::size_t>(slide_index)];
        frame.content_transition = cs.transition;
        frame.background_transition = cs.background_transition.value_or(model::Transition{});
    }

    // Image, video and a web page own the canvas: the background is plain
    // black, so letterboxing never reveals a theme background behind them.
    // The black is part of what they show, so it comes and goes with them:
    // media takes no background theme to fade it any other way.
    if (req.mode == SceneMode::Image || req.mode == SceneMode::Video
        || req.mode == SceneMode::Web || req.mode == SceneMode::Live) {
        frame.background.type = layout::BackgroundType::Color;
        frame.background.color = "#000000";
        frame.background.image.clear();
        frame.background.video.clear();
        frame.background_transition = frame.content_transition;
    }
    // Audio paints nothing, background included, so the stacks behind it show.
    if (req.mode == SceneMode::Audio) {
        frame.background = SceneBackground{};
    }

    // A title slide is designed in its own element context; the flag below picks
    // it, and the elements themselves carry placement, font and format.
    const bool title_slide =
        req.mode == SceneMode::Song
        && slide_index >= 0
        && slide_index < static_cast<int>(rt.layout.groups.size())
        && rt.layout.groups[static_cast<std::size_t>(slide_index)].is_title;
    // Whether this output paints that design is its theme's own call. With
    // ``show_title_slides`` off the slide is still there - the cue belongs to
    // the song and every output walks one line cursor - so this output simply
    // stays in its song design: the same chrome as any other slide, with an
    // empty lyrics box (or the opening lines, under follow-lines).
    const bool title_design = title_slide && theme.behavior.show_title_slides;

    frame.image = req.extras.image;
    frame.video = req.extras.video;
    frame.web = req.extras.web;
    frame.live = req.extras.live;
    place_frame_media(frame, theme);
    frame.overlay = overlay_at(rt, slide_index);
    // A slide's builds: a step within the slide plays only its
    // builds; coming in, the slide's transition plays first, then what it
    // comes in with.
    const bool freeform_slide = slide_index >= 0
        && slide_index < static_cast<int>(rt.compiled_slides.size());
    if (freeform_slide && req.within_slide) {
        frame.content_transition = {};
        frame.background_transition = {};
    }
    if (req.transition) {
        frame.content_transition = *req.transition;
        frame.background_transition = *req.transition;
    }
    const int after_ms = !req.within_slide && frame.content_transition.plays()
                             ? frame.content_transition.duration_ms
                             : 0;
    frame.objects = objects_at(rt, slide_index, req.step, req.animate_step, after_ms,
                               frame.builds_end_ms);

    std::optional<theme::ElementContext> element_ctx;
    std::vector<IndicatorItem> indicator_items;

    switch (req.mode) {
    case SceneMode::Song: {
        // A title slide with a theme overlay shows the overlay alone.
        const bool title_with_overlay = title_slide && !frame.overlay.empty();
        if (!title_with_overlay) {
            frame.text = build_song_text(rt, theme, slide_index, req.measurer);
        }
        // A title slide is designed in its own context: the boxes come through
        // the overlay (they gate the upcoming-lines rule in layout), everything
        // else in that list paints here.
        element_ctx = title_design ? theme::ElementContext::Titles
                                   : theme::ElementContext::Song;
        if (frame.overlay.empty()) {
            indicator_items = build_indicator_items(rt, frame.slide_index);
        }
        break;
    }
    case SceneMode::Bible:
        frame.text = build_bible_text(rt, theme, slide_index, req.measurer);
        element_ctx = theme::ElementContext::Bible;
        break;
    case SceneMode::Freeform:
        frame.text = std::nullopt;
        append_resolved_indicators(frame, rt, req.line_index);
        break;
    case SceneMode::Image:
    case SceneMode::Video:
    case SceneMode::Web:
    case SceneMode::Live:
        frame.text = std::nullopt;
        element_ctx = content_context_of(req.mode);
        break;
    case SceneMode::Audio:
    case SceneMode::Empty:
        frame.text = std::nullopt;
        break;
    }

    if (element_ctx) {
        theme::ElementConditions conditions;
        conditions.song_copyright = req.extras.song_copyright;
        const auto pos = context_slide_pos(rt, frame.slide_index,
                                           req.mode == SceneMode::Song, title_design);
        auto themed = build_theme_element_objects(
            theme.elements.at(*element_ctx), pos.index, pos.count,
            frame_token_pool(req.extras), indicator_items,
            *element_ctx == theme::ElementContext::Titles, {}, conditions);
        frame.objects.insert(frame.objects.end(), std::make_move_iterator(themed.begin()),
                             std::make_move_iterator(themed.end()));
        std::stable_sort(frame.objects.begin(), frame.objects.end(),
                         [](const SceneObject& a, const SceneObject& b) { return a.z < b.z; });
    }

    // Measured once here, so every paint of this frame only draws.
    if (req.measurer != nullptr) {
        lay_out_frame_text(frame, *req.measurer);
    }
    return frame;
}

void apply_scene_blank(SceneFrame& frame)
{
    frame.is_blank = true;
    frame.text = std::nullopt;
    frame.overlay = {};
    frame.objects.clear();
    // Nothing is left to build: a blank goes in by the slide's transition
    // alone, not for as long as its builds took.
    frame.builds_end_ms = 0;
    frame.image = std::nullopt;
    frame.video = std::nullopt;
    frame.web = std::nullopt;
    frame.live = std::nullopt;
}

}  // namespace seventhslide::scene
