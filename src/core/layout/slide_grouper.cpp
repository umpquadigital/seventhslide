#include "seventhslide/layout/slide_grouper.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::layout {

SlideGrouper::SlideGrouper(int max_visual_px, int follow_lines, bool prevent_mixed_active)
    : max_visual_px_(std::max(1, max_visual_px))
    , follow_lines_(follow_lines)
    , prevent_mixed_active_(prevent_mixed_active)
{
}

void SlideGrouper::add_line(
    int line_index, int base_height_px, int active_height_px, int verse_index)
{
    buffer_.push_back(BufLine{
        line_index,
        std::max(0, base_height_px),
        std::max(0, active_height_px),
        verse_index,
    });
}

void SlideGrouper::flush_buffer()
{
    if (buffer_.empty()) {
        return;
    }
    if (follow_lines_ > 0) {
        flush_follow_mode(follow_lines_);
    } else {
        flush_paging_mode();
    }
    buffer_.clear();
}

void SlideGrouper::push_group(SlideGroup group)
{
    groups_.push_back(std::move(group));
}

std::pair<std::vector<int>, std::size_t> SlideGrouper::pack_from(
    std::size_t start, int active_count) const
{
    std::vector<int> slide_grp;
    int used_h = 0;
    std::size_t k = start;
    while (k < buffer_.size()) {
        const auto& line = buffer_[k];
        const int h = (static_cast<int>(k - start) < active_count) ? line.active_h
                                                                   : line.base_h;
        if (used_h + h > max_visual_px_ && used_h > 0) {
            break;
        }
        slide_grp.push_back(line.index);
        used_h += h;
        ++k;
    }
    return {std::move(slide_grp), k};
}

void SlideGrouper::flush_follow_mode(int step)
{
    std::size_t curr_start = 0;
    while (curr_start < buffer_.size()) {
        const int start_verse = buffer_[curr_start].verse;
        int actual_active = 0;
        const std::size_t lim =
            std::min(curr_start + static_cast<std::size_t>(step), buffer_.size());
        for (std::size_t k = curr_start; k < lim; ++k) {
            if (prevent_mixed_active_ && buffer_[k].verse != start_verse) {
                break;
            }
            ++actual_active;
        }
        if (actual_active == 0) {
            actual_active = 1;
        }

        auto slide_grp = pack_from(curr_start, actual_active).first;
        // Active lines that do not fit wait for the next slide, which starts
        // with the first of them; what room is left here shows them following.
        if (static_cast<int>(slide_grp.size()) < actual_active) {
            actual_active = std::max(1, static_cast<int>(slide_grp.size()));
            slide_grp = pack_from(curr_start, actual_active).first;
        }
        if (!slide_grp.empty()) {
            SlideGroup g;
            g.indices = std::move(slide_grp);
            g.active_count = actual_active;
            groups_.push_back(std::move(g));
        }
        curr_start += static_cast<std::size_t>(actual_active);
    }
}

void SlideGrouper::flush_paging_mode()
{
    std::size_t curr_start = 0;
    while (curr_start < buffer_.size()) {
        auto [slide_grp, k] = pack_from(curr_start, 0);
        if (!slide_grp.empty()) {
            SlideGroup g;
            g.active_count = static_cast<int>(slide_grp.size());
            g.indices = std::move(slide_grp);
            groups_.push_back(std::move(g));
        } else {
            ++k;  // defensive: never stall
        }
        curr_start = k;
    }
}

std::vector<int> build_line_to_slide(
    const std::vector<SlideGroup>& groups,
    std::size_t line_count)
{
    // Later slides overwrite, so a follow-lines upcoming line maps to the
    // slide where it is active again.
    std::vector<int> map(line_count, 0);
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const int owner = groups[gi].owner_line;
        if (owner >= 0 && static_cast<std::size_t>(owner) < line_count) {
            map[static_cast<std::size_t>(owner)] = static_cast<int>(gi);
        }
        for (int idx : groups[gi].indices) {
            if (idx >= 0 && static_cast<std::size_t>(idx) < line_count) {
                map[static_cast<std::size_t>(idx)] = static_cast<int>(gi);
            }
        }
    }
    return map;
}

}  // namespace seventhslide::layout
