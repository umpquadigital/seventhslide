#pragma once

// Draft text-theme element set + undo + selection. QML is chrome over these
// commands. Everything both canvas editors do lives in
// canvas::ObjectEditor; this adds design contexts, element bindings, and
// the theme's text and transition commands.
// Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/object_editor.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/model/transition.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

/// The elements of ``ctx`` in ``set``.
[[nodiscard]] std::vector<Element>* context_elements(ElementSet& set,
                                                     const ElementContext& ctx) noexcept;

/// What an edit of one context may change: its elements and the id counter.
using ContextPart =
    canvas::DocPart<ElementSet, ElementContext, std::vector<Element>, &context_elements>;

/// In-memory editor session for one text theme's element model.
class Editor : public canvas::ObjectEditor<Editor, ElementSet, Element, ContextPart> {
public:
    void load(ElementSet set);
    /// ``style`` is the theme style blob (``elements`` nested). Empty / missing
    /// model -> empty lists on ``canvas``.
    void load_from_style(const nlohmann::json& style, geom::CanvasSize canvas);
    void clear();

    [[nodiscard]] const ElementSet& set() const noexcept { return doc_; }
    [[nodiscard]] ElementContext context() const noexcept { return ctx_; }

    /// The elements of the current context, back to front.
    [[nodiscard]] const std::vector<Element>& current() const noexcept { return doc_.at(ctx_); }

    void set_context(ElementContext ctx);

    /// New element on the current context, laid out for the set's canvas.
    /// ``shape_kind`` is only used for Shape objects (empty -> rectangle).
    [[nodiscard]] freeform::EditResult insert(freeform::ObjectType type, ElementBinding binding,
                                              std::string_view shape_kind = {});
    /// Persist in-canvas typing: markup + the box B/I/U flags.
    [[nodiscard]] freeform::EditResult set_text(std::string_view id, std::string text, bool bold,
                                                bool italic, bool underline);
    /// Whole-box run style (B/I/U) for a text element that is not being typed.
    [[nodiscard]] freeform::EditResult apply_text_attrs(const model::SpanAttrs& attrs);
    [[nodiscard]] freeform::EditResult set_transition(model::Transition spec);

private:
    friend class canvas::ObjectEditor<Editor, ElementSet, Element, ContextPart>;

    static constexpr auto k_units = freeform::GeometryUnits::WholePixels;
    /// Theme z values leave room between elements.
    static constexpr int k_z_step = 10;

    [[nodiscard]] std::vector<Element>* stack() noexcept { return &doc_.at(ctx_); }
    [[nodiscard]] const std::vector<Element>* stack() const noexcept { return &doc_.at(ctx_); }
    [[nodiscard]] std::optional<ElementContext> here() const noexcept { return ctx_; }
    [[nodiscard]] geom::CanvasSize canvas() const noexcept { return doc_.canvas; }
    [[nodiscard]] static freeform::EditResult writable() noexcept
    {
        return freeform::EditResult::success();
    }
    static void after_edit() noexcept {}
    static void after_restore() noexcept {}
    static void after_gesture() noexcept {}
    /// Theme ids are one sequence: groups and elements alike.
    [[nodiscard]] std::string mint_id(char kind);
    void prepare_copy(Element& el) const;
    [[nodiscard]] static freeform::EditResult apply_property(Element& el,
                                                             std::string_view name,
                                                             const nlohmann::json& value);

    [[nodiscard]] bool has_binding(ElementBinding binding) const noexcept;

    ElementContext ctx_ = ElementContext::Song;
};

}  // namespace seventhslide::theme
