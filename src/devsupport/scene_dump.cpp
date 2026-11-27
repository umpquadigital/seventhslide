#include "seventhslide/devsupport/scene_dump.hpp"

#include "seventhslide/build/title_slides.hpp"
#include "seventhslide/devsupport/flat_style.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace seventhslide::devsupport {

using layout::ITextMeasurer;
using build::LayoutResult;
using layout::OverlayScene;
using layout::SlideGroup;
using build::apply_title_slide_layout;
using layout::assign_overlay_to_title_slides;
using build::compute_song_layout;

nlohmann::json serialize_resolved_style(const theme::ResolvedTheme& theme)
{
    const auto& b = theme.behavior;
    const auto& bg = theme.background;
    nlohmann::json transitions = nlohmann::json::object();
    for (const auto ctx : theme::k_content_contexts) {
        transitions[std::string(theme::to_string(ctx))] = b.transition(ctx);
    }
    return {
        {"transitions", std::move(transitions)},
        {"balance_wrapped_lines", b.balance_wrapped_lines},
        {"balance_wrapped_strength", b.balance_wrapped_strength},
        {"bible_fluid_verses", b.bible_fluid_verses},
        {"elements", theme.elements.to_json()},
        {"fluid_slides", b.fluid_slides},
        {"follow_lines", b.follow_lines},
        {"prevent_mixed_active", b.prevent_mixed_active},
        {"show_title_slides", b.show_title_slides},
        {"title_slide_show_lines", b.title_slide_show_lines},
        {"verse_gap", b.verse_gap},
        {"background_type", layout::to_string(bg.type)},
        {"background_color", bg.color},
        {"background_image", bg.image},
        {"background_video", bg.video},
        {"bg_enable_fade", bg.enable_fade},
        {"bg_fade_duration", bg.fade_duration},
    };
}

nlohmann::json serialize_slide_group(const SlideGroup& g)
{
    return nlohmann::json{
        {"indices", g.indices},
        {"active_count", g.active_count},
        {"is_title", g.is_title},
    };
}

nlohmann::json serialize_output_layout(
    std::string_view name,
    int canvas_width,
    int canvas_height,
    const theme::ResolvedTheme& theme,
    const LayoutResult& result,
    const std::vector<bool>& overlay_nonempty,
    const std::vector<bool>& bg_override_present,
    int total_lines)
{
    const int n_lines = static_cast<int>(result.lines.size());
    const int pad_to = total_lines > 0 ? total_lines : n_lines;
    const int n_slides = static_cast<int>(result.groups.size());

    std::vector<int> line_to_slide = result.line_to_slide;
    while (static_cast<int>(line_to_slide.size()) < pad_to) {
        // Shorter outputs map their missing lines onto the last slide.
        line_to_slide.push_back(std::max(0, n_slides - 1));
    }
    if (static_cast<int>(line_to_slide.size()) > pad_to) {
        line_to_slide.resize(static_cast<std::size_t>(pad_to));
    }

    std::vector<bool> overlays = overlay_nonempty;
    std::vector<bool> bgs = bg_override_present;
    overlays.resize(static_cast<std::size_t>(std::max(n_slides, 1)), false);
    bgs.resize(static_cast<std::size_t>(std::max(n_slides, 1)), false);

    std::vector<std::string> all_lines;
    std::vector<std::string> line_labels;
    std::vector<int> verse_indices;
    all_lines.reserve(result.lines.size());
    line_labels.reserve(result.lines.size());
    verse_indices.reserve(result.lines.size());
    for (const auto& ln : result.lines) {
        all_lines.push_back(ln.text);
        line_labels.push_back(ln.label);
        verse_indices.push_back(ln.verse_index);
    }

    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : result.groups) {
        groups.push_back(serialize_slide_group(g));
    }

    return nlohmann::json{
        {"name", name},
        {"canvas", {{"width", canvas_width}, {"height", canvas_height}}},
        {"resolved_style", serialize_resolved_style(theme)},
        {"line_to_slide", line_to_slide},
        {"verse_codes", result.verse_codes},
        {"verse_indices", verse_indices},
        {"slide_count", std::max(n_slides, 1)},
        {"overlay_nonempty", overlays},
        {"bg_override_present", bgs},
        {"all_lines", all_lines},
        {"line_labels", line_labels},
        {"groups", groups},
    };
}

nlohmann::json build_song_scene_dump(
    const SceneDumpInput& input,
    std::string_view name,
    int canvas_width,
    int canvas_height,
    const theme::ResolvedTheme& theme,
    ITextMeasurer& measurer,
    bool title_mode,
    bool has_title_overlay,
    const OverlayScene* title_overlay)
{
    auto result = compute_song_layout(
        theme, measurer, input.lyrics, input.verse_order, /*suppress_titles=*/title_mode);

    if (title_mode) {
        apply_title_slide_layout(
            result, theme, input.verse_order, true, has_title_overlay);
    }

    std::vector<bool> overlay_flags(result.groups.size(), false);
    std::vector<bool> bg_flags(result.groups.size(), false);

    if (title_mode && has_title_overlay && title_overlay && !title_overlay->empty()) {
        const auto assigned =
            assign_overlay_to_title_slides(result.groups, *title_overlay);
        for (std::size_t i = 0; i < assigned.size(); ++i) {
            overlay_flags[i] = !assigned[i].empty();
        }
    }

    const auto out = serialize_output_layout(
        name, canvas_width, canvas_height, theme, result, overlay_flags, bg_flags,
        static_cast<int>(result.lines.size()));

    nlohmann::json input_meta = {
        {"item_type", input.item_type},
        {"item_index", input.item_index},
        {"title", input.title},
        {"lyrics", input.lyrics},
        {"verse_order", input.verse_order},
    };
    if (input.song_id.has_value()) {
        input_meta["song_id"] = *input.song_id;
    }

    nlohmann::json doc = {
        {"schema", std::string(k_scene_dump_schema)},
        {"schema_version", k_scene_dump_schema_version},
        {"oracle", "native"},
        {"input", input_meta},
        {"outputs", nlohmann::json::array({out})},
        {"player",
         {{"total_lines", static_cast<int>(result.lines.size())},
          {"all_lines", out.at("all_lines")},
          {"all_line_labels", out.at("line_labels")}}},
    };
    if (!input.notes.empty()) {
        doc["notes"] = input.notes;
    }
    if (!input.fixture_id.empty()) {
        doc["fixture_id"] = input.fixture_id;
    }
    return doc;
}

