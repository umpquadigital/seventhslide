#pragma once

// The image providers the operator QML loads from: output and theme
// previews, media, Freeform and service thumbnails, the live video plate and
// the icon set.

class QQmlEngine;

namespace seventhslide::app {

class SessionFacade;

/// Add every provider to ``engine``. ``session`` must outlive the engine.
void add_image_providers(QQmlEngine& engine, SessionFacade& session);

}  // namespace seventhslide::app
