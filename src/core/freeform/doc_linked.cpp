// Text linked to cues: per-cue styling overlays on a linked text object.

#include "seventhslide/freeform/doc_edit.hpp"

#include "seventhslide/model/rich_text.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>

namespace seventhslide::freeform {
namespace {

[[nodiscard]] std::string strip_newlines(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '\n' && c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

[[nodiscard]] std::string markup_plain_joined(std::string_view markup)
{
    const auto lines = model::parse_markup_lines(std::string(markup));
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out.push_back('\n');
        }
        out += model::plain_text(lines[i]);
    }
    return out;
}

void append_plain_slice(model::RichLine& dest, const model::RichLine& src, std::size_t from,
                        std::size_t to)
{
    std::size_t seen = 0;
    for (const auto& sp : src) {
        const std::size_t a = seen;
        const std::size_t b = seen + sp.text.size();
        const std::size_t lo = std::max(from, a);
        const std::size_t hi = std::min(to, b);
        if (lo < hi) {
            model::TextSpan cut = sp;
            cut.text = sp.text.substr(lo - a, hi - lo);
            dest.push_back(std::move(cut));
        }
        seen = b;
    }
}

[[nodiscard]] std::string slice_markup_plain_range(std::string_view markup, std::size_t start,
                                                   std::size_t end)
{
    if (end < start) {
        end = start;
    }
    const auto lines = model::parse_markup_lines(std::string(markup));
    std::vector<model::RichLine> out;
    model::RichLine current;
    std::size_t pos = 0;
    auto flush = [&]() {
        out.push_back(std::move(current));
        current.clear();
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string p = model::plain_text(lines[i]);
        const std::size_t line_start = pos;
        const std::size_t line_end = pos + p.size();
        if (line_end > start && line_start < end) {
            const std::size_t a = start > line_start ? start - line_start : 0;
            const std::size_t b = end < line_end ? end - line_start : p.size();
            append_plain_slice(current, lines[i], a, b);
        }
        pos = line_end;
        if (i + 1 < lines.size()) {
            if (start <= pos && pos < end) {
                flush();
            }
            pos += 1;
        }
    }
    out.push_back(std::move(current));
    if (out.empty()) {
        out.emplace_back();
    }
    return model::format_markup_lines(out);
}

/// Where the ``n``th character that is not a newline sits in ``joined``
/// (``markup_plain_joined``); the end when there are fewer.
[[nodiscard]] std::size_t joined_index_of_char(std::string_view joined, std::size_t n)
{
    std::size_t seen = 0;
    for (std::size_t i = 0; i < joined.size(); ++i) {
        if (joined[i] == '\n' || joined[i] == '\r') {
            continue;
        }
        if (seen == n) {
            return i;
        }
        ++seen;
    }
    return joined.size();
}

/// The style ``markup``'s first character paints in.
[[nodiscard]] model::TextStyle first_char_style(std::string_view markup)
{
    for (const auto& line : model::parse_markup_lines(markup)) {
        for (const auto& span : line) {
            if (!span.text.empty()) {
                return span.style;
            }
        }
    }
    return {};
}

[[nodiscard]] model::TextStyle default_ref_style()
{
    model::TextStyle style;
    style.size_pct = k_cue_ref_size_pct;
    style.baseline_rise_pct = model::k_super_rise_pct;
    return style;
}

/// ``ref`` in ``style``, then the space before the cue's text, in that text's
/// style, and ``body``, if any.
[[nodiscard]] std::string ref_then_body(parse::VerseRef ref, const model::TextStyle& style,
                                        std::string_view body)
{
    const model::RichLine line{
        model::TextSpan{.text = parse::format_verse_ref(ref), .style = style}};
    if (body.empty()) {
        return model::format_inline_markup(line);
    }
    auto lines = model::parse_markup_lines(body);
    model::RichLine first = line;
    model::append_styled(first, " ", first_char_style(body));
    for (const auto& run : lines.front()) {
        model::append_styled(first, run.text, run.style);
    }
    lines.front() = std::move(first);
    return model::format_markup_lines(lines);
}

/// The look ``obj`` draws a verse number in.
[[nodiscard]] model::TextStyle ref_look(const Object& obj)
{
    return obj.verse_ref_look.empty() ? default_ref_style() : first_char_style(obj.verse_ref_look);
}

/// ``markup`` with ``obj``'s bold, italic and underline on its runs, where it has
/// a style of its own and so would not otherwise take them (``styled_line``).
[[nodiscard]] std::string with_box_flags(const Object& obj, std::string markup)
{
    if (!obj.bold && !obj.italic && !obj.underline) {
        return markup;
    }
    auto lines = model::parse_markup_lines(markup);
    const bool styled = std::ranges::any_of(lines, [](const model::RichLine& line) {
        return std::ranges::any_of(line,
                                   [](const auto& run) { return run.style.has_inline_style(); });
    });
    if (!styled) {
        return markup;
    }
    for (auto& line : lines) {
        for (auto& run : line) {
            run.style.bold = run.style.bold || obj.bold;
            run.style.italic = run.style.italic || obj.italic;
            run.style.underline = run.style.underline || obj.underline;
        }
    }
    return model::format_markup_lines(lines);
}

/// ``body`` (a cue's new text, no styles of its own) with what ``styled`` (its old
/// text, styled) has on the words that did not change, and its line breaks.
[[nodiscard]] std::string restyled_body(std::string_view styled, std::string_view body)
{
    model::RichLine flat;
    std::vector<std::size_t> breaks;
    std::size_t old_size = 0;
    const auto old_lines = model::parse_markup_lines(styled);
    for (std::size_t l = 0; l < old_lines.size(); ++l) {
        if (l > 0) {
            breaks.push_back(old_size);
        }
        for (const auto& run : old_lines[l]) {
            model::append_styled(flat, run.text, run.style);
            old_size += run.text.size();
        }
    }
    const std::string old_text = model::plain_text(flat);
    const std::string next = strip_newlines(markup_plain_joined(body));
    const auto out = model::reconcile_rich_line(flat, next);

    // A break keeps its place from whichever end of the line did not change.
    std::size_t head = 0;
    while (head < old_text.size() && head < next.size() && old_text[head] == next[head]) {
        ++head;
    }
    std::size_t tail = 0;
    while (tail < old_text.size() - head && tail < next.size() - head
           && old_text[old_text.size() - 1 - tail] == next[next.size() - 1 - tail]) {
        ++tail;
    }
    std::vector<std::size_t> kept;
    for (const std::size_t at : breaks) {
        if (at <= head) {
            kept.push_back(at);
        } else if (old_text.size() - at <= tail) {
            kept.push_back(at + next.size() - old_text.size());
        }
    }
    std::vector<model::RichLine> lines(1);
    std::size_t at = 0;
    std::size_t k = 0;
    for (const auto& run : out) {
        std::size_t from = 0;
        while (k < kept.size() && kept[k] <= at + run.text.size()) {
            const std::size_t cut = kept[k++] - at;
            model::append_styled(lines.back(), std::string_view(run.text).substr(from, cut - from),
                                 run.style);
            lines.emplace_back();
            from = cut;
        }
        model::append_styled(lines.back(), std::string_view(run.text).substr(from), run.style);
        at += run.text.size();
    }
    return model::format_markup_lines(lines);
}

/// ``cue``'s verse when it shows it on ``deck``.
[[nodiscard]] std::optional<parse::VerseRef> shown_ref(const Deck& deck, const Cue& cue)
{
    return cue.show_ref && !deck.hide_verse_refs ? cue_verse_ref(cue) : std::nullopt;
}

/// Bytes of plain text a shown verse puts before ``cue``'s text: the verse
/// and the space after it.
[[nodiscard]] std::size_t ref_prefix_size(const Deck& deck, const Cue& cue)
{
    const auto ref = shown_ref(deck, cue);
    return ref ? parse::format_verse_ref(*ref).size() + 1 : 0;
}

} // namespace

