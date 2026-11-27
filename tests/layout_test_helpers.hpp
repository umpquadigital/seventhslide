#pragma once

// Layout steps the tests drive on their own.

#include <seventhslide/layout/word_wrap.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::test {

/// ``layout::split_word_runs_to_fit`` for one marked-up line, as song layout
/// runs it: each piece is balanced markup keeping its styles, and a line that
/// fits comes back unchanged.
[[nodiscard]] inline std::vector<std::string> split_marked_line_to_fit(
    std::string_view markup,
    const layout::RunMeasurer& measure,
    int line_height_px,
    double max_width_px,
    double max_height_px)
{
    const auto words = layout::StyledWords::marked(markup);
    const int max_rows = std::max(1, static_cast<int>(max_height_px / line_height_px));
    const auto runs = layout::split_word_runs_to_fit(
        words, {0, words.size()}, [&](layout::WrapRun run) {
            return layout::wrapped_row_count(words, run, measure, max_width_px) <= max_rows;
        });
    if (runs.size() <= 1) {
        return {std::string(markup)};
    }
    std::vector<std::string> pieces;
    for (const auto& run : runs) {
        pieces.push_back(words.text(run));
    }
    return pieces;
}

}  // namespace seventhslide::test
