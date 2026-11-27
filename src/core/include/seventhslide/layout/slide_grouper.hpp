#pragma once

// Slide grouper: packs measured lines into slides (with follow-lines).
// Seam: plain C++20 (no Qt).

#include <cstddef>
#include <vector>

namespace seventhslide::layout {

struct SlideGroup {
    std::vector<int> indices;
    int active_count = 0;
    bool is_title = false;
    /// Line this slide is selected by when it owns no active line - a title
    /// slide showing upcoming lyrics borrows the next window into ``indices``
    /// with ``active_count == 0``, so its own line would otherwise belong to
    /// no slide at all. ``-1`` => the active prefix of ``indices`` owns it.
    int owner_line = -1;

    [[nodiscard]] bool operator==(const SlideGroup&) const noexcept = default;
};

/**
 * Packs buffered lines into slide groups.
 *
 * Call ``add_line`` for each finished logical line, ``flush_buffer`` at verse
 * boundaries and forced breaks (paging only), and end-of-song.
 */
class SlideGrouper {
public:
    SlideGrouper(int max_visual_px, int follow_lines, bool prevent_mixed_active);

    void add_line(int line_index, int base_height_px, int active_height_px, int verse_index);

    void flush_buffer();

    /// Append an already-finished group (virtual title slides).
    void push_group(SlideGroup group);

    [[nodiscard]] const std::vector<SlideGroup>& groups() const noexcept { return groups_; }

    /// Steal groups (after final flush).
    [[nodiscard]] std::vector<SlideGroup> take_groups() { return std::move(groups_); }

private:
    struct BufLine {
        int index = 0;
        int base_h = 0;
        int active_h = 0;
        int verse = 0;
    };

    int max_visual_px_;
    int follow_lines_;
    bool prevent_mixed_active_;
    std::vector<BufLine> buffer_;
    std::vector<SlideGroup> groups_;

    [[nodiscard]] std::pair<std::vector<int>, std::size_t> pack_from(
        std::size_t start, int active_count) const;

    void flush_follow_mode(int step);
    void flush_paging_mode();
};

/// Map each logical line -> slide index (first group that contains it).
/// Line -> last slide index that lists it.
[[nodiscard]] std::vector<int> build_line_to_slide(
    const std::vector<SlideGroup>& groups,
    std::size_t line_count);

}  // namespace seventhslide::layout
