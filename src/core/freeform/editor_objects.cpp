// Editor: adding objects of each kind, and the slide clipboard beside
// ObjectEditor's object clipboard.

#include "seventhslide/freeform/editor.hpp"

#include <utility>

namespace seventhslide::freeform {

EditResult Editor::add_text()
{
    return add_item([&](Document& doc) { return make_text_object(doc, current_canvas(doc)); });
}

EditResult Editor::add_image(std::string filename)
{
    if (filename.empty()) {
        return EditResult::fail("image filename required");
    }
    return add_item([&](Document& doc) {
        return make_image_object(doc, std::move(filename), current_canvas(doc));
    });
}

EditResult Editor::add_video(std::string filename)
{
    if (filename.empty()) {
        return EditResult::fail("video filename required");
    }
    return add_item([&](Document& doc) {
        return make_video_object(doc, std::move(filename), current_canvas(doc));
    });
}

EditResult Editor::add_table(int rows, int columns)
{
    return add_item([&](Document& doc) {
        return make_table_object(doc, rows, columns, current_canvas(doc));
    });
}

EditResult Editor::add_drawn(std::vector<geom::PathPoint> points, bool closed)
{
    auto kept = distinct_points(points);
    if (kept.size() < 2) {
        return EditResult::fail("draw at least two points");
    }
    return add_item([&](Document& doc) { return make_drawn_object(doc, std::move(kept), closed); });
}

EditResult Editor::add_shape(geom::ShapeKind kind)
{
    return add_item(
        [&](Document& doc) { return make_shape_object(doc, kind, current_canvas(doc)); });
}

EditResult Editor::add_indicator()
{
    return add_item(
        [&](Document& doc) { return make_indicator_object(doc, current_canvas(doc)); });
}

EditResult Editor::add_clock()
{
    return add_item([&](Document& doc) { return make_clock_object(doc, current_canvas(doc)); });
}

EditResult Editor::copy_selected()
{
    const auto r = Base::copy_selected();
    if (r) {
        clipboard_slides_.reset();
    }
    return r;
}

EditResult Editor::copy_slides()
{
    if (current_slide() == nullptr) {
        return EditResult::fail("no slide selected");
    }
    auto clip = freeform::copy_slides(doc_, output_id_, range_first_, range_last_);
    if (clip.entries.empty()) {
        return EditResult::fail("no slide selected");
    }
    clear_clipboard();
    clipboard_slides_ = std::move(clip);
    return EditResult::success();
}

EditResult Editor::paste()
{
    if (!clipboard_slides_) {
        return Base::paste();
    }
    const int after = range_last_;
    PastedSlides pasted;
    const auto r = commit([&](Document& doc) {
        return paste_slides(doc, output_id_, after, *clipboard_slides_, pasted);
    });
    if (r && pasted.first >= 0) {
        select_slide(pasted.last);
        extend_slide_selection(pasted.first);
    }
    return r;
}

bool Editor::can_paste() const noexcept
{
    return clipboard_slides_.has_value() || Base::can_paste();
}

} // namespace seventhslide::freeform
