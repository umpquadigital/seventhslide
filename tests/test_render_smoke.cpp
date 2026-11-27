#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"
#include "pixel_compare.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/render/text_painter.hpp>

#include "oracle_render.hpp"

#include <QImage>

#include <cstdlib>
#include <filesystem>
#include <string>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::render_smoke::load_json;
using seventhslide::render_smoke::render_oracle_song_frame;
using seventhslide::test::count_ink;
using seventhslide::test::count_ink_in;
using seventhslide::test::mean_abs_diff;

namespace {

using seventhslide::test::oracle_dir;

std::filesystem::path golden_dir()
{
    return std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "render_smoke";
}

[[nodiscard]] bool update_goldens()
{
    const char* v = std::getenv("SEVENTHSLIDE_UPDATE_RENDER_SMOKE");
    return v != nullptr && v[0] != '\0' && std::string(v) != "0";
}

}  // namespace

TEST_CASE("Render smoke: fixture songs render 1080p with ink", "[render][render_smoke]")
{
    seventhslide::test::ensure_qt_application();
    ApproxTextMeasurer measurer;
    const auto manifest = load_json(oracle_dir() / "manifest.json");

    for (const auto& id_j : manifest.at("fixtures")) {
        const auto id = id_j.get<std::string>();
        INFO(id);
        const auto oracle = load_json(oracle_dir() / (id + ".json"));
        const auto result = render_oracle_song_frame(oracle, measurer, 0, false);

        REQUIRE(result.image.width() == 1920);
        REQUIRE(result.image.height() == 1080);
        REQUIRE(count_ink(result.image) > 200);

        // Text should land inside the lyrics box from the resolved style.
        const int bx = result.frame.text ? result.frame.text->box.x : 320;
        const int by = result.frame.text ? result.frame.text->box.y : 340;
        const int bw = result.frame.text ? result.frame.text->box.width : 1280;
        const int bh = result.frame.text ? result.frame.text->box.height : 400;
        REQUIRE(count_ink_in(result.image, QRect(bx, by, bw, bh)) > 100);

        // Determinism: second render matches the first.
        const auto again = render_oracle_song_frame(oracle, measurer, 0, false);
        REQUIRE(mean_abs_diff(result.image, again.image) == Catch::Approx(0.0).margin(0.01));

        // Optional golden compare / update.
        const auto golden_path = golden_dir() / (id + "_line0.png");
        if (update_goldens()) {
            std::filesystem::create_directories(golden_dir());
            REQUIRE(result.image.save(QString::fromStdString(golden_path.string()), "PNG"));
        } else if (std::filesystem::exists(golden_path)) {
            QImage golden(QString::fromStdString(golden_path.string()));
            REQUIRE_FALSE(golden.isNull());
            // AA / fontconfig drift budget - structural smoke, not Electron parity.
            REQUIRE(mean_abs_diff(result.image, golden) < 12.0);
        }
    }
}

TEST_CASE("Render smoke: blank clears text ink", "[render][render_smoke]")
{
    seventhslide::test::ensure_qt_application();
    ApproxTextMeasurer measurer;
    const auto oracle = load_json(oracle_dir() / "song_simple_v1.json");
    const auto live = render_oracle_song_frame(oracle, measurer, 0, false);
    const auto blank = render_oracle_song_frame(oracle, measurer, 0, true);

    REQUIRE(count_ink(live.image) > count_ink(blank.image));
    REQUIRE(blank.frame.is_blank);
    // Blank keeps background only - near-zero ink on a solid black stage.
    REQUIRE(count_ink(blank.image) < 50);
}

TEST_CASE("Render smoke: pixel_compare detects differences", "[render]")
{
    seventhslide::test::ensure_qt_application();
    QImage a(64, 64, QImage::Format_RGB32);
    QImage b(64, 64, QImage::Format_RGB32);
    a.fill(Qt::black);
    b.fill(Qt::black);
    REQUIRE(mean_abs_diff(a, b) == Catch::Approx(0.0));
    b.fill(Qt::white);
    REQUIRE(mean_abs_diff(a, b) > 200.0);
}