std::string cue_text_markup(const Cue& cue)
{
    return layout::take_chord_marks(cue.text).markup;
}

std::string cue_display_markup(const Deck& deck, const Cue& cue)
{
    const auto ref = shown_ref(deck, cue);
    const std::string body = cue_text_markup(cue);
    return ref ? ref_then_body(*ref, default_ref_style(), body) : body;
}

std::vector<layout::ChordMark> cue_display_chords(const Deck& deck, const Cue& cue)
{
    auto marks = layout::take_chord_marks(cue.text).marks;
    const std::size_t prefix = ref_prefix_size(deck, cue);
    for (auto& mark : marks) {
        mark.at += prefix;
    }
    return marks;
}

std::string with_cue_chords(const Deck& deck, std::string_view markup, const Cue& cue)
{
    return layout::put_chord_marks(markup, cue_display_chords(deck, cue));
}

std::string cue_text_with_chords(const Deck& deck, const Cue& cue,
                                 std::vector<layout::ChordMark> display_chords)
{
    const std::size_t prefix = ref_prefix_size(deck, cue);
    const std::string body = cue_text_markup(cue);
    const std::size_t body_size =
        model::plain_text_lines(model::parse_markup_lines(body)).size();
    for (auto& mark : display_chords) {
        // A chord on the shown verse goes on the text's first character.
        mark.at = std::min(mark.at > prefix ? mark.at - prefix : 0, body_size);
    }
    return layout::put_chord_marks(body, display_chords);
}

