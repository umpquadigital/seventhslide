#pragma once

// The flat style keys: a compact way for tests, fixtures and developer tools to
// describe an output's body boxes (``box_x``, ``width_px``, ``font_size``,
// ``bible_text_box_x``, ``video_area_x``, ``show_lyrics``, ...). The app reads
// element models only; nothing here ships in it.

#include "seventhslide/model/output_config.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <nlohmann/json.hpp>

namespace seventhslide::devsupport {

/**
 * The stock design, with its body boxes - lyrics, passage, video and image
 * areas - taken from the flat keys ``style`` carries. Flat keys describe the
 * reference canvas, like the stock design; ``0`` for an area's width or height
 * means the whole canvas. Chrome is always stock: there are no flat chrome keys.
 */
[[nodiscard]] theme::ElementSet elements_from_flat_style(const nlohmann::json& style);

/// Give ``oc`` a theme of every kind described by ``flat``, and defaults
/// naming them. Each text theme holds only its own kind's contexts of the
/// design, as a stored theme does. The flat keys stay on every theme, so a
/// tool that adds more later describes the same look. No-op when ``oc``
/// already has any theme.
void install_flat_style(model::OutputConfig& oc, const nlohmann::json& flat);

}  // namespace seventhslide::devsupport
