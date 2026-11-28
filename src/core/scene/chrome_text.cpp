#include <seventhslide/scene/chrome_text.hpp>

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/model/theme_keys.hpp"
#include "seventhslide/parse/bible.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seventhslide::scene {
namespace {

[[nodiscard]] int current_verse_index(const build::OutputRuntime& rt, int slide_index)
{
    for (std::size_t i = 0; i < rt.line_to_slide.size(); ++i) {
        if (rt.line_to_slide[i] != slide_index) {
            continue;
        }
        if (i < rt.verse_indices.size()) {
            return rt.verse_indices[i];
        }
        return -1;
    }
    return -1;
}

/**
 * ``gap`` spaces of the box font between two labels: whole spaces, then one
 * space scaled to the rest. No run is larger than the box font, so a wide gap
 * never makes the row taller, and the gap is plain whitespace, so the labels
 * still wrap there.
 */
void append_indicator_gap(model::RichLine& line, double gap)
{
    const auto pct = static_cast<int>(std::lround(freeform::clamp_indicator_gap(gap) * 100.0));
    const int whole = pct / 100;
    const int rest = pct % 100;
    if (whole > 0) {
        model::TextSpan spaces;
        spaces.text.assign(static_cast<std::size_t>(whole), ' ');
        line.push_back(std::move(spaces));
    }
    if (rest > 0) {
        model::TextSpan part;
        part.text = " ";
        // ``<size>`` stops at 10%: a sliver of a gap rounds up to that.
        part.style.size_pct = model::clamp_size_pct(rest);
        line.push_back(std::move(part));
    }
}

}  // namespace

