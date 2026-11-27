// Editor: loading, the view (output, slide, cue), the ObjectEditor hooks,
// and the slide list. Adding objects and the clipboard live in
// editor_objects.cpp, text and the slide background in editor_properties.cpp,
// and cues and fields in editor_cues.cpp.

#include "seventhslide/freeform/editor.hpp"

#include "seventhslide/freeform/object_properties.hpp"
#include "seventhslide/freeform/tokens.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::freeform {
geom::CanvasSize Editor::current_canvas(const Document& doc) const
{
    const auto* deck = find_deck(doc, output_id_);
    return deck != nullptr ? deck->canvas : geom::k_reference_canvas;
}

void Editor::load(Document doc, std::string output_id)
{
    normalize(doc);
    start(std::move(doc));
    clipboard_slides_.reset();
    if (!output_id.empty() && find_deck(doc_, output_id)) {
        output_id_ = std::move(output_id);
    } else if (!doc_.decks.empty()) {
        output_id_ = doc_.decks.front().output_id;
    } else {
        output_id_.clear();
    }
    slide_index_ = 0;
    cue_index_ = doc_.line_control && !doc_.cues.empty() ? 0 : -1;
    clamp_view();
}

void Editor::clear()
{
    stop();
    clipboard_slides_.reset();
    output_id_.clear();
    slide_index_ = 0;
    cue_index_ = -1;
    range_anchor_ = range_first_ = range_last_ = 0;
}

const Deck* Editor::current_deck() const
{
    return find_deck(doc_, output_id_);
}

const Slide* Editor::current_slide() const
{
    return find_slide(doc_, output_id_, slide_index_);
}

std::vector<Object>* Editor::stack()
{
    auto* slide = find_slide(doc_, output_id_, slide_index_);
    return slide != nullptr ? &slide->objects : nullptr;
}

const std::vector<Object>* Editor::stack() const
{
    const auto* slide = current_slide();
    return slide != nullptr ? &slide->objects : nullptr;
}

Slide* slide_at(Document& doc, const SlidePlace& place) noexcept
{
    if (place.deck >= doc.decks.size() || place.slide >= doc.decks[place.deck].slides.size()) {
        return nullptr;
    }
    return &doc.decks[place.deck].slides[place.slide];
}

std::optional<SlidePlace> Editor::here() const
{
    const auto* deck = current_deck();
    if (deck == nullptr || slide_index_ < 0 || slide_index_ >= deck->slide_count()) {
        return std::nullopt;
    }
    return SlidePlace{static_cast<std::size_t>(deck - doc_.decks.data()),
                      static_cast<std::size_t>(slide_index_)};
}

EditResult Editor::apply_property(Object& obj, std::string_view name, const nlohmann::json& value)
{
    return set_object_property(obj, name, value);
}

int Editor::slide_count() const noexcept
{
    const auto* deck = current_deck();
    return deck ? deck->slide_count() : 0;
}

std::vector<FieldRow> Editor::field_rows() const
{
    const auto used = collect_document_tokens(doc_);
    std::vector<FieldRow> rows;
    rows.reserve(used.size() + doc_.fields.size());
    for (const auto& hit : used) {
        FieldRow row;
        row.name = hit.name;
        const auto it = doc_.fields.find(hit.name);
        if (it != doc_.fields.end()) {
            row.value = it->second;
        }
        row.unused = false;
        row.hide = hit.hide_line_if_empty;
        rows.push_back(std::move(row));
    }
    const auto is_used = [&](const std::string& name) {
        return std::find_if(used.begin(), used.end(), [&](const TokenHit& hit) {
                   return hit.name == name;
               }) != used.end();
    };
    for (const auto& [name, value] : doc_.fields) {
        if (is_used(name)) {
            continue;
        }
        FieldRow row;
        row.name = name;
        row.value = value;
        row.unused = true;
        rows.push_back(std::move(row));
    }
    return rows;
}

