#pragma once

// A table object: rows and columns of even cells, each a text
// box in the object's type, the first row a header when it is one, every
// other row banded when asked, the borders over them all. Simple by design:
// no merged cells, no sizes of their own. Seam: Qt Gui.

#include "seventhslide/canvas/object.hpp"
#include "seventhslide/render/paint_context.hpp"

#include <QPainter>

namespace seventhslide::render {

/// ``table`` at (0, 0, its width, its height) of the painter: the caller has
/// placed, turned and faded it.
void paint_table_object(QPainter& painter, const canvas::Object& table, const PaintContext& ctx = {});

}  // namespace seventhslide::render
