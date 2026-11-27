#include "seventhslide/build/even_wrap.hpp"

#include <algorithm>

namespace seventhslide::build {

namespace {

[[nodiscard]] int rows_at(const layout::StyledWords& words, const layout::RunMeasurer& measure, double width)
{
    return layout::wrapped_row_count(words, {0, words.size()}, measure, width);
}

}  // namespace

double min_even_width(const layout::StyledWords& words, const layout::RunMeasurer& measure, double box_w)
{
    const int n = words.empty() ? 1 : rows_at(words, measure, box_w);
    if (n <= 1) {
        return box_w;
    }
    // Widest-word floor.
    double lo = 0.0;
    for (std::size_t i = 0; i < words.size(); ++i) {
        lo = std::max(lo, words.width(i, i + 1, measure));
    }
    double hi = box_w;
    for (int iter = 0; iter < 32; ++iter) {
        if (hi - lo <= 0.5) {
            break;
        }
        const double mid = (lo + hi) / 2.0;
        if (rows_at(words, measure, mid) <= n) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

double topheavy_even_width(const layout::StyledWords& words, const layout::RunMeasurer& measure, double box_w)
{
    if (words.size() < 2) {
        return box_w;
    }
    const int n = rows_at(words, measure, box_w);
    if (n <= 1) {
        return box_w;
    }
    for (std::size_t i = 1; i < words.size(); ++i) {
        const double target = words.width(0, i, measure);
        if (target > box_w) {
            break;
        }
        const auto runs = layout::wrap_word_runs(words, 0, words.size(), measure, target);
        if (static_cast<int>(runs.size()) != n) {
            continue;
        }
        bool topheavy = true;
        double previous = words.width(runs.front().start, runs.front().end, measure);
        for (std::size_t j = 1; j < runs.size(); ++j) {
            const double next = words.width(runs[j].start, runs[j].end, measure);
            if (!(previous + 0.5 >= next)) {
                topheavy = false;
                break;
            }
            previous = next;
        }
        if (topheavy) {
            return target;
        }
    }
    return min_even_width(words, measure, box_w);
}

double search_even_target(
    double ideal,
    double box_w,
    double cap,
    const std::function<double(double)>& slide_height)
{
    if (slide_height(ideal) <= cap) {
        return ideal;
    }
    double lo = ideal;
    double hi = box_w;
    for (int iter = 0; iter < 32; ++iter) {
        if (hi - lo <= 0.5) {
            break;
        }
        const double mid = (lo + hi) / 2.0;
        if (slide_height(mid) <= cap) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

double balance_strength_from_percent(int percent) noexcept
{
    const double raw = static_cast<double>(percent) / 100.0;
    return std::max(0.0, std::min(1.0, raw));
}

void even_one_slide(
    const std::vector<int>& indices,
    std::vector<LayoutLine>& lines,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap,
    double strength)
{
    // Each line on the slide is tokenised once; the target search below wraps
    // them all at every step it tries.
    struct SlideLine {
        LayoutLine* line;
        layout::StyledWords words;
    };
    std::vector<SlideLine> slide;
    slide.reserve(indices.size());
    for (const int i : indices) {
        if (i >= 0 && static_cast<std::size_t>(i) < lines.size()) {
            auto& ln = lines[static_cast<std::size_t>(i)];
            slide.push_back({&ln, layout::StyledWords::marked(ln.text)});
        }
    }

    const SlideLine* widest = nullptr;
    double widest_w = -1.0;
    for (const auto& entry : slide) {
        if (!entry.line->evenable) {
            continue;
        }
        const double w = entry.words.width(0, entry.words.size(), measure);
        if (w > widest_w) {
            widest_w = w;
            widest = &entry;
        }
    }
    if (widest == nullptr) {
        return;
    }

    const double ideal = topheavy_even_width(widest->words, measure, box_w);
    if (ideal >= box_w) {
        return;
    }

    auto slide_height = [&](double target) -> double {
        double total = 0.0;
        for (const auto& entry : slide) {
            // A line evening leaves alone keeps the height layout gave it
            // (a chord line's rows are its chords', not its words').
            if (!entry.line->evenable) {
                total += static_cast<double>(entry.line->base_height_px);
                continue;
            }
            const int n = entry.words.empty() ? 1 : rows_at(entry.words, measure, target);
            total += static_cast<double>(n) * static_cast<double>(entry.line->line_height_px)
                + static_cast<double>(entry.line->extra_px);
        }
        return total;
    };

    double target = search_even_target(ideal, box_w, cap, slide_height);
    target = box_w - strength * (box_w - target);
    if (target >= box_w - 0.5) {
        return;
    }

    for (auto& entry : slide) {
        if (!entry.line->evenable) {
            continue;
        }
        const auto runs = layout::wrap_word_runs(entry.words, 0, entry.words.size(), measure, target);
        // Store fragments when the wrap baked breaks; a single row stays empty
        // so paint uses the box wrap.
        entry.line->wrap_fragments.clear();
        entry.line->evened = runs.size() > 1;
        if (runs.size() > 1) {
            for (const auto& run : runs) {
                entry.line->wrap_fragments.push_back(entry.words.text(run));
            }
        }
    }
}

void even_slide_lines(
    LayoutResult& result,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap,
    double strength)
{
    for (const auto& grp : result.groups) {
        even_one_slide(grp.indices, result.lines, measure, box_w, cap, strength);
    }
}

void apply_even_wrap_if_enabled(
    LayoutResult& result,
    const theme::ResolvedTheme& theme,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap)
{
    const double strength = balance_strength_from_percent(theme.behavior.balance_wrapped_strength);
    if (!theme.behavior.balance_wrapped_lines || strength <= 0.0
        || theme.behavior.follow_lines > 0) {
        return;
    }
    even_slide_lines(result, measure, box_w, cap, strength);
}

}  // namespace seventhslide::build
