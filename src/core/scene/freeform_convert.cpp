#include "seventhslide/scene/freeform_convert.hpp"

#include "seventhslide/freeform/background_copy.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/model/output_library.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/model/transition.hpp"
#include "seventhslide/parse/bible.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/scene/paint_order.hpp"
#include "seventhslide/scene/scene_builder.hpp"
#include "seventhslide/theme/resolved_theme.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::scene {
namespace {

[[nodiscard]] bool span_starts_space(const model::TextSpan& sp) noexcept
{
    return !sp.text.empty()
        && util::is_space(sp.text.front());
}

[[nodiscard]] bool span_ends_space(const model::TextSpan& sp) noexcept
{
    return !sp.text.empty()
        && util::is_space(sp.text.back());
}

void strip_leading_spaces(std::string& text)
{
    std::size_t i = 0;
    while (i < text.size()
           && util::is_space(text[i])) {
        ++i;
    }
    text.erase(0, i);
}

/// A verse number's margin paints instead of the space after it, which its
/// cue's text still has (``TextStyle::margin_for_space``).
void keep_margin_spaces(model::RichLine& rich)
{
    for (std::size_t i = 0; i + 1 < rich.size(); ++i) {
        if (rich[i].style.margin_for_space && !span_starts_space(rich[i + 1])) {
            rich[i + 1].text.insert(rich[i + 1].text.begin(), ' ');
        }
    }
}

void merge_continue_line(model::RichLine& dest, model::RichLine src)
{
    if (src.empty()) {
        return;
    }
    if (dest.empty()) {
        dest = std::move(src);
        return;
    }
    if (span_ends_space(dest.back()) && span_starts_space(src.front())) {
        strip_leading_spaces(src.front().text);
        if (src.front().text.empty()) {
            src.erase(src.begin());
        }
    } else if (!span_ends_space(dest.back()) && !src.empty()
               && !span_starts_space(src.front())) {
        if (dest.back().style == src.front().style) {
            dest.back().text.push_back(' ');
        } else {
            dest.push_back(model::TextSpan{" ", {}});
        }
    }
    if (src.empty()) {
        return;
    }
    if (dest.back().style == src.front().style) {
        dest.back().text += src.front().text;
        dest.insert(dest.end(), src.begin() + 1, src.end());
    } else {
        dest.insert(dest.end(), src.begin(), src.end());
    }
}

[[nodiscard]] std::string join_overlay_text(const layout::OverlayBox& box)
{
    return model::format_markup_lines(box.lines);
}

/// Add one laid-out line to ``lines``, the spaces its margins stand for kept. A
/// soft-wrap continuation joins the last line kept instead of starting one.
void append_scene_line(std::vector<model::RichLine>& lines, model::RichLine rich,
                       const SceneTextLine& source, const SceneTextContent& text)
{
    // A line with a style of its own takes nothing from its box (``styled_line``),
    // and rows joined back into a line become one: all of them carry it.
    const bool styled = std::ranges::any_of(text.lines, [](const SceneTextLine& line) {
        return std::ranges::any_of(line.runs,
                                   [](const auto& run) { return run.style.has_inline_style(); });
    });
    if (styled) {
        for (auto& run : rich) {
            run.style.bold = run.style.bold || text.box.bold;
            run.style.italic = run.style.italic || text.box.italic;
        }
    }
    keep_margin_spaces(rich);
    if (source.wrap_kept && !lines.empty()) {
        // A break of its own, its space kept so the text still reads as the cue.
        if (!lines.back().empty()) {
            model::append_styled(lines.back(), " ", lines.back().back().style);
        }
        lines.push_back(std::move(rich));
    } else if (source.wrap_in_word && !lines.empty()) {
        for (const auto& span : rich) {
            model::append_styled(lines.back(), span.text, span.style);
        }
    } else if (source.wrap_continue && !lines.empty()) {
        merge_continue_line(lines.back(), std::move(rich));
    } else {
        lines.push_back(std::move(rich));
    }
}

/// A laid-out box's vertical alignment for a Freeform object, which always
/// names one.
[[nodiscard]] std::string map_valign(std::string_view v)
{
    auto align = freeform::read_vertical_align(v);
    return align.empty() ? "middle" : align;
}

void apply_overlay_style(freeform::Object& obj, const layout::OverlayBox& box)
{
    static_cast<canvas::BoxRect&>(obj) = box;
    static_cast<canvas::BoxType&>(obj) = box;
    obj.rotation = box.rotation;
    canvas::fill_type_defaults(obj);
    obj.vertical_align = map_valign(box.vertical_align);
    // What the words carry: their effects and paragraphs. The frame behind
    // them is the scene object's (see object_from_scene).
    obj.effects = box.effects;
    obj.paragraph = box.paragraph;
}

/// The frame a scene object paints behind its words, onto the flattened
/// object: its fill, line and the effects round them.
void apply_frame(freeform::Object& obj, const canvas::Object& painted)
{
    obj.fill = painted.fill;
    obj.line = painted.line;
    obj.effects = painted.effects;
}

void apply_scene_box_style(freeform::Object& obj, const SceneTextBoxStyle& box)
{
    // The body box's rectangle is its words'; the element's is that plus
    // the margins, which the object keeps as its own.
    const auto& m = box.margins;
    obj.x = box.x - std::max(0.0, m.left);
    obj.y = box.y - std::max(0.0, m.top);
    obj.w = box.width + std::max(0.0, m.left) + std::max(0.0, m.right);
    obj.h = box.height + std::max(0.0, m.top) + std::max(0.0, m.bottom);
    obj.paragraph.margins = m;
    obj.fill = box.fill;
    obj.line = box.line;
    obj.effects = box.effects;
    obj.opacity = box.opacity;
    obj.font_family = box.font_family;
    obj.font_size = box.font_size;
    obj.font_color = box.color;
    obj.bold = box.bold;
    obj.italic = box.italic;
    obj.underline = box.underline;
    obj.text_align = box.align;
    canvas::fill_type_defaults(obj);
    obj.vertical_align = map_valign(box.valign);
    obj.line_height = box.line_height > 0 ? box.line_height : 1.2;
    obj.line_gap = 0;
    obj.letter_spacing = box.letter_spacing;
}

[[nodiscard]] freeform::Object text_from_overlay(freeform::Document& doc, const layout::OverlayBox& box)
{
    freeform::Object obj;
    obj.id = freeform::alloc_id(doc, 'o');
    obj.type = freeform::ObjectType::Text;
    apply_overlay_style(obj, box);
    obj.text = join_overlay_text(box);
    return obj;
}

[[nodiscard]] std::string join_active_scene_text(const SceneTextContent& text)
{
    bool any_active = false;
    bool any_follow = false;
    for (const auto& line : text.lines) {
        if (line.role == SceneLineRole::Active) {
            any_active = true;
        } else if (line.role == SceneLineRole::Follow) {
            any_follow = true;
        }
    }
    std::vector<model::RichLine> lines;
    lines.reserve(text.lines.size());
    for (const auto& line : text.lines) {
        // Show-upcoming leftovers are Follow (or untagged Normal next to Active).
        // Only Active lines belong in the unlink snapshot / cue span.
        if (any_active || any_follow) {
            if (line.role != SceneLineRole::Active) {
                continue;
            }
        }
        append_scene_line(lines, line.runs, line, text);
    }
    return model::format_markup_lines(lines);
}

/// The body box as a box linked to the document's cues.
[[nodiscard]] freeform::Object text_from_scene(freeform::Document& doc, const SceneTextContent& text)
{
    freeform::Object obj;
    obj.id = freeform::alloc_id(doc, 'o');
    obj.type = freeform::ObjectType::Text;
    apply_scene_box_style(obj, text.box);
    obj.cue_linked = true;
    obj.text = join_active_scene_text(text);
    // The types the theme set its active line and its other lines in.
    canvas::LineType active;
    canvas::LineType others;
    bool saw_active = false;
    bool saw_follow = false;
    for (const auto& line : text.lines) {
        if (line.role == SceneLineRole::Follow && !saw_follow) {
            saw_follow = true;
            others = line.type;
        } else if (line.role == SceneLineRole::Active && !saw_active) {
            saw_active = true;
            active = line.type;
        }
    }
    // The box's own look is the active line's, whether or not a line
    // follows it on this slide; a following line keeps the theme's size
    // for it, or the box's.
    if (saw_follow) {
        obj.show_upcoming = true;
        obj.upcoming_color = others.color.empty()
                                 ? std::string(freeform::k_default_upcoming_color)
                                 : others.color;
    }
    obj.upcoming_font_size = others.size > 0 ? others.size
                             : text.box.font_size > 0 ? text.box.font_size
                                                      : canvas::k_default_font_size;
    const auto type = canvas::painted_type(text.box, active);
    obj.font_family = type.family;
    obj.font_size = type.size;
    obj.font_color = type.color;
    obj.bold = type.bold;
    obj.italic = type.italic;
    obj.underline = type.underline;
    // The other lines' flags where they are not the active line's: a run can
    // turn bold on, but not off under a bold box.
    const auto other = canvas::painted_type(text.box, others);
    if (other.bold != type.bold) {
        obj.upcoming_bold = other.bold;
    }
    if (other.italic != type.italic) {
        obj.upcoming_italic = other.italic;
    }
    if (other.underline != type.underline) {
        obj.upcoming_underline = other.underline;
    }
    canvas::fill_type_defaults(obj);
    return obj;
}

/// The body box fades with the slide; every other element carries its own
/// transition on the object.
void apply_content_transition(freeform::Object& obj, const model::Transition& spec)
{
    if (spec.plays()) {
        obj.transition = spec;
    }
}

/// Indicator element id -> its label gap, which the painted box does not carry.
[[nodiscard]] std::map<std::string, double> indicator_gaps_of(
    const std::vector<theme::Element>& elements)
{
    std::map<std::string, double> gaps;
    for (const auto& el : elements) {
        if (el.object.type == freeform::ObjectType::Indicator) {
            gaps.emplace(el.object.id, el.object.indicator_gap);
        }
    }
    return gaps;
}

/**
 * One of the frame's canvas objects as the slide's, or none for one with
 * nothing to copy.
 *
 * Theme chrome is not a special case any more: copyright, reference, indicator
 * and clock are canvas objects on the frame, exactly like a Freeform document's
 * own, so snapshotting them is a straight translation. Indicators are matched
 * back to their theme element (``indicator_gaps``) so they stay live in the
 * saved document instead of freezing to the labels this one slide happened to
 * show.
 */
[[nodiscard]] std::optional<freeform::Object> object_from_scene(
    freeform::Document& doc, const SceneObject& so,
    const std::map<std::string, double>& indicator_gaps)
{
    freeform::Object obj;
    if (so.text) {
        const auto& box = *so.text;
        if (const auto gap = indicator_gaps.find(so.id); gap != indicator_gaps.end()) {
            obj.type = freeform::ObjectType::Indicator;
            obj.indicator_gap = gap->second;
            apply_overlay_style(obj, box);
        } else {
            obj.type = freeform::ObjectType::Text;
            apply_overlay_style(obj, box);
            obj.text = join_overlay_text(box);
        }
        if (so.object) {
            apply_frame(obj, *so.object);
        }
    } else if (so.object) {
        // The painted copy is the object: it paints where and how it did
        // on the frame.
        static_cast<canvas::Object&>(obj) = *so.object;
    } else {
        return std::nullopt;
    }
    // The id is this document's.
    obj.id = freeform::alloc_id(doc, 'o');
    if (so.transition.plays()) {
        obj.transition = so.transition;
    }
    return obj;
}

/// Own only the group's active lines (plus a title slide's own line). Follow /
/// upcoming leftovers are paint-only.
///
/// ``normalize`` clamps the deck map to non-decreasing, so an unclaimed cue must
/// never be filled with a slide that sits *after* the ones its neighbours own:
/// a single early ``last`` would drag every later cue onto the final slide.
/// Unclaimed cues therefore inherit the previous cue's slide, and only the
/// trailing run - cues past this output's own lines, from a longer master -
/// falls back to the last slide.
[[nodiscard]] std::vector<int> active_line_to_slide(const build::LayoutResult& layout,
                                                    int cue_count, int slide_count)
{
    const int cues = std::max(0, cue_count);
    const int last = std::max(0, slide_count - 1);
    std::vector<int> map(static_cast<std::size_t>(cues), -1);
    const int n_groups = static_cast<int>(layout.groups.size());
    int last_owned = -1;
    const auto claim_cue = [&](int cue, int slide) {
        if (cue < 0 || cue >= cues) {
            return;
        }
        map[static_cast<std::size_t>(cue)] = std::min(slide, last);
        last_owned = std::max(last_owned, cue);
    };
    // A cue is a row of the song, and a line of this output shows no row, one,
    // or several (see LayoutResult::row_lines): a line's number is not a cue's.
    std::vector<std::vector<int>> line_cues(layout.lines.size());
    for (std::size_t row = 0; row < layout.row_lines.size(); ++row) {
        const int line = layout.row_lines[row];
        if (line >= 0 && static_cast<std::size_t>(line) < line_cues.size()) {
            line_cues[static_cast<std::size_t>(line)].push_back(static_cast<int>(row));
        }
    }
    const auto claim = [&](int line_index, int slide) {
        if (line_index < 0 || static_cast<std::size_t>(line_index) >= line_cues.size()) {
            return;
        }
        if (layout.row_lines.empty()) {
            claim_cue(line_index, slide);
        }
        for (const int cue : line_cues[static_cast<std::size_t>(line_index)]) {
            claim_cue(cue, slide);
        }
    };
    for (int s = 0; s < n_groups; ++s) {
        const auto& g = layout.groups[static_cast<std::size_t>(s)];
        claim(g.owner_line, s);
        const int n_active = std::max(0, g.active_count);
        for (int i = 0; i < n_active && i < static_cast<int>(g.indices.size()); ++i) {
            claim(g.indices[static_cast<std::size_t>(i)], s);
        }
    }
    int carry = 0;
    for (int i = 0; i < cues; ++i) {
        int& v = map[static_cast<std::size_t>(i)];
        if (v >= 0) {
            carry = v;
        } else {
            v = (i > last_owned) ? last : carry;
        }
    }
    return map;
}

void apply_show_upcoming_from_group(freeform::Object& obj, const layout::SlideGroup& group)
{
    if (group.active_count < 0
        || group.active_count >= static_cast<int>(group.indices.size())) {
        return;
    }
    obj.show_upcoming = true;
    if (obj.upcoming_color.empty()) {
        obj.upcoming_color = freeform::k_default_upcoming_color;
    }
    if (obj.upcoming_font_size <= 0) {
        obj.upcoming_font_size = obj.font_size > 0 ? obj.font_size : canvas::k_default_font_size;
    }
}

void stamp_linked_overlays(freeform::Document& doc, freeform::Deck& deck)
{
    for (int s = 0; s < deck.slide_count(); ++s) {
        auto& slide = deck.slides[static_cast<std::size_t>(s)];
        for (auto& obj : slide.objects) {
            if (obj.type != freeform::ObjectType::Text || !obj.cue_linked || obj.text.empty()) {
                continue;
            }
            std::map<std::string, std::string> overlays;
            if (freeform::split_linked_overlays(doc, deck, s, obj, obj.text, overlays)) {
                obj.cue_overlays = std::move(overlays);
            }
        }
    }
}

/// What a slide copies of the background ``bg`` it was built with. A picture
/// or video a theme owns is named by its theme URL: the app copies it into the
/// library before the item is saved (``BackgroundCopy::copy_from_url``).
[[nodiscard]] freeform::BackgroundCopy slide_background(const theme::ThemeBackground& bg)
{
    auto copy = freeform::background_copy_of(
        bg.fill(), model::transition_from_fade(bg.enable_fade, bg.fade_duration));
    if (copy.waits_for_copy()) {
        auto& file = copy.background.kind == freeform::SlideBackgroundKind::Video
                         ? copy.background.video
                         : copy.background.image;
        file = copy.copy_from_url;
    }
    return copy;
}

} // namespace