bool linked_overlay_matches(std::string_view cue_text, std::string_view overlay_markup)
{
    return strip_newlines(markup_plain_joined(cue_text))
        == strip_newlines(markup_plain_joined(overlay_markup));
}

std::string box_cue_markup(const Deck& deck, const Object& obj, const Cue& cue)
{
    const auto ref = shown_ref(deck, cue);
    const std::string body = cue_text_markup(cue);
    return with_box_flags(obj, ref ? ref_then_body(*ref, ref_look(obj), body) : body);
}

std::string effective_cue_markup(const Deck& deck, const Object& obj, const Cue& cue)
{
    const auto it = obj.cue_overlays.find(cue.id);
    if (it == obj.cue_overlays.end()
        || !linked_overlay_matches(cue_display_markup(deck, cue), it->second)) {
        return box_cue_markup(deck, obj, cue);
    }
    return it->second;
}

std::string linked_snapshot_markup(const Document& doc, const Deck& deck, int slide_index,
                                   const Object& obj)
{
    if (!doc.line_control || slide_index < 0 || slide_index >= deck.slide_count()) {
        return {};
    }
    for (const auto& span : deck_spans(doc, deck)) {
        if (span.slide_index != slide_index || span.last_cue < span.first_cue) {
            continue;
        }
        std::string out;
        bool spaced = false;  // a space before a cue the text has no line for is still owed
        const int n = static_cast<int>(doc.cues.size());
        for (int c = span.first_cue; c <= span.last_cue && c < n; ++c) {
            if (c < 0) {
                continue;
            }
            const auto& cue = doc.cues[static_cast<std::size_t>(c)];
            const std::string markup = effective_cue_markup(deck, obj, cue);
            const auto* flow = deck.flow_of(cue);
            const bool runs_on = flow != nullptr && flow->runs_on;
            spaced = spaced || (runs_on && !flow->in_word);
            // Chords with no words: the text has no line for them. Nor does it
            // for nothing, on a line that goes on.
            if (markup.empty() && (runs_on || !cue_display_chords(deck, cue).empty())) {
                continue;
            }
            if (!out.empty()) {
                if (!runs_on) {
                    out.push_back('\n');
                } else if (spaced) {
                    out.push_back(' ');
                }
            }
            spaced = false;
            out += markup;
        }
        return out;
    }
    return {};
}

