#include "seventhslide/theme/element_editor.hpp"

#include "seventhslide/freeform/document.hpp"
#include "seventhslide/geom/shape_path.hpp"
#include "seventhslide/theme/element_properties.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace seventhslide::theme {
namespace {

using seventhslide::freeform::EditResult;
using seventhslide::freeform::ObjectType;

void sort_list(std::vector<Element>& list)
{
    std::stable_sort(list.begin(), list.end(), [](const Element& a, const Element& b) {
        return a.object.z < b.object.z;
    });
}

}  // namespace

void Editor::load(ElementSet set)
{
    for (const auto ctx : k_element_contexts) {
        sort_list(set.at(ctx));
    }
    ctx_ = ElementContext::Song;
    start(std::move(set));
}

void Editor::load_from_style(const nlohmann::json& style, geom::CanvasSize canvas)
{
    ElementSet loaded;
    if (has_elements(style)) {
        // The model is kept on the output's canvas, so this moves nothing;
        // one on another canvas is shown as the output paints it.
        nlohmann::json copy = style;
        if (canvas.valid()) {
            fit_elements_to_canvas(copy, canvas);
        }
        loaded = ElementSet::from_json(copy.at(std::string(k_elements_key)));
    } else {
        loaded.canvas = canvas.valid() ? canvas : geom::k_reference_canvas;
    }
    load(std::move(loaded));
}

void Editor::clear()
{
    stop();
    ctx_ = ElementContext::Song;
}

void Editor::set_context(ElementContext ctx)
{
    ctx_ = ctx;
    if (selected_item() == nullptr) {
        clear_selection();
    }
}

std::vector<Element>* context_elements(ElementSet& set, const ElementContext& ctx) noexcept
{
    return &set.at(ctx);
}

std::string Editor::mint_id(char /*kind*/)
{
    return doc_.mint_id();
}

void Editor::prepare_copy(Element& el) const
{
    // A copy may not take a binding the context cannot have, or one that
    // must stay unique; it keeps its look as a static element.
    if (!binding_fits_context(ctx_, el.binding)
        || (binding_is_exclusive(el.binding) && has_binding(el.binding))) {
        el.binding = ElementBinding::Static;
    }
}

EditResult Editor::apply_property(Element& el, std::string_view name, const nlohmann::json& value)
{
    return set_element_property(el, name, value);
}

bool Editor::has_binding(ElementBinding binding) const noexcept
{
    return std::any_of(current().begin(), current().end(),
                       [&](const Element& el) { return el.binding == binding; });
}

EditResult Editor::insert(ObjectType type, ElementBinding binding, std::string_view shape_kind)
{
    if (!binding_fits_context(ctx_, binding)) {
        return EditResult::fail("binding does not fit this context");
    }
    if (binding_is_exclusive(binding) && has_binding(binding)) {
        return EditResult::fail("binding already used");
    }
    return add_item([&](ElementSet& set) {
        auto el = make_element(type, binding, set.mint_id(), set.canvas);
        if (type == ObjectType::Shape) {
            el.object.shape_kind = geom::shape_kind_from_string(shape_kind);
            el.object.shape_adjust =
                geom::clamp_adjust(el.object.shape_kind, el.object.shape_adjust);
        }
        if (type == ObjectType::Text && binding == ElementBinding::Static) {
            el.object.text = "New text";
        }
        return el;
    });
}

EditResult Editor::set_text(std::string_view id, std::string text, bool bold, bool italic,
                            bool underline)
{
    return commit_stack([&](std::vector<Element>&) {
        auto* el = find(id);
        if (el == nullptr) {
            return EditResult::fail("object not found");
        }
        if (el->object.type != ObjectType::Text) {
            return EditResult::fail("not a text object");
        }
        el->object.text = std::move(text);
        el->object.bold = bold;
        el->object.italic = italic;
        el->object.underline = underline;
        return EditResult::success();
    });
}

EditResult Editor::apply_text_attrs(const model::SpanAttrs& attrs)
{
    return mutate_selected([&](Element& el) {
        if (el.object.type != ObjectType::Text) {
            return EditResult::success();
        }
        auto lines = model::parse_editor_lines(el.object.text, el.object.bold, el.object.italic,
                                               el.object.underline);
        lines = model::apply_attrs_to_all(std::move(lines), attrs);
        const auto formatted = model::format_editor_object(lines);
        el.object.text = formatted.text;
        el.object.bold = formatted.bold;
        el.object.italic = formatted.italic;
        el.object.underline = formatted.underline;
        return EditResult::success();
    });
}

EditResult Editor::set_transition(model::Transition spec)
{
    return mutate_selected([&](Element& el) {
        el.object.transition = spec;
        return EditResult::success();
    });
}

}  // namespace seventhslide::theme
