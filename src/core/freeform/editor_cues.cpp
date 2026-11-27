// Editor: cue-linked text, fields, line control and cues.

#include "seventhslide/freeform/editor.hpp"

#include "seventhslide/freeform/tokens.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace seventhslide::freeform {
EditResult Editor::set_linked_style(std::string markup)
{
    const auto target = linked_text_target();
    if (!target.box) {
        return target.error;
    }
    const auto* obj = target.box;
    const auto* deck = target.deck;
    std::map<std::string, std::string> overlays;
    const auto split = split_linked_overlays(doc_, *deck, slide_index_, *obj, markup, overlays);
    if (!split) {
        return split;
    }
    return mutate_selected([&](Object& o) {
        o.cue_overlays = std::move(overlays);
        return EditResult::success();
    });
}

EditResult Editor::set_linked_edit(std::string markup, int upcoming,
                                   std::vector<std::vector<layout::ChordMark>> chords)
{
    const auto target = linked_text_target();
    if (!target.box) {
        return target.error;
    }
    const auto* obj = target.box;
    const auto* deck = target.deck;
    std::string owned;
    if (const auto cut = owned_linked_markup(doc_, *deck, slide_index_, upcoming, markup, owned);
        !cut) {
        return cut;
    }
    std::map<std::string, std::string> overlays;
    if (const auto split = split_linked_overlays(doc_, *deck, slide_index_, *obj, owned, overlays);
        !split) {
        return split;
    }
    int first_cue = -1;
    for (const auto& span : deck_spans(doc_, *deck)) {
        if (span.slide_index == slide_index_) {
            first_cue = std::max(0, span.first_cue);
            break;
        }
    }
    // What the cues' texts become; a cue whose chords did not change keeps
    // its text as it is.
    std::vector<std::pair<std::size_t, std::string>> texts;
    for (std::size_t i = 0; i < chords.size() && first_cue >= 0; ++i) {
        const std::size_t c = static_cast<std::size_t>(first_cue) + i;
        if (c >= doc_.cues.size()) {
            break;
        }
        const auto& cue = doc_.cues[c];
        if (chords[i] == cue_display_chords(*deck, cue)) {
            continue;
        }
        texts.emplace_back(c, cue_text_with_chords(*deck, cue, std::move(chords[i])));
    }
    if (texts.empty() && overlays == obj->cue_overlays) {
        return EditResult::success();
    }
    return commit([&](Document& doc) {
        for (auto& [c, text] : texts) {
            doc.cues[c].text = std::move(text);
        }
        auto* item = selected_item();
        if (item == nullptr) {
            return EditResult::fail("nothing selected");
        }
        item->cue_overlays = std::move(overlays);
        return EditResult::success();
    });
}

Editor::LinkedTextTarget Editor::linked_text_target() const
{
    const auto* obj = selected_item();
    if (!obj) {
        return {.error = EditResult::fail("nothing selected")};
    }
    if (obj->type != ObjectType::Text || !obj->cue_linked) {
        return {.error = EditResult::fail("not a cue-linked text box")};
    }
    const auto* deck = current_deck();
    if (!deck) {
        return {.error = EditResult::fail("no deck")};
    }
    return {.box = obj, .deck = deck};
}

bool Editor::linked_edit_allowed(std::string_view plain, int upcoming) const
{
    const auto* obj = selected_item();
    if (!obj || !obj->cue_linked || obj->type != ObjectType::Text) {
        return true;
    }
    const auto* deck = current_deck();
    if (!deck) {
        return false;
    }
    std::string owned;
    std::map<std::string, std::string> ignored;
    return owned_linked_markup(doc_, *deck, slide_index_, upcoming, plain, owned)
        && split_linked_overlays(doc_, *deck, slide_index_, *obj, owned, ignored);
}

EditResult Editor::set_cue_linked(bool on)
{
    const auto* obj = selected_item();
    if (!obj) {
        return EditResult::fail("nothing selected");
    }
    if (obj->type != ObjectType::Text) {
        return EditResult::fail("not a text box");
    }
    if (on && !doc_.line_control) {
        return EditResult::fail("turn on line control first");
    }
    std::string snapshot;
    if (!on && obj->cue_linked) {
        const auto* deck = current_deck();
        if (deck) {
            snapshot = linked_snapshot_markup(doc_, *deck, slide_index_, *obj);
        }
    }
    return mutate_selected([&](Object& o) {
        if (!on && o.cue_linked) {
            o.text = snapshot;
            o.cue_overlays.clear();
        }
        if (on) {
            o.cue_overlays.clear();
        }
        o.cue_linked = on;
        return EditResult::success();
    });
}

EditResult Editor::set_show_upcoming(bool on)
{
    return mutate_selected([&](Object& o) {
        if (o.type != ObjectType::Text) {
            return EditResult::fail("not a text box");
        }
        o.show_upcoming = on;
        return EditResult::success();
    });
}