namespace {

/// Whether a space comes between cue ``c``, with text, and the next on its line
/// with any, up to ``last``. Cues with none between them can each owe it.
[[nodiscard]] bool space_owed_after(const Document& doc, const Deck& deck, int c, int last)
{
    bool owed = false;
    for (int k = c + 1; k <= last && k < static_cast<int>(doc.cues.size()); ++k) {
        const auto& cue = doc.cues[static_cast<std::size_t>(k)];
        const auto* flow = deck.flow_of(cue);
        if (flow == nullptr || !flow->runs_on) {
            return false;
        }
        owed = owed || !flow->in_word;
        if (!markup_plain_joined(cue_display_markup(deck, cue)).empty()) {
            return owed;
        }
    }
    return false;
}

}  // namespace

EditResult split_linked_overlays(const Document& doc, const Deck& deck, int slide_index,
                                 const Object& obj, std::string_view display_markup,
                                 std::map<std::string, std::string>& out)
{
    out.clear();
    if (!doc.line_control) {
        return EditResult::fail("line control is off");
    }
    CueSpan span;
    span.slide_index = slide_index;
    bool found = false;
    for (const auto& s : deck_spans(doc, deck)) {
        if (s.slide_index == slide_index) {
            span = s;
            found = true;
            break;
        }
    }
    if (!found || span.last_cue < span.first_cue) {
        return EditResult::fail("empty cue span");
    }
    const std::string plain = markup_plain_joined(display_markup);
    std::size_t i = 0;
    const int n = static_cast<int>(doc.cues.size());
    for (int c = span.first_cue; c <= span.last_cue && c < n; ++c) {
        if (c < 0) {
            continue;
        }
        const auto& cue = doc.cues[static_cast<std::size_t>(c)];
        const std::string display = cue_display_markup(deck, cue);
        const std::string need = strip_newlines(markup_plain_joined(display));
        // Chords with no words: a box that hides chords has no line for them.
        // Text left for the last cue to take is not theirs to pass over.
        if (need.empty() && !cue_display_chords(deck, cue).empty()
            && (i >= plain.size()
                || (c < span.last_cue && plain[i] != '\n' && plain[i] != '\r'))) {
            continue;
        }
        const std::size_t start = i;
        std::string got;
        while (got != need) {
            if (i >= plain.size()) {
                return EditResult::fail("linked text no longer matches cues");
            }
            if (plain[i] != '\n' && plain[i] != '\r') {
                got.push_back(plain[i]);
            }
            ++i;
        }
        if (c < span.last_cue) {
            const auto& next = doc.cues[static_cast<std::size_t>(c + 1)];
            // A cue running on from this one is on its line. Between it and the
            // next on that line with text is a space, nothing inside a cut word.
            // A line break typed before the space stays this cue's, one after it
            // the next's, and one can stand for it; nothing else can.
            if (const auto* flow = deck.flow_of(next); flow != nullptr && flow->runs_on) {
                std::size_t end = i;
                if (!need.empty() && space_owed_after(doc, deck, c, span.last_cue)) {
                    std::size_t j = i;
                    while (j < plain.size() && (plain[j] == '\n' || plain[j] == '\r')) {
                        ++j;
                    }
                    if (j < plain.size() && plain[j] == ' ') {
                        end = j;
                        i = j + 1;
                    } else if (j == i) {
                        return EditResult::fail("linked text no longer matches cues");
                    }
                }
                const std::string overlay = slice_markup_plain_range(display_markup, start, end);
                if (overlay != box_cue_markup(deck, obj, cue)) {
                    out[cue.id] = overlay;
                }
                continue;
            }
            std::size_t nl_run = 0;
            std::size_t j = i;
            while (j < plain.size() && (plain[j] == '\n' || plain[j] == '\r')) {
                ++nl_run;
                ++j;
            }
            if (nl_run < 1) {
                return EditResult::fail("linked text no longer matches cues");
            }
            // Blank lines after a cue are its own, unless they are the row of a
            // cue of chords with no words coming next.
            const bool next_has_row = strip_newlines(markup_plain_joined(cue_display_markup(deck, next)))
                                          .empty()
                && !cue_display_chords(deck, next).empty();
            if (!next_has_row) {
                i += (nl_run - 1);
            }
            const std::size_t end = i;
            const std::string overlay = slice_markup_plain_range(display_markup, start, end);
            if (overlay != box_cue_markup(deck, obj, cue)) {
                out[cue.id] = overlay;
            }
            ++i;
        } else {
            while (i < plain.size() && (plain[i] == '\n' || plain[i] == '\r')) {
                ++i;
            }
            if (i != plain.size()) {
                return EditResult::fail("linked text no longer matches cues");
            }
            const std::string overlay = slice_markup_plain_range(display_markup, start, i);
            if (overlay != box_cue_markup(deck, obj, cue)) {
                out[cue.id] = overlay;
            }
        }
    }
    return EditResult::success();
}

