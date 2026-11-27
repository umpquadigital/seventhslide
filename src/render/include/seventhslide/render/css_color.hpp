#pragma once

// One CSS color parser for every painter.
//
// Freeform documents and themes can carry functional notation -
// ``rgba(255,255,255,0.08)`` - which ``QColor`` alone does not understand and
// silently turns into an invalid color, i.e. nothing painted. This handles the
// functional forms and delegates everything else to Qt.

#include <QColor>

#include <string>
#include <string_view>

namespace seventhslide::render {

/// ``#rgb`` / ``#rrggbb`` / ``#aarrggbb`` / SVG name / ``rgb()`` / ``rgba()``.
/// Empty or ``none`` yields an invalid color; so does anything unparseable.
[[nodiscard]] QColor css_color(std::string_view css);

/// ``css_color`` with a fallback for empty / unparseable input.
[[nodiscard]] QColor css_color_or(std::string_view css, const QColor& fallback);

/// ``#rrggbb``, or ``#aarrggbb`` when the color is translucent.
/// Empty when ``css`` is not a color. This is the string to store: every
/// painter reads it back through ``css_color``.
[[nodiscard]] std::string css_color_hex(std::string_view css);

} // namespace seventhslide::render