freeform::Document document_from_build(const build::SlideBuildResult& built,
                                       const std::vector<model::OutputConfig>& outputs,
                                       layout::ITextMeasurer& measurer,
                                       const FreeformConvertOptions& opts)
{
    freeform::Document doc;
    doc.name = opts.name.empty() ? "Untitled" : opts.name;
    doc.line_control = true;

    // A passage's cues carry their verse. Its lines name it (``3:16``) and the
    // first line of each verse starts with it; the cue shows it instead, and
    // only where an output showed verse numbers.
    const bool bible = built.kind == build::BuildKind::Bible;
    const bool show_refs = bible && std::ranges::any_of(built.outputs, [](const auto& rt) {
        return rt.theme.bible.shown && rt.theme.bible.show_verse_numbers;
    });
    for (std::size_t i = 0; i < built.player.all_lines.size(); ++i) {
        freeform::Cue cue;
        cue.id = freeform::alloc_id(doc, 'c');
        cue.text = built.player.all_lines[i];
        // A song's chords go with it: its boxes show them where chords are on.
        if (i < built.player.all_line_chords.size() && !built.player.all_line_chords[i].empty()) {
            cue.text = built.player.all_line_chords[i];
        }
        const auto ref = bible && i < built.player.all_line_labels.size()
                             ? parse::parse_verse_ref(built.player.all_line_labels[i])
                             : std::nullopt;
        if (ref) {
            cue.chapter = ref->chapter;
            cue.verse = ref->verse;
            if (const auto lead = parse::split_leading_verse_ref(cue.text);
                lead && lead->ref == *ref) {
                cue.text = std::string(lead->rest);
                cue.show_ref = show_refs;
            }
        } else if (!built.outputs.empty()) {
            const auto& rt = built.outputs.front();
            if (i < rt.verse_indices.size()) {
                const int vi = rt.verse_indices[i];
                if (vi >= 0 && vi < static_cast<int>(rt.verse_codes.size())) {
                    const auto label = parse::verse_label_from_code(
                        rt.verse_codes[static_cast<std::size_t>(vi)]);
                    cue.verse_type = label.type;
                    cue.verse_number = label.number;
                }
            }
        }
        doc.cues.push_back(std::move(cue));
    }

    const auto mode = scene_mode_from_build_kind(built.kind);
    const auto element_ctx = built.kind == build::BuildKind::Bible
                                 ? theme::ElementContext::Bible
                                 : theme::ElementContext::Song;

    for (const auto& rt : built.outputs) {
        freeform::Deck deck;
        deck.output_id = rt.output_id;
        const auto* oc = model::find_output_by_id(outputs, rt.output_id);
        const auto canvas = oc != nullptr ? model::output_canvas(*oc) : geom::k_reference_canvas;
        const int canvas_w = canvas.width;
        const int canvas_h = canvas.height;
        // The objects below are captured from frames painted on this canvas.
        deck.canvas = canvas;
        const int n = std::max(1, rt.slide_count());
        for (int s = 0; s < n; ++s) {
            freeform::Slide slide;
            slide.id = freeform::alloc_id(doc, 's');
            // The slide's own copy of the background it was built with, which a
            // theme chip may have changed from the slide before.
            // One with nothing to paint and no fade copies nothing, so it keeps
            // the document's transition for a background given it later.
            auto bg = slide_background(rt.theme_for_slide(s).background);
            if (!bg.background.is_none() || bg.transition.plays()) {
                slide.background = std::move(bg.background);
                slide.background_transition = bg.transition;
            }
            SceneFrameRequest req{
                .runtime = rt,
                .mode = mode,
                .canvas_width = canvas_w,
                .canvas_height = canvas_h,
                .slide_index = s,
                .measurer = &measurer,
            };
            req.extras.copyright_vars = opts.copyright_vars;
            req.extras.song_copyright = opts.song_copyright;
            req.extras.bible_ref_vars = opts.bible_ref_vars;
            const auto frame = build_scene_frame(req);
            const auto& theme = rt.theme_for_slide(s);
            const auto gaps = indicator_gaps_of(theme.elements.at(element_ctx));
            // In the order the frame paints them, so each keeps its place in
            // the stack whatever z its theme element had.
            int z = 0;
            for (const auto& step : paint_order(frame)) {
                std::optional<freeform::Object> obj;
                switch (step.part) {
                case PaintPart::Body:
                    obj = text_from_scene(doc, *frame.text);
                    // An output that showed the song's chords keeps showing them.
                    if (built.kind == build::BuildKind::Song) {
                        obj->show_chords = theme.song.show_chords;
                        obj->chords = theme.song.chords;
                    }
                    // How its verse numbers look, shown here or for when they are.
                    if (built.kind == build::BuildKind::Bible) {
                        obj->verse_ref_look = model::format_inline_markup(
                            {model::TextSpan{.text = "0", .style = bible_citation_look(theme)}});
                    }
                    if (s < static_cast<int>(rt.layout.groups.size())) {
                        apply_show_upcoming_from_group(
                            *obj, rt.layout.groups[static_cast<std::size_t>(s)]);
                    }
                    apply_content_transition(*obj, frame.content_transition);
                    break;
                case PaintPart::TitleBox:
                    obj = text_from_overlay(doc, frame.overlay.boxes[step.index]);
                    apply_content_transition(*obj, frame.content_transition);
                    break;
                case PaintPart::Object:
                    obj = object_from_scene(doc, frame.objects[step.index], gaps);
                    break;
                case PaintPart::Media:
                    // A song or passage shows none.
                    break;
                }
                if (obj) {
                    obj->z = z++;
                    slide.objects.push_back(std::move(*obj));
                }
            }
            deck.slides.push_back(std::move(slide));
        }

        if (!rt.layout.groups.empty()) {
            deck.line_to_slide = active_line_to_slide(
                rt.layout, static_cast<int>(doc.cues.size()), deck.slide_count());
        } else {
            deck.line_to_slide = rt.line_to_slide;
        }
        // A passage's text runs on across the verses and pieces a slide holds.
        if (bible && rt.theme.bible.shown) {
            deck.hide_verse_refs = show_refs && !rt.theme.bible.show_verse_numbers;
            for (std::size_t c = 1; c < doc.cues.size() && c < deck.line_to_slide.size(); ++c) {
                if (deck.line_to_slide[c] == deck.line_to_slide[c - 1]) {
                    deck.cue_flow.push_back({.id = doc.cues[c].id, .runs_on = true});
                }
            }
        }
        // A line split into cues for another output, shown whole on this one,
        // still wraps as one line here.
        const auto& row_lines = rt.layout.row_lines;
        const auto plain_of = [](std::string_view markup) {
            return model::plain_text(model::parse_inline_markup(markup));
        };
        std::size_t read = 0;  // how much of the line the rows so far have read
        int line_above = -1;
        for (std::size_t row = 0; row < row_lines.size() && row < doc.cues.size(); ++row) {
            const int line = row_lines[row];
            if (line < 0) {
                continue;
            }
            freeform::CueFlow flow{.id = doc.cues[row].id};
            const auto& ln = rt.layout.lines[static_cast<std::size_t>(line)];
            // With its chords: they are as much of its spacing as its words.
            const std::string whole = plain_of(ln.chord_text.empty() ? ln.text : ln.chord_text);
            const std::string text = plain_of(doc.cues[row].text);
            if (line != line_above) {
                read = 0;
                // The room a song keeps between verses, when both are shown.
                const auto& behavior =
                    rt.theme_for_slide(rt.layout.line_to_slide[static_cast<std::size_t>(line)])
                        .behavior;
                if (line_above >= 0
                    && build::needs_verse_gap(rt.layout, behavior.fluid_slides, behavior.verse_gap,
                                              line, line_above)) {
                    flow.gap_above = behavior.verse_gap;
                }
            }
            const auto at = whole.find(text, read);
            if (line == line_above) {
                flow.runs_on = true;
                flow.in_word = at != std::string::npos
                    ? at == read
                    : read >= whole.size() || !util::is_space(whole[read]);
            }
            if (at != std::string::npos) {
                read = at + text.size();
            }
            line_above = line;
            if (flow.runs_on || flow.gap_above > 0) {
                deck.cue_flow.push_back(std::move(flow));
            }
        }
        stamp_linked_overlays(doc, deck);
        doc.decks.push_back(std::move(deck));
    }

    if (doc.decks.empty()) {
        std::vector<std::string> output_ids;
        for (const auto& oc : outputs) {
            output_ids.push_back(oc.id);
        }
        doc = freeform::make_document(doc.name, output_ids);
        return doc;
    }

    if (doc.cues.empty()) {
        const int slides = doc.decks.front().slide_count();
        for (int i = 0; i < slides; ++i) {
            freeform::Cue cue;
            cue.id = freeform::alloc_id(doc, 'c');
            cue.text = std::to_string(i + 1);
            doc.cues.push_back(std::move(cue));
        }
    }
    freeform::normalize(doc);

    if (!built.outputs.empty()) {
        const auto& first = built.outputs.front().theme.background;
        const auto bg = model::transition_from_fade(first.enable_fade, first.fade_duration);
        if (bg.plays()) {
            doc.background_transition = bg;
        }
    }

    return doc;
}

}  // namespace seventhslide::scene