void Editor::set_output(std::string_view output_id)
{
    if (!find_deck(doc_, output_id)) {
        return;
    }
    output_id_ = std::string(output_id);
    selected_ids_.clear();
    // The line is the document's; this deck may put it on another slide.
    const auto* deck = current_deck();
    if (doc_.line_control && deck && cue_index_ >= 0
        && cue_index_ < static_cast<int>(deck->line_to_slide.size())) {
        slide_index_ = deck->line_to_slide[static_cast<std::size_t>(cue_index_)];
    }
    clamp_view();
}

void Editor::select_slide(int index)
{
    slide_index_ = index;
    selected_ids_.clear();
    clamp_view();
    pick_current_cue();
}

void Editor::extend_slide_selection(int index)
{
    const int anchor = range_anchor_;
    select_slide(index);
    range_anchor_ = std::clamp(anchor, 0, std::max(0, slide_count() - 1));
    range_first_ = std::min(range_anchor_, slide_index_);
    range_last_ = std::max(range_anchor_, slide_index_);
}

void Editor::select_all_slides()
{
    selected_ids_.clear();
    range_first_ = 0;
    range_last_ = std::max(0, slide_count() - 1);
}

void Editor::pick_current_cue()
{
    if (!doc_.line_control) {
        cue_index_ = -1;
        return;
    }
    const auto* deck = current_deck();
    if (!deck) {
        return;
    }
    const int cues = static_cast<int>(doc_.cues.size());
    for (int c = 0; c < cues && c < static_cast<int>(deck->line_to_slide.size()); ++c) {
        if (deck->line_to_slide[static_cast<std::size_t>(c)] == slide_index_) {
            cue_index_ = c;
            return;
        }
    }
}

void Editor::select_cue(int index)
{
    const int n = static_cast<int>(doc_.cues.size());
    if (index < 0 || index >= n) {
        cue_index_ = -1;
        return;
    }
    cue_index_ = index;
    selected_ids_.clear();
    const auto* deck = current_deck();
    if (deck && index < static_cast<int>(deck->line_to_slide.size())) {
        slide_index_ = deck->line_to_slide[static_cast<std::size_t>(index)];
    }
    clamp_view();
}

void Editor::clamp_view()
{
    const int n = slide_count();
    range_anchor_ = range_first_ = range_last_ = 0;
    if (n <= 0) {
        slide_index_ = 0;
        cue_index_ = -1;
        return;
    }
    slide_index_ = std::clamp(slide_index_, 0, n - 1);
    range_anchor_ = range_first_ = range_last_ = slide_index_;
    const int cues = static_cast<int>(doc_.cues.size());
    if (!doc_.line_control || cues <= 0) {
        cue_index_ = -1;
        return;
    }
    cue_index_ = std::clamp(cue_index_, 0, cues - 1);
}

std::string_view Editor::slide_deck_arg() const noexcept
{
    return doc_.line_control ? std::string_view(output_id_) : std::string_view{};
}

EditResult Editor::set_name(std::string name)
{
    if (name.empty()) {
        return EditResult::fail("Name is required");
    }
    return commit_document([&](Document& doc) {
        doc.name = std::move(name);
        return EditResult::success();
    });
}

EditResult Editor::insert_slide()
{
    const int at = slide_index_ + 1;
    const auto r = commit([&](Document& doc) { return freeform::insert_slide(doc, at, slide_deck_arg()); });
    if (r) {
        select_slide(at);
    }
    return r;
}

EditResult Editor::duplicate_slide()
{
    const int from = slide_index_;
    const auto r = commit(
        [&](Document& doc) { return freeform::duplicate_slide(doc, from, slide_deck_arg()); });
    if (r) {
        select_slide(from + 1);
    }
    return r;
}

EditResult Editor::delete_slide()
{
    if (slide_count() <= 1) {
        return EditResult::fail("at least one slide is required");
    }
    const int index = slide_index_;
    return commit([&](Document& doc) { return freeform::delete_slide(doc, index, slide_deck_arg()); });
}

EditResult Editor::reorder_slides(int from, int to)
{
    const auto r =
        commit([&](Document& doc) { return freeform::reorder_slides(doc, from, to, slide_deck_arg()); });
    if (r) {
        select_slide(to);
    }
    return r;
}

} // namespace seventhslide::freeform