nlohmann::json native_scene_dump_from_oracle_fixture(
    const nlohmann::json& oracle_doc,
    ITextMeasurer& measurer)
{
    const auto& input = oracle_doc.at("input");
    const auto& outs = oracle_doc.at("outputs");
    if (!outs.is_array() || outs.empty()) {
        throw std::runtime_error("oracle fixture missing outputs");
    }
    const auto& o0 = outs.at(0);

    const std::string name = o0.value("name", "Main");
    int canvas_width = geom::k_reference_canvas.width;
    int canvas_height = geom::k_reference_canvas.height;
    if (o0.contains("canvas") && o0["canvas"].is_object()) {
        canvas_width = o0["canvas"].value("width", canvas_width);
        canvas_height = o0["canvas"].value("height", canvas_height);
    }
    const auto& flat = o0.contains("resolved_style") && o0["resolved_style"].is_object()
                           ? o0["resolved_style"]
                           : nlohmann::json::object();
    // Fixtures describe the look with the flat body keys.
    nlohmann::json text = flat;
    text[std::string(theme::k_elements_key)] = elements_from_flat_style(flat).to_json();
    const auto resolved = theme::resolve_styles(text, flat, {canvas_width, canvas_height});

    SceneDumpInput in;
    in.item_type = input.value("item_type", "song");
    in.item_index = input.value("item_index", 0);
    in.title = input.value("title", "");
    in.lyrics = input.value("lyrics", "");
    in.verse_order = input.value("verse_order", "");
    if (input.contains("song_id") && !input["song_id"].is_null()) {
        in.song_id = input["song_id"].get<std::int64_t>();
    }
    if (oracle_doc.contains("fixture_id")) {
        in.fixture_id = oracle_doc["fixture_id"].get<std::string>();
    }
    if (oracle_doc.contains("notes")) {
        in.notes = oracle_doc["notes"].get<std::string>();
    }

    return build_song_scene_dump(in, name, canvas_width, canvas_height, resolved, measurer);
}

std::vector<std::string> validate_scene_document(const nlohmann::json& doc)
{
    std::vector<std::string> errs;
    if (!doc.is_object()) {
        errs.emplace_back("document must be an object");
        return errs;
    }
    if (doc.value("schema", "") != std::string(k_scene_dump_schema)) {
        errs.push_back(
            std::string("schema must be '") + std::string(k_scene_dump_schema) + "'");
    }
    if (doc.value("schema_version", 0) != k_scene_dump_schema_version) {
        errs.push_back("schema_version must be 1");
    }
    if (!doc.contains("input") || !doc["input"].is_object()) {
        errs.emplace_back("input must be an object");
    }
    if (!doc.contains("outputs") || !doc["outputs"].is_array() || doc["outputs"].empty()) {
        errs.emplace_back("outputs must be a non-empty array");
    }
    if (!doc.contains("player") || !doc["player"].is_object()) {
        errs.emplace_back("player must be an object");
    } else {
        for (const char* key : {"total_lines", "all_lines", "all_line_labels"}) {
            if (!doc["player"].contains(key)) {
                errs.push_back(std::string("player.") + key + " missing");
            }
        }
    }
    if (doc.contains("outputs") && doc["outputs"].is_array()) {
        for (std::size_t i = 0; i < doc["outputs"].size(); ++i) {
            const auto& oc = doc["outputs"][i];
            if (!oc.is_object()) {
                errs.push_back("outputs[" + std::to_string(i) + "] must be an object");
                continue;
            }
            for (const char* key :
                 {"name", "canvas", "line_to_slide", "slide_count", "resolved_style"}) {
                if (!oc.contains(key)) {
                    errs.push_back(
                        "outputs[" + std::to_string(i) + "]." + key + " missing");
                }
            }
        }
    }
    return errs;
}

std::vector<bool> overlay_nonempty_flags(const build::OutputRuntime& rt)
{
    const int n = std::max(rt.slide_count(), 1);
    std::vector<bool> flags(static_cast<std::size_t>(n), false);
    for (std::size_t i = 0; i < rt.slide_overlays.size() && static_cast<int>(i) < n; ++i) {
        flags[i] = !rt.slide_overlays[i].empty();
    }
    return flags;
}

std::vector<bool> bg_override_present_flags(const build::OutputRuntime& rt)
{
    const int n = std::max(rt.slide_count(), 1);
    std::vector<bool> flags(static_cast<std::size_t>(n), false);
    for (std::size_t i = 0; i < rt.slide_bg_overrides.size() && static_cast<int>(i) < n; ++i) {
        flags[i] = rt.slide_bg_overrides[i].has_value();
    }
    return flags;
}

}  // namespace seventhslide::devsupport
