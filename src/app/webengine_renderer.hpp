#pragma once

// Qt WebEngine's web-page renderer (WebRenderer): Chromium lays the page out
// in an off-screen Qt Quick window, drawn through QQuickRenderControl on the
// app's own graphics API and read back as a picture whenever the page changes.
// Built only where Qt WebEngine is installed (SEVENTHSLIDE_HAS_WEBENGINE).
// Seam: Qt Quick, Qt WebEngine. GUI thread.

#include "web_renderer.hpp"

#include <QString>

namespace seventhslide::app {

/// Qt WebEngine's set-up. Call once, before the QGuiApplication exists.
void initialize_web_engine();

/// Makes a Qt WebEngine renderer per page. Call after the application exists.
/// The pages keep what they store (sign-ins, cookies) in ``storage_dir``, so
/// a page signed into once stays signed in; empty keeps nothing past the run.
[[nodiscard]] WebRendererFactory webengine_renderer_factory(const QString& storage_dir = {});

} // namespace seventhslide::app
