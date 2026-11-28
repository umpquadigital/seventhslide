#include "seventhslide/freeform/compile.hpp"

#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/freeform/paint.hpp"
#include "seventhslide/freeform/tokens.hpp"
#include "seventhslide/layout/overlay_layout.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/util/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace seventhslide::freeform {
namespace {

[[nodiscard]] std::map<std::string, std::string> token_values(const Document& doc)
{
    std::map<std::string, std::string> values;
    for (const auto& name : collect_field_names(doc)) {
        values[name] = "";
    }
    for (const auto& [name, val] : doc.fields) {
        values[name] = val;
    }
    return values;
}

[[nodiscard]] layout::OverlayBox styled_box(const Object& obj)
{
    layout::OverlayBox box;
    static_cast<canvas::BoxRect&>(box) = obj;
    static_cast<canvas::BoxType&>(box) = obj;
    box.rotation = obj.rotation;
    fill_painted_type(box, obj.type);
    box.line_gap = layout::clamp_overlay_line_gap(obj.line_gap);
    box.letter_spacing = layout::clamp_overlay_letter_spacing(obj.letter_spacing);
    box.effects = obj.effects;
    box.paragraph = obj.paragraph;
    return box;
}

/// The bold, italic and underline a line with no style of its own takes: the
/// box's, or for a follow cue the upcoming ones the box sets.
struct LineFlags {
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

[[nodiscard]] LineFlags line_flags(const Object& obj, bool upcoming)
{
    if (!upcoming) {
        return {obj.bold, obj.italic, obj.underline};
    }
    return {obj.upcoming_bold.value_or(obj.bold), obj.upcoming_italic.value_or(obj.italic),
            obj.upcoming_underline.value_or(obj.underline)};
}

[[nodiscard]] model::RichLine line_from_markup(std::string_view line, LineFlags flags)
{
    return layout::styled_line(model::parse_inline_markup(line), flags.bold, flags.italic,
                               flags.underline);
}

void append_markup_lines(std::vector<model::RichLine>& out, std::string_view text,
                         LineFlags flags, const std::map<std::string, std::string>& values)
{
    const std::string substituted = substitute_fields(text, values);
    if (substituted.empty()) {
        return;  // nothing to show
    }
    // Lines as the editor splits them: a trailing newline is a blank last line.
    for (const auto& line : model::parse_markup_lines(substituted)) {
        out.push_back(layout::styled_line(line, flags.bold, flags.italic, flags.underline));
    }
}

void apply_upcoming_style(model::RichLine& line, const Object& obj)
{
    const int box_px = obj.font_size > 0 ? obj.font_size : canvas::k_default_font_size;
    const int up_px = obj.upcoming_font_size > 0 ? obj.upcoming_font_size : box_px;
    const std::string color = obj.upcoming_color.empty() ? std::string(k_default_upcoming_color)
                                                         : obj.upcoming_color;
    if (line.empty()) {
        line.push_back({});
    }
    for (auto& sp : line) {
        if (sp.style.color.empty()) {
            sp.style.color = color;
        }
        // A run's own size is relative to the upcoming size, as it is to the box's.
        sp.style.size_px = up_px * (std::max(1, sp.style.size_pct) / 100.0);
        sp.style.size_pct = std::max(
            1, static_cast<int>(std::lround(100.0 * sp.style.size_px / box_px)));
    }
}

[[nodiscard]] CueSpan span_for_slide(const Document& doc, const Deck& deck, int slide_index)
{
    for (const auto& span : deck_spans(doc, deck)) {
        if (span.slide_index == slide_index) {
            return span;
        }
    }
    CueSpan empty;
    empty.slide_index = slide_index;
    return empty;
}

/// A linked box's lines for ``cue``: the box's overlay of it, or the cue as it
/// shows in the upcoming style, with its chords when the box shows them.
[[nodiscard]] std::vector<model::RichLine> linked_cue_lines(
    const Deck& deck, const Object& obj, const Cue& cue, bool upcoming, bool chords,
    const std::map<std::string, std::string>& values)
{
    std::vector<model::RichLine> lines;
    std::string markup =
        upcoming ? box_cue_markup(deck, obj, cue) : effective_cue_markup(deck, obj, cue);
    // Chords with no words are nothing to a box that hides chords: not a blank
    // line, which a cue with neither is.
    if (!chords && markup.empty() && !cue_display_chords(deck, cue).empty()) {
        return lines;
    }
    if (chords) {
        markup = with_cue_chords(deck, markup, cue);
    }
    append_markup_lines(lines, markup, line_flags(obj, upcoming), values);
    if (lines.empty()) {
        lines.push_back(line_from_markup("", line_flags(obj, upcoming)));
    }
    for (auto& ol : lines) {
        // Chord rows have a fixed height, whatever size a word of them asks for.
        if (chords && layout::has_chords(model::plain_text(ol))) {
            for (auto& sp : ol) {
                sp.style.size_pct = 100;
            }
        }
        if (upcoming) {
            apply_upcoming_style(ol, obj);
        }
    }
    return lines;
}

/// ``last_shown``, when given, is set to the last cue the box shows (the
/// slide's last, or the last follow cue that fit); -1 for none.
/// A cue's ``more`` lines added to ``lines``, running on from the last of them:
/// a space between, if ``spaced``, in the style of the words either side of it or
/// else plain. Whether there was one.
bool run_on_lines(std::vector<model::RichLine>& lines, std::vector<model::RichLine> more,
                  bool spaced, const Object& obj, bool upcoming)
{
    if (lines.empty()) {
        lines = std::move(more);
        return false;
    }
    auto& into = lines.back();
    const bool space =
        spaced && !model::plain_text(into).empty() && !model::plain_text(more.front()).empty();
    if (space) {
        model::RichLine gap{{" ", {}}};
        const auto flags = line_flags(obj, upcoming);
        gap.front().style.bold = flags.bold;
        gap.front().style.italic = flags.italic;
        if (upcoming) {
            apply_upcoming_style(gap, obj);
        }
        if (into.back().style == more.front().front().style) {
            gap.front().style = into.back().style;
        }
        model::append_styled(into, " ", gap.front().style);
    }
    for (const auto& run : more.front()) {
        model::append_styled(into, run.text, run.style);
    }
    lines.insert(lines.end(), std::make_move_iterator(more.begin() + 1),
                 std::make_move_iterator(more.end()));
    return space;
}

[[nodiscard]] layout::OverlayBox linked_text_box(const Document& doc, const Deck& deck,
                                                 int slide_index, const Object& obj,
                                                 const std::map<std::string, std::string>& values,
                                                 layout::ITextMeasurer& measurer,
                                                 int* last_shown = nullptr)
{
    auto box = styled_box(obj);
    // One column: the upcoming cues are fitted down it.
    box.paragraph.columns = 1;
    box.show_chords = obj.show_chords;
    box.chords = obj.chords;
    if (last_shown) {
        *last_shown = -1;
    }
    const auto span = span_for_slide(doc, deck, slide_index);
    if (span.last_cue < span.first_cue) {
        return box;
    }
    const int cues = static_cast<int>(doc.cues.size());
    if (last_shown) {
        *last_shown = std::min(span.last_cue, cues - 1);
    }

    // The lines of cue ``first`` and of those up to ``last`` that run on from
    // it, one line where they do. ``end`` is the last of them.
    auto cue_lines = [&](int first, int last, bool upcoming, int& end) {
        auto lines = linked_cue_lines(deck, obj, doc.cues[static_cast<std::size_t>(first)], upcoming,
                                      obj.show_chords, values);
        end = first;
        bool spaced = false;  // a space before a cue this box has no line for is still owed
        while (end < last) {
            const auto& next = doc.cues[static_cast<std::size_t>(end + 1)];
            const auto* flow = deck.flow_of(next);
            if (flow == nullptr || !flow->runs_on) {
                break;
            }
            ++end;
            auto more = linked_cue_lines(deck, obj, next, upcoming, obj.show_chords, values);
            spaced = spaced || !flow->in_word;
            // No line, or nothing on it: a verse number this deck hides, alone.
            if (more.empty() || (more.size() == 1 && model::plain_text(more.front()).empty())) {
                continue;
            }
            run_on_lines(lines, std::move(more), spaced, obj, upcoming);
            spaced = false;
        }
        return lines;
    };
    // The room above ``lines`` of ``cue``: only a line above it needs any.
    auto gap_above = [&](const Cue& cue) {
        const auto* flow = deck.flow_of(cue);
        return flow != nullptr && !box.lines.empty() ? flow->gap_above : 0;
    };
    auto add_lines = [&](std::vector<model::RichLine> lines, int gap) {
        if (lines.empty()) {
            return;
        }
        box.gaps_above.push_back(gap);
        box.gaps_above.resize(box.lines.size() + lines.size(), 0);
        box.lines.insert(box.lines.end(), std::make_move_iterator(lines.begin()),
                         std::make_move_iterator(lines.end()));
    };

    // The slide's own cues always show.
    const int last_own = std::min(span.last_cue, cues - 1);
    for (int c = std::max(0, span.first_cue), end = c; c <= last_own; c = end + 1) {
        const int gap = gap_above(doc.cues[static_cast<std::size_t>(c)]);
        add_lines(cue_lines(c, last_own, false, end), gap);
    }
    if (!obj.show_upcoming) {
        return box;
    }

    // Then the cues after them while they fit, measured as paint lays them out
    // (sizes and fonts in the markup included). A box's height is its lines'
    // heights plus the gaps between them, so each cue is measured on its own.
    const double box_h =
        std::max(0.0, box.h - box.paragraph.margins.top - box.paragraph.margins.bottom);
    const double gap = layout::overlay_line_gap_px(box);
    double used = layout::lay_out_overlay(box, measurer).height;
    for (int c = std::max(0, span.last_cue + 1), end = c; c < cues; c = end + 1) {
        auto lines = cue_lines(c, cues - 1, true, end);
        if (lines.empty()) {
            if (last_shown) {
                *last_shown = end;
            }
            continue;
        }
        const bool after_lines = !box.lines.empty();
        // The box's gaps are for its lines so far, not for these.
        std::vector<int> gaps;
        std::swap(box.lines, lines);
        std::swap(box.gaps_above, gaps);
        const double h = layout::lay_out_overlay(box, measurer).height;
        std::swap(box.lines, lines);
        std::swap(box.gaps_above, gaps);
        const int room = gap_above(doc.cues[static_cast<std::size_t>(c)]);
        const double added = (after_lines ? gap + room : 0.0) + h;
        if (used > 0 && used + added > box_h) {
            break;
        }
        used += added;
        add_lines(std::move(lines), room);
        if (last_shown) {
            *last_shown = end;
        }
    }
    return box;
}

[[nodiscard]] std::string overlay_to_markup(const layout::OverlayBox& box)
{
    return model::format_markup_lines(box.lines);
}

[[nodiscard]] CompiledObject compile_object(const Object& obj, const Document& doc, const Deck& deck,
                                            int slide_index,
                                            const std::map<std::string, std::string>& values,
                                            layout::ITextMeasurer& measurer)
{
    CompiledObject out;
    out.z = obj.z;
    out.id = obj.id;
    out.transition = obj.transition;
    switch (obj.type) {
    case ObjectType::Text: {
        auto box = compile_text_object(doc, deck, slide_index, obj, values, measurer);
        box.id = obj.id;
        if (!box.lines.empty()) {
            out.text = std::move(box);
        }
        // The frame behind the words, when the box has one; it paints with or
        // without words.
        if (canvas::paints_frame(obj)) {
            out.object = painted_object(obj);
        }
        break;
    }
    case ObjectType::Image:
    case ObjectType::Video:
    case ObjectType::Shape:
    case ObjectType::Clock:
        out.object = painted_object(obj);
        break;
    case ObjectType::Indicator:
        out.indicator = CompiledIndicator{.box = indicator_box(obj),
                                          .fallback = obj.indicator_fallback,
                                          .gap = obj.indicator_gap};
        if (canvas::paints_frame(obj)) {
            out.object = painted_object(obj);
        }
        break;
    case ObjectType::Table:
        out.object = painted_object(obj);
        for (auto& cell : out.object->table.cells) {
            cell = substitute_fields(cell, values);
        }
        break;
    }
    return out;
}

[[nodiscard]] model::Transition content_clock(const std::vector<CompiledObject>& objects)
{
    int max_ms = 0;
    for (const auto& obj : objects) {
        if (obj.transition.plays()) {
            max_ms = std::max(max_ms, obj.transition.duration_ms);
        }
    }
    if (max_ms <= 0) {
        return {};
    }
    return {model::TransitionType::Fade, max_ms};
}

} // namespace

layout::OverlayBox text_box_from_object(const Object& obj,
                                        const std::map<std::string, std::string>& values)
{
    auto box = styled_box(obj);
    append_markup_lines(box.lines, obj.text, line_flags(obj, false), values);
    return box;
}

layout::OverlayBox compile_text_object(const Document& doc, const Deck& deck, int slide_index,
                                       const Object& obj,
                                       const std::map<std::string, std::string>& values,
                                       layout::ITextMeasurer& measurer)
{
    if (obj.cue_linked && doc.line_control) {
        return linked_text_box(doc, deck, slide_index, obj, values, measurer);
    }
    return text_box_from_object(obj, values);
}

LinkedEdit linked_edit(const Document& doc, const Deck& deck, int slide_index,
                       const Object& obj, layout::ITextMeasurer& measurer)
{
    LinkedEdit out;
    if (obj.type != ObjectType::Text || !obj.cue_linked || !doc.line_control) {
        return out;
    }
    const auto span = span_for_slide(doc, deck, slide_index);
    const int cues = static_cast<int>(doc.cues.size());
    if (span.last_cue < span.first_cue || span.first_cue >= cues) {
        return out;
    }
    int last_shown = -1;
    (void)linked_text_box(doc, deck, slide_index, obj, token_values(doc), measurer, &last_shown);
    out.first_cue = std::max(0, span.first_cue);
    const int last_own = std::min(span.last_cue, cues - 1);
    out.owned = last_own - out.first_cue + 1;
    out.upcoming = std::max(0, last_shown - last_own);

    // One line where the box paints one: only a slide's own cues, and only its
    // follow cues, run on from each other.
    std::vector<model::RichLine> lines;
    bool spaced = false;
    for (int c = out.first_cue; c <= last_own + out.upcoming; ++c) {
        const auto& cue = doc.cues[static_cast<std::size_t>(c)];
        const bool upcoming = c > last_own;
        auto cue_lines = model::parse_markup_lines(upcoming ? box_cue_markup(deck, obj, cue)
                                                            : effective_cue_markup(deck, obj, cue));
        if (cue_lines.empty()) {
            cue_lines.emplace_back();
        }
        if (upcoming) {
            for (auto& line : cue_lines) {
                apply_upcoming_style(line, obj);
            }
        }
        // Line breaks do not count in a cue's chord offsets, so not in its length.
        const std::string plain =
            model::plain_text_lines(model::parse_markup_lines(cue_display_markup(deck, cue)));
        const auto breaks = static_cast<std::size_t>(std::ranges::count(plain, '\n'));
        out.lengths.push_back(plain.size() - breaks);
        out.chords.push_back(cue_display_chords(deck, cue));

        const auto* flow = deck.flow_of(cue);
        const bool runs_on =
            flow != nullptr && flow->runs_on && c != out.first_cue && c != last_own + 1;
        bool space = false;
        if (runs_on) {
            spaced = spaced || !flow->in_word;
            if (plain.empty()) {
                out.spaces_before.push_back(0);
                continue;
            }
            space = run_on_lines(lines, std::move(cue_lines), spaced, obj, upcoming);
        } else {
            lines.insert(lines.end(), std::make_move_iterator(cue_lines.begin()),
                         std::make_move_iterator(cue_lines.end()));
        }
        spaced = false;
        out.spaces_before.push_back(space ? 1 : 0);
    }
    out.markup = model::format_markup_lines(lines);
    return out;
}

ResolvedText resolved_object_text(const Document& doc, const Deck& deck, int slide_index,
                                  const Object& obj, layout::ITextMeasurer& measurer)
{
    const auto values = token_values(doc);
    auto box = compile_text_object(doc, deck, slide_index, obj, values, measurer);
    return {overlay_to_markup(box), std::move(box.gaps_above)};
}

CompiledPlayback compile(const Document& doc,
                         layout::ITextMeasurer* measurer,
                         const std::set<std::string, std::less<>>* only_outputs)
{
    Document working = doc;
    normalize(working);

    layout::ApproxTextMeasurer approx;
    layout::ITextMeasurer& measure = measurer ? *measurer : static_cast<layout::ITextMeasurer&>(approx);

    CompiledPlayback out;
    out.line_control = working.line_control;
    const auto values = token_values(working);

    if (working.line_control) {
        out.lines.reserve(working.cues.size());
        out.line_labels.reserve(working.cues.size());
        auto sections = cue_sections(working.cues);
        out.verse_codes = std::move(sections.codes);
        out.verse_indices = std::move(sections.cue_section);
        for (const auto& cue : working.cues) {
            const auto code = cue_label_code(cue);
            // The line list reads a cue's words; chords are for its boxes. A cue
            // of a verse number alone reads as that.
            const auto ref = cue.show_ref ? cue_verse_ref(cue) : std::nullopt;
            std::string line = cue_text_markup(cue);
            out.lines.push_back(line.empty() && ref ? parse::format_verse_ref(*ref)
                                                    : std::move(line));
            out.line_labels.push_back(code);
        }
    } else {
        const int n = working.decks.empty() ? 0 : working.decks.front().slide_count();
        for (int i = 0; i < n; ++i) {
            const std::string label = std::to_string(i + 1);
            out.lines.push_back(label);
            out.line_labels.push_back(label);
        }
    }

    for (const auto& deck : working.decks) {
        if (only_outputs != nullptr && !only_outputs->contains(deck.output_id)) {
            continue;
        }
        CompiledDeck cd;
        cd.output_id = deck.output_id;
        if (working.line_control) {
            cd.line_to_slide = deck.line_to_slide;
            if (static_cast<int>(cd.line_to_slide.size()) != static_cast<int>(working.cues.size())) {
                cd.line_to_slide.resize(working.cues.size(),
                                        std::max(0, deck.slide_count() - 1));
            }
        } else {
            cd.line_to_slide = identity_map(deck.slide_count());
        }
        cd.slides.reserve(deck.slides.size());
        for (const auto& slide : deck.slides) {
            CompiledSlide cs;
            cs.background = slide.background;
            cs.background_transition = slide.background_transition
                ? slide.background_transition
                : working.background_transition;
            cs.objects.reserve(slide.objects.size());
            const int slide_index = static_cast<int>(cd.slides.size());
            for (const auto& obj : slide.objects) {
                auto compiled = compile_object(obj, working, deck, slide_index, values, measure);
                if (!compiled.text && !compiled.object && !compiled.indicator) {
                    continue;
                }
                cs.objects.push_back(std::move(compiled));
            }
            std::stable_sort(cs.objects.begin(), cs.objects.end(),
                             [](const CompiledObject& a, const CompiledObject& b) {
                                 return a.z < b.z;
                             });
            if (slide.transition) {
                // The slide's own transition is every object's that has none.
                for (auto& obj : cs.objects) {
                    if (!obj.transition.plays()) {
                        obj.transition = *slide.transition;
                    }
                }
            }
            cs.transition = slide.transition ? *slide.transition : content_clock(cs.objects);
            // Its builds, step by step: only for what paints on its own (an
            // indicator joins its words; an empty box is dropped), so no
            // click goes by with nothing to show for it.
            std::set<std::string> painted;
            for (const auto& co : cs.objects) {
                if (!co.indicator && (co.text || co.object)) {
                    painted.insert(co.id);
                }
            }
            std::vector<ObjectAnimations> animated;
            animated.reserve(slide.objects.size());
            for (const auto& obj : slide.objects) {
                if (painted.contains(obj.id)) {
                    animated.push_back({obj.id, obj.animations,
                                        obj.type == ObjectType::Video
                                            && obj.video.start == canvas::VideoStart::OnClick});
                }
            }
            cs.steps = plan_steps(animated);
            cd.slides.push_back(std::move(cs));
        }
        out.decks.push_back(std::move(cd));
    }
    return out;
}

namespace {

[[nodiscard]] std::string preview_key_with(const Document& doc,
                                           const Deck& deck,
                                           int slide_index,
                                           const std::map<std::string, std::string>& values)
{
    if (slide_index < 0 || slide_index >= deck.slide_count()) {
        return {};
    }

    nlohmann::json j;
    const auto& slide = deck.slides[static_cast<std::size_t>(slide_index)];
    if (!slide.background.is_none()) {
        j["slide_bg"] = slide.background;
    }
    j["objects"] = nlohmann::json::array();
    bool has_indicator = false;
    bool has_linked = false;
    layout::ApproxTextMeasurer approx;
    for (const auto& obj : slide.objects) {
        nlohmann::json item = obj;
        item.erase("id");
        if (obj.type == ObjectType::Text) {
            if (obj.cue_linked && doc.line_control) {
                const auto box = compile_text_object(doc, deck, slide_index, obj, values, approx);
                item["text"] = overlay_to_markup(box);
                // The room between its lines shows in the preview, not in its text.
                if (std::ranges::any_of(box.gaps_above, [](int gap) { return gap > 0; })) {
                    item["gaps_above"] = box.gaps_above;
                }
                has_linked = true;
            } else {
                item["text"] = substitute_fields(obj.text, values);
            }
        } else if (obj.type == ObjectType::Indicator) {
            has_indicator = true;
        }
        j["objects"].push_back(std::move(item));
    }
    if (has_indicator || has_linked) {
        nlohmann::json cues = nlohmann::json::array();
        for (const auto& cue : doc.cues) {
            cues.push_back(cue_label_code(cue));
        }
        j["indicator_cues"] = std::move(cues);
        // Where sections start, which a label alone does not say.
        j["indicator_sections"] = cue_sections(doc.cues).cue_section;
        j["line_to_slide"] = deck.line_to_slide;
        j["line_control"] = doc.line_control;
    }

    return util::fnv1a64_hex(j.dump());
}

} // namespace

std::string slide_preview_key(const Document& doc, const Deck& deck, int slide_index)
{
    if (slide_index < 0 || slide_index >= deck.slide_count()) {
        return {};
    }
    return preview_key_with(doc, deck, slide_index, token_values(doc));
}

std::vector<std::string> slide_preview_keys(const Document& doc, const Deck& deck)
{
    std::vector<std::string> keys;
    keys.reserve(deck.slides.size());
    const auto values = token_values(doc);
    for (int s = 0; s < deck.slide_count(); ++s) {
        keys.push_back(preview_key_with(doc, deck, s, values));
    }
    return keys;
}

void SlidePreviewKeys::sync(const Document& doc, const Deck& deck)
{
    Context now{
        .values = token_values(doc),
        .line_control = doc.line_control,
        .cues = doc.cues,
        .output_id = deck.output_id,
        .canvas = deck.canvas,
        .line_to_slide = deck.line_to_slide,
        .cue_flow = deck.cue_flow,
        .hide_verse_refs = deck.hide_verse_refs,
    };
    if (now.values != context_.values || now.output_id != context_.output_id) {
        entries_.clear();
    } else if (now != context_) {
        std::erase_if(entries_, [](const auto& entry) { return entry.second.uses_cues; });
    }
    context_ = std::move(now);
}

const std::string& SlidePreviewKeys::key_of(const Document& doc, const Deck& deck, int slide_index)
{
    const auto& slide = deck.slides[static_cast<std::size_t>(slide_index)];
    auto& entry = entries_[slide.id];
    if (entry.key.empty() || entry.index != slide_index || !(entry.slide == slide)) {
        entry.slide = slide;
        entry.index = slide_index;
        entry.uses_cues = std::ranges::any_of(slide.objects, [&](const Object& obj) {
            return obj.type == ObjectType::Indicator
                   || (obj.type == ObjectType::Text && obj.cue_linked && doc.line_control);
        });
        entry.key = preview_key_with(doc, deck, slide_index, context_.values);
    }
    return entry.key;
}

std::vector<std::string> SlidePreviewKeys::keys(const Document& doc, const Deck& deck)
{
    sync(doc, deck);
    std::vector<std::string> out;
    out.reserve(deck.slides.size());
    for (int s = 0; s < deck.slide_count(); ++s) {
        out.push_back(key_of(doc, deck, s));
    }
    // Slides that are gone go too.
    if (entries_.size() > deck.slides.size()) {
        std::erase_if(entries_, [&](const auto& entry) {
            return std::ranges::none_of(deck.slides,
                                        [&](const Slide& slide) { return slide.id == entry.first; });
        });
    }
    return out;
}

std::string SlidePreviewKeys::key(const Document& doc, const Deck& deck, int slide_index)
{
    if (slide_index < 0 || slide_index >= deck.slide_count()) {
        return {};
    }
    sync(doc, deck);
    return key_of(doc, deck, slide_index);
}

std::optional<LibraryThumbRef> library_thumb_ref(const Document& doc)
{
    const Deck* deck = nullptr;
    for (const auto& candidate : doc.decks) {
        if (candidate.slide_count() > 0) {
            deck = &candidate;
            break;
        }
    }
    if (deck == nullptr) {
        return std::nullopt;
    }
    LibraryThumbRef ref;
    ref.output_id = deck->output_id;
    ref.preview_key = slide_preview_key(doc, *deck, 0);
    return ref;
}

} // namespace seventhslide::freeform