EditResult owned_linked_markup(const Document& doc, const Deck& deck, int slide_index,
                               int upcoming, std::string_view markup, std::string& owned)
{
    owned = std::string(markup);
    if (upcoming <= 0) {
        return EditResult::success();
    }
    const auto spans = deck_spans(doc, deck);
    const auto span = std::find_if(spans.begin(), spans.end(), [&](const CueSpan& s) {
        return s.slide_index == slide_index;
    });
    const int n = static_cast<int>(doc.cues.size());
    if (span == spans.end() || span->last_cue < span->first_cue) {
        return EditResult::fail("empty cue span");
    }
    // The characters the slide's own cues take, then the follow cues', with the
    // spaces between those that run on, as ``linked_edit`` writes them.
    std::size_t own_chars = 0;
    std::string follow;
    bool spaced = false;
    bool on_line = false;
    for (int c = std::max(0, span->first_cue); c < n && c <= span->last_cue + upcoming; ++c) {
        const auto& cue = doc.cues[static_cast<std::size_t>(c)];
        std::string plain = strip_newlines(markup_plain_joined(cue_display_markup(deck, cue)));
        const auto* flow = deck.flow_of(cue);
        const bool runs_on = flow != nullptr && flow->runs_on && c != span->first_cue
            && c != span->last_cue + 1;
        if (!runs_on) {
            spaced = false;
            on_line = false;
        } else {
            spaced = spaced || !flow->in_word;
        }
        if (!plain.empty()) {
            if (spaced && on_line) {
                plain.insert(plain.begin(), ' ');
            }
            spaced = false;
            on_line = true;
        }
        if (c <= span->last_cue) {
            own_chars += plain.size();
        } else {
            follow += plain;
        }
    }
    const std::string joined = markup_plain_joined(markup);
    const std::size_t follow_start = joined_index_of_char(joined, own_chars);
    if (strip_newlines(joined.substr(follow_start)) != follow) {
        return EditResult::fail("linked text no longer matches cues");
    }
    // One line break parts the slide's cues from the follow cues; any more
    // were typed after the last of its own.
    std::size_t own_end = follow_start;
    if (own_end > 0 && joined[own_end - 1] == '\n') {
        --own_end;
    }
    owned = slice_markup_plain_range(markup, 0, own_end);
    return EditResult::success();
}

