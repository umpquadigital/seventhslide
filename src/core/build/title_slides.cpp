#include "seventhslide/build/title_slides.hpp"

#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::build {

bool verse_order_allows_title_slide(std::string_view verse_order) noexcept
{
    // Empty order -> implicit title. Explicit order needs a t1 token.
    const auto tokens = util::split_ws(verse_order);
    return tokens.empty() || std::ranges::any_of(tokens, [](std::string_view token) {
               return parse::is_title_code(util::to_lower(token));
           });
}

bool title_slide_mode_active(std::string_view verse_order) noexcept
{
    return verse_order_allows_title_slide(verse_order);
}

bool should_prepend_title_line(
    bool title_mode_active,
    std::string_view verse_order) noexcept
{
    if (!title_mode_active) {
        return false;
    }
    return verse_order.find_first_not_of(" \t\n\r") == std::string_view::npos;
}

bool should_show_title_upcoming(
    bool title_mode_active,
    const theme::ResolvedTheme& theme,
    bool this_output_has_overlay) noexcept
{
    return title_mode_active && theme.behavior.follow_lines > 0
        && theme.behavior.title_slide_show_lines && !this_output_has_overlay;
}

void prepend_title_line(LayoutResult& result, bool show_upcoming_lines)
{
    // Shift existing lines' verse indices (+1) and group indices (+1).
    for (auto& line : result.lines) {
        ++line.verse_index;
    }
    for (auto& g : result.groups) {
        for (int& idx : g.indices) {
            ++idx;
        }
    }
    // The title is the first row too; every output of a song prepends it.
    for (int& line : result.row_lines) {
        if (line >= 0) {
            ++line;
        }
    }
    result.row_lines.insert(result.row_lines.begin(), 0);

    LayoutLine title;
    title.text = "";
    title.label = "t1";
    title.verse_index = 0;
    title.is_title = true;
    title.evenable = false;
    title.line_height_px = 0;

    result.lines.insert(result.lines.begin(), std::move(title));
    result.verse_codes.insert(result.verse_codes.begin(), "t1");

    std::vector<layout::SlideGroup> shifted = std::move(result.groups);
    layout::SlideGroup title_group;
    title_group.is_title = true;
    // The prepended line sits at 0 and stays this slide's own, even when the
    // window below it is borrowed from the first lyric slide.
    title_group.owner_line = 0;
    if (show_upcoming_lines && !shifted.empty()) {
        title_group.indices = shifted.front().indices;
        title_group.active_count = 0;
    } else {
        title_group.indices = {0};
        title_group.active_count = 1;
    }
    result.groups.clear();
    result.groups.push_back(std::move(title_group));
    for (auto& g : shifted) {
        result.groups.push_back(std::move(g));
    }
    result.line_to_slide = layout::build_line_to_slide(result.groups, result.lines.size());
}

bool show_upcoming_on_title(LayoutResult& result, std::size_t group_index)
{
    auto& groups = result.groups;
    for (std::size_t j = group_index + 1; j < groups.size(); ++j) {
        if (groups[j].is_title) {
            continue;
        }
        auto& title = groups[group_index];
        // Keep the title line as this slide's own before the window below it
        // is borrowed - it is what keeps the line pointing at this slide.
        if (title.owner_line < 0 && !title.indices.empty()) {
            title.owner_line = title.indices.front();
        }
        title.indices = groups[j].indices;
        title.active_count = 0;
        return true;
    }
    return false;
}

void apply_title_upcoming(LayoutResult& result)
{
    for (std::size_t i = 0; i < result.groups.size(); ++i) {
        if (result.groups[i].is_title) {
            (void)show_upcoming_on_title(result, i);
        }
    }
    result.line_to_slide = layout::build_line_to_slide(result.groups, result.lines.size());
}

void apply_title_slide_layout(
    LayoutResult& result,
    const theme::ResolvedTheme& theme,
    std::string_view verse_order,
    bool title_mode_active,
    bool this_output_has_overlay)
{
    if (!title_mode_active) {
        return;
    }
    const bool upcoming = should_show_title_upcoming(
        title_mode_active, theme, this_output_has_overlay);
    if (should_prepend_title_line(title_mode_active, verse_order)) {
        prepend_title_line(result, upcoming);
    } else if (upcoming) {
        apply_title_upcoming(result);
    }
}

}  // namespace seventhslide::build
