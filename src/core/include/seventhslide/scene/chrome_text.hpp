#pragma once

// Copyright / verse-indicator / wall-clock string helpers.
// Seam: plain C++20 (no Qt). Scene builder + painters consume these.

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::scene {

struct IndicatorItem {
    std::string label;  // uppercased base code (e.g. "V1")
    bool active = false;
};

/// Colour of the indicator labels that are not the current section.
inline constexpr std::string_view k_indicator_inactive_color = "#808080";

/// The indicator's one line: every label, the current one bold, the others in
/// ``k_indicator_inactive_color``, ``gap`` spaces apart (``Object::indicator_gap``).
/// Empty when there are no items.
[[nodiscard]] model::RichLine indicator_line(
    const std::vector<IndicatorItem>& items,
    double gap = freeform::k_default_indicator_gap);

/// Built-in Bible reference tokens. ``ref`` is an alias for ``reference``.
inline constexpr std::string_view k_bible_ref_variables[] = {
    "book",
    "chapter",
    "ref",
    "reference",
    "verse-end",
    "verse-start",
    "verses",
    "version",
};

/// Built-in overlay tokens for copyright (``authors``, ``copyright``,
/// ``ccli-number``, ``ccli-license``, ...). Always contains every
/// ``k_template_variables`` key so unknown braces stay literal and known
/// empties substitute to nothing.
[[nodiscard]] std::map<std::string, std::string> copyright_token_values(
    const model::Song& song,
    std::string_view ccli_licence);

/// Overlay tokens for the Bible reference box. Always contains every
/// ``k_bible_ref_variables`` key.
[[nodiscard]] std::map<std::string, std::string> bible_ref_token_values(
    const model::BiblePassage& passage);

/// Collapse lettered sections (v1a/v1b -> V1); mark the active verse.
[[nodiscard]] std::vector<IndicatorItem> build_indicator_items(
    const build::OutputRuntime& runtime,
    int slide_index);

/// Collapse consecutive same-base codes; ``current_code_index`` is the active
/// entry in ``verse_codes`` (``-1`` = none active). Empty codes are skipped.
/// A Bible verse code (``3:16``) is labelled by its verse number, with its
/// chapter only where the chapter changes.
[[nodiscard]] std::vector<IndicatorItem> build_indicator_items_from_codes(
    const std::vector<std::string>& verse_codes,
    int current_code_index);

/// First line mapped onto ``slide_index``, or ``-1`` if none.
[[nodiscard]] int first_line_on_slide(const std::vector<int>& line_to_slide, int slide_index);

/// ``5/10`` (1-based slide / total). Empty when ``slide_count < 1``.
[[nodiscard]] std::vector<IndicatorItem> slide_number_indicator(int slide_index, int slide_count);

/**
 * Live indicator content. When ``verse_codes`` is empty or the current code is
 * unlabeled, returns a slide-number item - or nothing if ``blank_if_unlabeled``.
 * ``current_code_index < 0`` uses the first line on ``slide_index``.
 */
[[nodiscard]] std::vector<IndicatorItem> resolve_indicator_items(
    const std::vector<std::string>& verse_codes,
    const std::vector<int>& line_to_slide,
    int current_code_index,
    int slide_index,
    int slide_count,
    bool blank_if_unlabeled);

/// ``indicator_line`` as inline markup: ``<b>V1</b> <color=#808080>C1</color>``.
[[nodiscard]] std::string format_indicator_markup(
    const std::vector<IndicatorItem>& items,
    double gap = freeform::k_default_indicator_gap);

/**
 * Wall clock text.
 * ``hour24`` is 0-23 local time.
 */
[[nodiscard]] std::string format_wall_clock_hms(int hour24,
                                               int minute,
                                               int second,
                                               bool clock_24h,
                                               bool clock_seconds);

}  // namespace seventhslide::scene