namespace {

/// A sample of ``style``, as ``Object::verse_ref_look`` and ``cue_ref_looks`` hold it.
[[nodiscard]] std::string look_sample(const model::TextStyle& style)
{
    return model::format_inline_markup({model::TextSpan{.text = "0", .style = style}});
}

/// ``obj``'s styling of ``before`` carried to ``after``, as ``deck`` now shows it.
/// A number going keeps its look on the box, for when it comes back.
void carry_overlay(const Deck& deck, Object& obj, const Cue& before, const Cue& after,
                   const std::optional<parse::VerseRef>& old_ref, std::string_view old_display)
{
    const auto new_ref = shown_ref(deck, after);
    const std::string new_display = cue_display_markup(deck, after);
    auto it = obj.cue_overlays.find(before.id);
    if (it != obj.cue_overlays.end() && !linked_overlay_matches(old_display, it->second)) {
        obj.cue_overlays.erase(it);
        it = obj.cue_overlays.end();
    }
    const auto kept_look = obj.cue_ref_looks.find(before.id);
    const bool returning = new_ref && kept_look != obj.cue_ref_looks.end();
    if (it == obj.cue_overlays.end() && !returning) {
        return;
    }

    std::string body = cue_text_markup(after);
    std::optional<model::TextStyle> number;
    if (it != obj.cue_overlays.end()) {
        const std::string overlay = it->second;
        if (old_ref) {
            number = first_char_style(overlay);
        }
        // The old verse's characters, then the one space before the text.
        const std::size_t body_start =
            old_ref ? parse::format_verse_ref(*old_ref).size() + 1 : 0;
        const std::string joined = markup_plain_joined(overlay);
        const std::string styled =
            slice_markup_plain_range(overlay, joined_index_of_char(joined, body_start), joined.size());
        if (cue_text_markup(before) == body) {
            body = styled;
        } else if (const auto own = model::parse_markup_lines(body);
                   std::ranges::none_of(own, [](const model::RichLine& line) {
                       return std::ranges::any_of(
                           line, [](const auto& run) { return run.style.has_inline_style(); });
                   })) {
            body = restyled_body(styled, body);
        }
    }
    std::string next = with_box_flags(obj, std::move(body));
    if (new_ref) {
        const auto style = returning ? first_char_style(kept_look->second)
                           : number  ? *number
                                     : first_char_style(with_box_flags(
                                         obj, look_sample(ref_look(obj))));
        next = ref_then_body(*new_ref, style, next);
        if (returning) {
            obj.cue_ref_looks.erase(kept_look);
        }
    } else if (number && look_sample(*number) != with_box_flags(obj, look_sample(ref_look(obj)))) {
        obj.cue_ref_looks[before.id] = look_sample(*number);
    }
    it = obj.cue_overlays.find(before.id);
    if (next == box_cue_markup(deck, obj, after) || !linked_overlay_matches(new_display, next)) {
        if (it != obj.cue_overlays.end()) {
            obj.cue_overlays.erase(it);
        }
    } else {
        obj.cue_overlays[before.id] = std::move(next);
    }
}

}  // namespace

void carry_cue_overlays(Document& doc, const Cue& before, const Cue& after)
{
    for (auto& deck : doc.decks) {
        const std::string old_display = cue_display_markup(deck, before);
        const auto old_ref = shown_ref(deck, before);
        for (auto& slide : deck.slides) {
            for (auto& obj : slide.objects) {
                carry_overlay(deck, obj, before, after, old_ref, old_display);
            }
        }
    }
}

void show_deck_verse_refs(Document& doc, Deck& deck)
{
    if (!deck.hide_verse_refs) {
        return;
    }
    std::vector<std::string> old_displays;
    old_displays.reserve(doc.cues.size());
    for (const auto& cue : doc.cues) {
        old_displays.push_back(cue_display_markup(deck, cue));
    }
    deck.hide_verse_refs = false;
    for (std::size_t c = 0; c < doc.cues.size(); ++c) {
        for (auto& slide : deck.slides) {
            for (auto& obj : slide.objects) {
                carry_overlay(deck, obj, doc.cues[c], doc.cues[c], std::nullopt, old_displays[c]);
            }
        }
    }
}

void prune_cue_overlays(Document& doc)
{
    std::unordered_set<std::string> ids;
    ids.reserve(doc.cues.size());
    for (const auto& cue : doc.cues) {
        if (!cue.id.empty()) {
            ids.insert(cue.id);
        }
    }
    for (auto& deck : doc.decks) {
        for (auto& slide : deck.slides) {
            for (auto& obj : slide.objects) {
                std::erase_if(obj.cue_ref_looks,
                              [&](const auto& look) { return !ids.contains(look.first); });
                if (obj.cue_overlays.empty()) {
                    continue;
                }
                for (auto it = obj.cue_overlays.begin(); it != obj.cue_overlays.end();) {
                    if (ids.find(it->first) == ids.end()) {
                        it = obj.cue_overlays.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
}

} // namespace seventhslide::freeform
