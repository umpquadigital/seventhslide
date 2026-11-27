#pragma once

// The font and the controls' style of the program's own interface, the same
// on every desktop: the app sets them before any window opens, and the tests
// do too, so that what fits in a test fits in the app.

#include <QFont>

namespace seventhslide::app {

/// The interface font, at the pixel size QML controls start from.
[[nodiscard]] QFont operator_ui_font();

/// Qt Quick Controls drawn in Fusion on every system. Left to Qt, Windows
/// and macOS take their native styles, which refuse the backgrounds,
/// handles and content items the interface gives its own controls (and size
/// them differently); Fusion is what Linux has anyway. Off macOS, the
/// interface's words are drawn by the system's font engine too. Before the
/// first QML engine.
void use_operator_ui_style();

}  // namespace seventhslide::app
