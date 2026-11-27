#pragma once

// macOS: an output window kept over everything on its screen, and the Mac
// kept awake while anything is presented (output_window_mac.mm). Private to
// src/render.

#include <QString>

class QScreen;
class QWindow;

namespace seventhslide::render {

/// ``window`` above the menu bar and the Dock, on every Space, left out of
/// Command+Tab's windows and Mission Control's shuffling, and not hidden by
/// Command+H: an audience screen, not a document. Call once it is shown.
void keep_above_menu_bar(QWindow& window);

/// ``window`` an ordinary window again, as keep_above_menu_bar found it: for
/// an output on the operator's own screen, which must not shut the operator
/// out of every app (Command+`, Command+Tab and Command+H reach past it).
void keep_as_window(QWindow& window);

/// The display's UUID (CGDisplayCreateUUIDFromDisplayID): the same across
/// resolution changes, rearranging and restarts. Empty when it has none or
/// under a platform other than Cocoa.
[[nodiscard]] QString display_uuid(const QScreen& screen);

/// While ``presenting``, the displays don't sleep, the Mac doesn't idle to
/// sleep, and App Nap leaves the app's timers alone (they drive the outputs
/// and the NDI/SRT sends). Call again with false to let them. Repeated calls
/// with the same value do nothing.
void set_presenting(bool presenting);

}  // namespace seventhslide::render
