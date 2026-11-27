#pragma once

// The commands both canvas editors have, behind one interface. Both are
// core's canvas::ObjectEditor (theme::Editor and freeform::Editor), a
// template moc cannot take, so the slide editor's canvas
// (SlideObjectsSession) drives whichever is open through CanvasCommands,
// which CanvasCommandsFor implements once for either editor.

#include <seventhslide/canvas/layout.hpp>
#include <seventhslide/freeform/doc_edit.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

/// The editor commands the canvas uses, the same for either editor.
class CanvasCommands {
public:
    virtual ~CanvasCommands() = default;

    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual const std::vector<std::string>& selected_ids() const = 0;
    [[nodiscard]] virtual bool can_reorder_selected(freeform::ZOrderOp op) const = 0;
    [[nodiscard]] virtual bool can_group_selected() const = 0;
    [[nodiscard]] virtual bool can_ungroup_selected() const = 0;
    [[nodiscard]] virtual bool selection_locked() const = 0;
    [[nodiscard]] virtual bool can_paste() const = 0;

    virtual void select(std::string_view id, bool additive) = 0;
    virtual void select_ids(const std::vector<std::string>& ids, bool additive) = 0;
    virtual void select_all() = 0;
    virtual void clear_selection() = 0;
    virtual void begin_gesture() = 0;
    virtual void end_gesture() = 0;

    [[nodiscard]] virtual freeform::EditResult undo() = 0;
    [[nodiscard]] virtual freeform::EditResult redo() = 0;
    [[nodiscard]] virtual freeform::EditResult delete_selected() = 0;
    [[nodiscard]] virtual freeform::EditResult reorder_selected(freeform::ZOrderOp op) = 0;
    [[nodiscard]] virtual freeform::EditResult copy_selected() = 0;
    [[nodiscard]] virtual freeform::EditResult paste() = 0;
    [[nodiscard]] virtual freeform::EditResult set_geometries(
        const std::vector<freeform::ObjectGeometry>& geometries) = 0;
    [[nodiscard]] virtual freeform::EditResult align_selected(canvas::Align align) = 0;
    [[nodiscard]] virtual freeform::EditResult distribute_selected(canvas::Distribute dist) = 0;
    [[nodiscard]] virtual freeform::EditResult nudge_selected(double dx, double dy) = 0;
    [[nodiscard]] virtual freeform::EditResult flip_selected(canvas::FlipAxis axis) = 0;
    [[nodiscard]] virtual freeform::EditResult group_selected() = 0;
    [[nodiscard]] virtual freeform::EditResult ungroup_selected() = 0;
    [[nodiscard]] virtual freeform::EditResult set_selected_locked(bool locked) = 0;
    [[nodiscard]] virtual freeform::EditResult set_property(std::string_view name,
                                                            const nlohmann::json& value) = 0;
};

/// CanvasCommands for ``Editor``, one line per command.
template <class Editor>
class CanvasCommandsFor final : public CanvasCommands {
public:
    explicit CanvasCommandsFor(Editor& editor) noexcept : editor_(editor) {}

    [[nodiscard]] bool active() const override { return editor_.active(); }
    [[nodiscard]] const std::vector<std::string>& selected_ids() const override
    {
        return editor_.selected_ids();
    }
    [[nodiscard]] bool can_reorder_selected(freeform::ZOrderOp op) const override
    {
        return editor_.can_reorder_selected(op);
    }
    [[nodiscard]] bool can_group_selected() const override { return editor_.can_group_selected(); }
    [[nodiscard]] bool can_ungroup_selected() const override
    {
        return editor_.can_ungroup_selected();
    }
    [[nodiscard]] bool selection_locked() const override { return editor_.selection_locked(); }
    [[nodiscard]] bool can_paste() const override { return editor_.can_paste(); }

    void select(std::string_view id, bool additive) override { editor_.select(id, additive); }
    void select_ids(const std::vector<std::string>& ids, bool additive) override
    {
        editor_.select_ids(ids, additive);
    }
    void select_all() override { editor_.select_all(); }
    void clear_selection() override { editor_.clear_selection(); }
    void begin_gesture() override { editor_.begin_gesture(); }
    void end_gesture() override { editor_.end_gesture(); }

    [[nodiscard]] freeform::EditResult undo() override { return editor_.undo(); }
    [[nodiscard]] freeform::EditResult redo() override { return editor_.redo(); }
    [[nodiscard]] freeform::EditResult delete_selected() override
    {
        return editor_.delete_selected();
    }
    [[nodiscard]] freeform::EditResult reorder_selected(freeform::ZOrderOp op) override
    {
        return editor_.reorder_selected(op);
    }
    [[nodiscard]] freeform::EditResult copy_selected() override { return editor_.copy_selected(); }
    [[nodiscard]] freeform::EditResult paste() override { return editor_.paste(); }
    [[nodiscard]] freeform::EditResult set_geometries(
        const std::vector<freeform::ObjectGeometry>& geometries) override
    {
        return editor_.set_geometries(geometries);
    }
    [[nodiscard]] freeform::EditResult align_selected(canvas::Align align) override
    {
        return editor_.align_selected(align);
    }
    [[nodiscard]] freeform::EditResult distribute_selected(canvas::Distribute dist) override
    {
        return editor_.distribute_selected(dist);
    }
    [[nodiscard]] freeform::EditResult nudge_selected(double dx, double dy) override
    {
        return editor_.nudge_selected(dx, dy);
    }
    [[nodiscard]] freeform::EditResult flip_selected(canvas::FlipAxis axis) override
    {
        return editor_.flip_selected(axis);
    }
    [[nodiscard]] freeform::EditResult group_selected() override { return editor_.group_selected(); }
    [[nodiscard]] freeform::EditResult ungroup_selected() override
    {
        return editor_.ungroup_selected();
    }
    [[nodiscard]] freeform::EditResult set_selected_locked(bool locked) override
    {
        return editor_.set_selected_locked(locked);
    }
    [[nodiscard]] freeform::EditResult set_property(std::string_view name,
                                                    const nlohmann::json& value) override
    {
        return editor_.set_property(name, value);
    }

private:
    Editor& editor_;
};

} // namespace seventhslide::app
