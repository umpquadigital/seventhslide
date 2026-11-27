#pragma once

// An interlaced picture made whole: its two fields were caught a moment
// apart, and anything moving shows combed between them. One field is kept,
// the lines of the other made from it (each the mean of the lines above and
// below). Moving pictures stay clean at the cost of half the lines' detail;
// for an operator's camera that is the right trade. Seam: Qt Gui.

#include <QImage>

namespace seventhslide::render {

/// Make ``picture`` (32 bits a pixel) whole, in place: the even lines (the
/// upper field) kept, the odd lines made from them.
void deinterlace(QImage& picture);

} // namespace seventhslide::render
