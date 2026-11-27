#pragma once

// Qt HTML for the in-canvas TextEdit overlay.
// Pixel line-height matches the output painter (``ceil(font_px * factor)``),
// not Qt's proportional CSS mapping. Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/box.hpp"
#include "seventhslide/model/rich_text.hpp"

#include <functional>
#include <string>
#include <vector>

namespace seventhslide::layout {

struct EditorHtmlOptions {
    int font_size = canvas::k_default_font_size;
    std::string color{canvas::k_default_font_color};
    std::string family{canvas::k_default_font_family};
    double stage_scale = 1.0;
    double line_height = canvas::k_default_line_height;
    double letter_spacing = 0.0;
    int line_gap = 0;
    /// The name Qt knows a family by, as slides show it
    /// (``QtFontFaces::painted_family``). Unset: the family's own.
    std::function<std::string(const std::string&)> painted_family;
};

[[nodiscard]] std::string editor_html(const std::vector<model::RichLine>& lines,
                                      const EditorHtmlOptions& opts);

} // namespace seventhslide::layout
