// SlideObjectsSession: the canvas commands that are the same for either
// editor, through CanvasCommands.

#include "slide_edit_session.hpp"

#include "object_geometry_qml.hpp"
#include "qml_ids.hpp"
#include "qvariant_json.hpp"

#include <seventhslide/freeform/object_properties.hpp>

namespace seventhslide::app {

QString SlideObjectsSession::apply(const freeform::EditResult& result, Touched touched)
{
    if (!result) {
        return QString::fromStdString(result.error);
    }
    owner_.after_edit(touched == Touched::Geometry);
    emit documentChanged();
    emit selectionChanged();
    return {};
}

QString SlideObjectsSession::apply_copy(const freeform::EditResult& result)
{
    if (!result) {
        return QString::fromStdString(result.error);
    }
    // The document is untouched: only what Paste would bring changed.
    emit clipboardChanged();
    return {};
}

void SlideObjectsSession::emit_all()
{
    emit documentChanged();
    emit selectionChanged();
    emit clipboardChanged();
}

// --- selection ----------------------------------------------------------

template <class Change>
void SlideObjectsSession::select_while_active(Change change)
{
    if (!canvas_->active()) {
        return;
    }
    change();
    emit selectionChanged();
}

void SlideObjectsSession::selectObject(const QString& object_id, bool additive)
{
    select_while_active([&] { canvas_->select(object_id.toStdString(), additive); });
}

void SlideObjectsSession::selectIds(const QStringList& object_ids, bool additive)
{
    select_while_active([&] { canvas_->select_ids(ids_from_list(object_ids), additive); });
}

void SlideObjectsSession::selectAll()
{
    select_while_active([&] { canvas_->select_all(); });
}

void SlideObjectsSession::clearSelection()
{
    select_while_active([&] { canvas_->clear_selection(); });
}

// --- objects ------------------------------------------------------------

QString SlideObjectsSession::deleteSelected()
{
    return apply(canvas_->delete_selected());
}

QString SlideObjectsSession::reorderSelected(CanvasOrderQml::Order order)
{
    const auto op = CanvasOrderQml::to_core(order);
    if (!op) {
        return tr("That is not a way to order objects.");
    }
    return apply(canvas_->reorder_selected(*op));
}

QString SlideObjectsSession::copySelected()
{
    return apply_copy(canvas_->copy_selected());
}

QString SlideObjectsSession::paste()
{
    return apply(canvas_->paste());
}

// --- history ------------------------------------------------------------

QString SlideObjectsSession::undo()
{
    return apply(canvas_->undo());
}

QString SlideObjectsSession::redo()
{
    return apply(canvas_->redo());
}

void SlideObjectsSession::beginGesture()
{
    canvas_->begin_gesture();
}

void SlideObjectsSession::endGesture()
{
    canvas_->end_gesture();
    owner_.after_edit(false);
    owner_.emit_document_changed();
}

// --- geometry and arrangement -------------------------------------------

QString SlideObjectsSession::setGeometries(const QVariantList& geometries)
{
    return apply(canvas_->set_geometries(geometries_from_qml(geometries)), Touched::Geometry);
}

QString SlideObjectsSession::align(CanvasAlignQml::Align align)
{
    const auto to = CanvasAlignQml::to_core(align);
    if (!to) {
        return tr("That is not a way to align objects.");
    }
    return apply(canvas_->align_selected(*to), Touched::Geometry);
}

QString SlideObjectsSession::distribute(CanvasDistributeQml::Distribute dist)
{
    const auto along = CanvasDistributeQml::to_core(dist);
    if (!along) {
        return tr("That is not a way to space objects out.");
    }
    return apply(canvas_->distribute_selected(*along), Touched::Geometry);
}

QString SlideObjectsSession::nudge(double dx, double dy)
{
    return apply(canvas_->nudge_selected(dx, dy), Touched::Geometry);
}

QString SlideObjectsSession::flip(CanvasFlipQml::Axis axis)
{
    const auto across = CanvasFlipQml::to_core(axis);
    if (!across) {
        return tr("That is not a way to flip an object.");
    }
    return apply(canvas_->flip_selected(*across), Touched::Geometry);
}

QString SlideObjectsSession::group()
{
    return apply(canvas_->group_selected());
}

QString SlideObjectsSession::ungroup()
{
    return apply(canvas_->ungroup_selected());
}

QString SlideObjectsSession::setLocked(bool locked)
{
    return apply(canvas_->set_selected_locked(locked));
}

QString SlideObjectsSession::setSelectedProperty(const QString& name, const QVariant& value)
{
    const auto key = name.toStdString();
    // Moving or resizing touches only where the object sits.
    const auto touched = freeform::is_geometry_property(key) ? Touched::Geometry
                                                             : Touched::Anything;
    return apply(canvas_->set_property(key, to_json(value)), touched);
}

// --- state --------------------------------------------------------------

QStringList SlideObjectsSession::selectedIds() const
{
    QStringList out;
    for (const auto& id : canvas_->selected_ids()) {
        out.push_back(QString::fromStdString(id));
    }
    return out;
}

bool SlideObjectsSession::canRaise() const
{
    return canvas_->can_reorder_selected(freeform::ZOrderOp::Forward);
}

bool SlideObjectsSession::canLower() const
{
    return canvas_->can_reorder_selected(freeform::ZOrderOp::Backward);
}

bool SlideObjectsSession::canGroup() const
{
    return canvas_->can_group_selected();
}

bool SlideObjectsSession::canUngroup() const
{
    return canvas_->can_ungroup_selected();
}

bool SlideObjectsSession::selectionLocked() const
{
    return canvas_->selection_locked();
}

bool SlideObjectsSession::canPaste() const
{
    return canvas_->can_paste();
}

} // namespace seventhslide::app