int first_line_on_slide(std::span<const int> line_to_slide, int slide_index)
{
    for (std::size_t i = 0; i < line_to_slide.size(); ++i) {
        if (line_to_slide[i] == slide_index) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<IndicatorItem> build_indicator_items_from_sections(
    std::span<const std::string> section_codes,
    int current_section)
{
    std::vector<IndicatorItem> items;
    // A Bible verse shows its number alone, and its chapter too where the
    // chapter changes from the verse before: ``16 17 18``, ``36 4:1``.
    int chapter = 0;
    for (std::size_t i = 0; i < section_codes.size(); ++i) {
        const auto& code = section_codes[i];
        if (code.empty()) {
            continue;
        }
        IndicatorItem item;
        if (const auto ref = parse::parse_verse_ref(code)) {
            item.label = chapter == 0 || chapter == ref->chapter ? std::to_string(ref->verse)
                                                                 : code;
            chapter = ref->chapter;
        } else {
            const auto base = parse::base_verse_code(code);
            item.label = base.empty() ? "?" : util::to_upper(base);
        }
        item.active = static_cast<int>(i) == current_section;
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<IndicatorItem> slide_number_indicator(int slide_index, int slide_count)
{
    if (slide_count < 1) {
        return {};
    }
    const int n = slide_count;
    const int shown = std::clamp(slide_index, 0, n - 1) + 1;
    IndicatorItem item;
    item.label = std::to_string(shown) + "/" + std::to_string(n);
    item.active = true;
    return {std::move(item)};
}

std::vector<IndicatorItem> resolve_indicator_items(
    const IndicatorSections& play,
    int current_line,
    int slide_index,
    int slide_count,
    bool blank_if_unlabeled)
{
    const int line = current_line >= 0 ? current_line
                                       : first_line_on_slide(play.line_to_slide, slide_index);
    int section = -1;
    if (line >= 0 && line < static_cast<int>(play.line_section.size())) {
        section = play.line_section[static_cast<std::size_t>(line)];
    }
    const bool labelled = section >= 0 && section < static_cast<int>(play.codes.size())
        && !play.codes[static_cast<std::size_t>(section)].empty();
    if (!labelled) {
        if (blank_if_unlabeled) {
            return {};
        }
        return slide_number_indicator(slide_index, slide_count);
    }
    return build_indicator_items_from_sections(play.codes, section);
}

model::RichLine indicator_line(const std::vector<IndicatorItem>& items, double gap)
{
    // The default gap is the one space each label has always ended with, in
    // the label's own style, so existing indicators paint exactly as before.
    const bool default_gap = freeform::clamp_indicator_gap(gap) == freeform::k_default_indicator_gap;
    model::RichLine line;
    line.reserve(default_gap ? items.size() : items.size() * 3);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const bool last = i + 1 == items.size();
        model::TextSpan span;
        span.text = items[i].label;
        if (!last && default_gap) {
            span.text.push_back(' ');
        }
        if (items[i].active) {
            span.style.bold = true;
        } else {
            span.style.color = std::string(k_indicator_inactive_color);
        }
        line.push_back(std::move(span));
        if (!last && !default_gap) {
            append_indicator_gap(line, gap);
        }
    }
    return line;
}

std::string format_indicator_markup(const std::vector<IndicatorItem>& items, double gap)
{
    const auto line = indicator_line(items, gap);
    return line.empty() ? std::string{} : model::format_inline_markup(line);
}

std::map<std::string, std::string> copyright_token_values(const model::Song& song,
                                                          std::string_view ccli_licence)
{
    return layout::song_token_values(layout::song_template_vars(song, ccli_licence));
}

namespace {

[[nodiscard]] std::string format_bible_reference_citation(const model::BiblePassage& passage)
{
    if (!passage.ref.empty()) {
        return passage.ref;
    }
    if (passage.book.empty()) {
        return {};
    }
    std::string ref = passage.book;
    if (passage.chapter > 0) {
        ref.push_back(' ');
        ref += std::to_string(passage.chapter);
        if (passage.verse_start > 0) {
            ref.push_back(':');
            ref += std::to_string(passage.verse_start);
            if (passage.verse_end > passage.verse_start) {
                ref.push_back('-');
                ref += std::to_string(passage.verse_end);
            }
        }
    }
    return ref;
}

[[nodiscard]] std::string format_bible_verses_token(const model::BiblePassage& passage)
{
    if (passage.verse_start <= 0) {
        return {};
    }
    if (passage.verse_end > passage.verse_start) {
        return std::to_string(passage.verse_start) + "-"
            + std::to_string(passage.verse_end);
    }
    return std::to_string(passage.verse_start);
}

}  // namespace

std::map<std::string, std::string> bible_ref_token_values(const model::BiblePassage& passage)
{
    std::map<std::string, std::string> vars;
    for (const auto name : k_bible_ref_variables) {
        vars[std::string(name)] = "";
    }
    vars["book"] = passage.book;
    vars["chapter"] = passage.chapter > 0 ? std::to_string(passage.chapter) : "";
    vars["verse-start"] = passage.verse_start > 0 ? std::to_string(passage.verse_start) : "";
    vars["verse-end"] = passage.verse_end > 0 ? std::to_string(passage.verse_end) : "";
    vars["verses"] = format_bible_verses_token(passage);
    const auto ref = format_bible_reference_citation(passage);
    vars["reference"] = ref;
    vars["ref"] = ref;
    vars["version"] = passage.version;
    return vars;
}

std::vector<IndicatorItem> build_indicator_items(const build::OutputRuntime& runtime,
                                                 int slide_index)
{
    return build_indicator_items_from_sections(runtime.verse_codes,
                                               current_verse_index(runtime, slide_index));
}

std::string format_wall_clock_hms(int hour24,
                                  int minute,
                                  int second,
                                  bool clock_24h,
                                  bool clock_seconds)
{
    int h = hour24;
    std::string suffix;
    if (!clock_24h) {
        suffix = (h >= 12) ? " PM" : " AM";
        h = h % 12;
        if (h == 0) {
            h = 12;
        }
    }
    char buf[32];
    if (clock_24h) {
        if (clock_seconds) {
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, minute, second);
        } else {
            std::snprintf(buf, sizeof(buf), "%02d:%02d", h, minute);
        }
    } else if (clock_seconds) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, minute, second);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", h, minute);
    }
    return std::string(buf) + suffix;
}

}  // namespace seventhslide::scene
