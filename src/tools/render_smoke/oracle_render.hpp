#pragma once

// Shared helpers: oracle fixture JSON -> Session -> SceneFrame -> PNG.

#include "seventhslide/devsupport/flat_style.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/render/scene_compositor.hpp"
#include "seventhslide/render/text_painter.hpp"
#include "seventhslide/scene/scene_builder.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/session/session.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <nlohmann/json.hpp>

#include <QColor>
#include <QImage>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace seventhslide::render_smoke {

inline nlohmann::json load_json(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    nlohmann::json j;
    in >> j;
    return j;
}

inline model::OutputConfig oc_from_oracle_output(const nlohmann::json& o)
{
    model::OutputConfig oc;
    oc.name = o.at("name").get<std::string>();
    oc.canvas_width = o.at("canvas").at("width").get<int>();
    oc.canvas_height = o.at("canvas").at("height").get<int>();
    seventhslide::devsupport::install_flat_style(oc, o.at("resolved_style"));
    // Transparent oracle bgs still need a visible stage for smoke screenshots.
    if (!oc.bg_themes.empty()) {
        auto& style = oc.bg_themes.front().style;
        const auto type = style.value("background_type", std::string("transparent"));
        if (type.empty() || type == "transparent") {
            style["background_type"] = "color";
            style["background_color"] = "#000000";
        }
    }
    return oc;
}

struct OracleRenderResult {
    scene::SceneFrame frame;
    QImage image;
};

inline OracleRenderResult render_oracle_song_frame(
    const nlohmann::json& oracle,
    layout::ITextMeasurer& measurer,
    int line_index = 0,
    bool blank = false)
{
    session::Session session(measurer);
    session.set_outputs({oc_from_oracle_output(oracle.at("outputs").at(0))});

    model::Song song;
    song.title = oracle.at("input").value("title", "");
    song.lyrics = oracle.at("input").at("lyrics").get<std::string>();
    song.verse_order = oracle.at("input").value("verse_order", "");
    session.play_song(song);
    if (line_index > 0) {
        if (!session.focused_stack().program.jump_to_line(line_index)) {
            throw std::runtime_error("jump_to_line failed");
        }
    }
    if (blank) {
        session.set_global_blank(true);
    }

    scene::SceneExtras extras;
    extras.transition_key = "song";
    session.focused_stack().program.set_scene_extras(extras);

    auto frames = session.scene_frames();
    if (frames.empty()) {
        throw std::runtime_error("no scene frames");
    }
    scene::SceneFrame frame = std::move(frames.front());
    if (frame.background.type == seventhslide::layout::BackgroundType::Transparent) {
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#000000";
    }

    const int w = frame.canvas_width > 0 ? frame.canvas_width : 1920;
    const int h = frame.canvas_height > 0 ? frame.canvas_height : 1080;
    QImage image =
        render::render_scene_frame_image(frame, w, h, QColor(Qt::black));
    return {std::move(frame), std::move(image)};
}

}  // namespace seventhslide::render_smoke