EditResult Editor::set_field(std::string name, std::string value)
{
    if (!is_valid_field_name(name)) {
        return EditResult::fail("invalid field name");
    }
    return commit_document([&](Document& doc) {
        doc.fields[std::move(name)] = std::move(value);
        return EditResult::success();
    });
}

EditResult Editor::add_field(std::string name)
{
    if (!is_valid_field_name(name)) {
        return EditResult::fail("invalid field name");
    }
    if (doc_.fields.contains(name)) {
        return EditResult::fail("field already exists");
    }
    return commit_document([&](Document& doc) {
        doc.fields[std::move(name)] = std::string{};
        return EditResult::success();
    });
}

EditResult Editor::rename_field(std::string from, std::string to)
{
    if (!is_valid_field_name(from) || !is_valid_field_name(to)) {
        return EditResult::fail("invalid field name");
    }
    if (from == to) {
        return EditResult::success();
    }
    if (doc_.fields.contains(to)) {
        return EditResult::fail("field already exists");
    }
    return commit_document([&](Document& doc) {
        std::string value;
        const auto it = doc.fields.find(from);
        if (it != doc.fields.end()) {
            value = std::move(it->second);
            doc.fields.erase(it);
        }
        rewrite_field_token(doc, from, to);
        doc.fields[std::move(to)] = std::move(value);
        return EditResult::success();
    });
}

EditResult Editor::delete_field(std::string name)
{
    if (!is_valid_field_name(name)) {
        return EditResult::fail("invalid field name");
    }
    return commit_document([&](Document& doc) {
        doc.fields.erase(name);
        rewrite_field_token(doc, name, {});
        return EditResult::success();
    });
}

EditResult Editor::set_line_control(bool on, bool flatten)
{
    const auto r = commit_document([&](Document& doc) {
        return freeform::set_line_control(doc, on, flatten, output_id_);
    });
    if (r) {
        selected_ids_.clear();
        cue_index_ = doc_.line_control && !doc_.cues.empty() ? 0 : -1;
        clamp_view();
    }
    return r;
}

EditResult Editor::insert_cue(std::string text)
{
    const int at = cue_index_ < 0 ? static_cast<int>(doc_.cues.size()) : cue_index_ + 1;
    const auto r =
        commit_document([&](Document& doc) { return freeform::insert_cue(doc, at, std::move(text)); });
    if (r) {
        select_cue(at);
    }
    return r;
}

EditResult Editor::delete_cue()
{
    if (cue_index_ < 0) {
        return EditResult::fail("no cue selected");
    }
    const int index = cue_index_;
    const auto r = commit_document([&](Document& doc) { return freeform::delete_cue(doc, index); });
    if (r) {
        cue_index_ = std::min(index, static_cast<int>(doc_.cues.size()) - 1);
        clamp_view();
    }
    return r;
}

bool Editor::can_delete_cue() const noexcept
{
    if (!active_ || cue_index_ < 0) {
        return false;
    }
    return freeform::can_delete_cue(doc_, cue_index_);
}

EditResult Editor::rename_cue(std::string text)
{
    if (cue_index_ < 0) {
        return EditResult::fail("no cue selected");
    }
    const int index = cue_index_;
    return commit_document([&](Document& doc) { return freeform::rename_cue(doc, index, std::move(text)); });
}

EditResult Editor::set_cue_verse(std::string type, int number)
{
    if (cue_index_ < 0) {
        return EditResult::fail("no cue selected");
    }
    const int index = cue_index_;
    return commit_document([&](Document& doc) {
        return freeform::set_cue_verse(doc, index, std::move(type), number);
    });
}

EditResult Editor::set_cue_scripture(int chapter, int verse)
{
    if (cue_index_ < 0) {
        return EditResult::fail("no cue selected");
    }
    const int index = cue_index_;
    return commit_document([&](Document& doc) {
        return freeform::set_cue_scripture(doc, index, chapter, verse);
    });
}

EditResult Editor::set_cue_ref_shown(bool shown)
{
    if (cue_index_ < 0) {
        return EditResult::fail("no cue selected");
    }
    const int index = cue_index_;
    return commit_document([&](Document& doc) {
        const auto result = freeform::set_cue_ref_shown(doc, index, shown);
        // Shown here means shown on this output, if it had verse numbers off.
        if (auto* deck = find_deck(doc, output_id_); result && deck != nullptr && shown) {
            show_deck_verse_refs(doc, *deck);
        }
        return result;
    });
}

EditResult Editor::place_slide_thumb(int slide_index, int last_cue)
{
    const auto r = commit([&](Document& doc) {
        return freeform::place_slide_thumb(doc, output_id_, slide_index, last_cue);
    });
    if (r) {
        select_slide(slide_index);
    }
    return r;
}

void Editor::prune_on_save()
{
    prune_invalid_fields(doc_);
    refresh_dirty();
}

} // namespace seventhslide::freeform
