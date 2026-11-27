#pragma once

// What the slide, scene and session tests share: the outputs the oracle
// files describe, the text a frame paints for one element, a measurer that
// counts characters, and a song item pinned to a stack. The fixtures
// themselves are fixture_files.hpp's.

#include "fixture_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::test {

/// The output one of the oracle files describes: its name, canvas and look.
[[nodiscard]] inline model::OutputConfig oc_from_oracle(const nlohmann::json& o)
{
    model::OutputConfig oc;
    oc.name = o.at("name").get<std::string>();
    oc.canvas_width = o.at("canvas").at("width").get<int>();
    oc.canvas_height = o.at("canvas").at("height").get<int>();
    devsupport::install_flat_style(oc, o.at("resolved_style"));
    return oc;
}

/// Painted text of one theme element on a frame. Copyright, reference, indicator
/// and the clocks are canvas objects now, each keeping the id its theme element
/// was migrated with (``te_copyright``, ``te_bible_ref``, ``te_indicator``, ...).
[[nodiscard]] inline std::string element_text(const scene::SceneFrame& frame, std::string_view id)
{
    for (const auto& so : frame.objects) {
        if (so.id != id || !so.text) {
            continue;
        }
        std::string out;
        for (const auto& ln : so.text->lines) {
            if (!out.empty()) {
                out.push_back('\n');
            }
            out += model::plain_text(ln);
        }
        return out;
    }
    return {};
}

/// A measurer whose width is the number of bytes: layout rules checked
/// without fonts.
class LenTextMeasurer final : public layout::ITextMeasurer {
public:
    [[nodiscard]] double measure_width(std::string_view text, const layout::FontStyle&) override
    {
        return static_cast<double>(text.size());
    }
};

/// ``groups`` are the slide groups ``expected`` (a golden fixture) lists.
template <typename Groups>
void require_slide_groups(const nlohmann::json& expected, const Groups& groups)
{
    REQUIRE(groups.size() == expected.size());
    for (std::size_t i = 0; i < groups.size(); ++i) {
        REQUIRE(groups[i].indices == expected[i].at("indices").get<std::vector<int>>());
        REQUIRE(groups[i].active_count == expected[i].at("active_count").get<int>());
        REQUIRE(groups[i].is_title == expected[i].value("is_title", false));
    }
}

/// A two-verse song item ``title`` that plays on stack ``stack``.
[[nodiscard]] inline model::ServiceItem song_item_on_stack(std::int64_t id, const std::string& title,
                                                           const std::string& stack)
{
    model::ServiceItem item;
    item.item_id = id;
    item.item_type = "song";
    item.data = {{"title", title},
                 {"lyrics", "[V1]\nOne\n\n[V2]\nTwo"},
                 {"verse_order", "v1 v2"},
                 {"stack_id", stack}};
    return item;
}

}  // namespace seventhslide::test
