#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>


#include <seventhslide/render/canvas_fit.hpp>

#include <cmath>

using seventhslide::render::CanvasFit;
using seventhslide::render::fit_canvas;

namespace {

[[nodiscard]] bool nearly_eq(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

}  // namespace

TEST_CASE("fit_canvas matches output.html fitCanvas scale", "[render]")
{
    SECTION("identical aspect fills edge-to-edge")
    {
        const CanvasFit f = fit_canvas(1920, 1080, 1920, 1080);
        REQUIRE(f.scale == Catch::Approx(1.0));
        REQUIRE(nearly_eq(f.offset_x, 0.0));
        REQUIRE(nearly_eq(f.offset_y, 0.0));
    }

    SECTION("2x same aspect (4K panel)")
    {
        const CanvasFit f = fit_canvas(1920, 1080, 3840, 2160);
        REQUIRE(f.scale == Catch::Approx(2.0));
        REQUIRE(nearly_eq(f.offset_x, 0.0));
        REQUIRE(nearly_eq(f.offset_y, 0.0));
    }

    SECTION("downscale 1280x720")
    {
        const CanvasFit f = fit_canvas(1920, 1080, 1280, 720);
        REQUIRE(f.scale == Catch::Approx(1280.0 / 1920.0));
        REQUIRE(nearly_eq(f.offset_x, 0.0));
        REQUIRE(nearly_eq(f.offset_y, 0.0));
    }

    SECTION("letterbox taller viewport")
    {
        // 1920x1080 into 1920x1200 -> scale 1, vertical bars (letterbox)
        const CanvasFit f = fit_canvas(1920, 1080, 1920, 1200);
        REQUIRE(f.scale == Catch::Approx(1.0));
        REQUIRE(nearly_eq(f.offset_x, 0.0));
        REQUIRE(f.offset_y == Catch::Approx((1200.0 - 1080.0) * 0.5));
    }

    SECTION("pillarbox wider viewport")
    {
        // 1920x1080 into 2560x1080 -> scale 1, side bars
        const CanvasFit f = fit_canvas(1920, 1080, 2560, 1080);
        REQUIRE(f.scale == Catch::Approx(1.0));
        REQUIRE(f.offset_x == Catch::Approx((2560.0 - 1920.0) * 0.5));
        REQUIRE(nearly_eq(f.offset_y, 0.0));
    }

    SECTION("width-limited scale Math.min(vw/cw, vh/ch)")
    {
        // 800x600 vs 1920x1080 -> min(800/1920, 600/1080) = 800/1920
        const CanvasFit f = fit_canvas(1920, 1080, 800, 600);
        const double expected = std::min(800.0 / 1920.0, 600.0 / 1080.0);
        REQUIRE(f.scale == Catch::Approx(expected));
        REQUIRE(f.offset_x == Catch::Approx((800.0 - 1920.0 * expected) * 0.5));
        REQUIRE(f.offset_y == Catch::Approx((600.0 - 1080.0 * expected) * 0.5));
    }

    SECTION("height-limited scale")
    {
        // 1600x600 vs 1920x1080 -> min(1600/1920, 600/1080) = 600/1080
        const CanvasFit f = fit_canvas(1920, 1080, 1600, 600);
        const double expected = std::min(1600.0 / 1920.0, 600.0 / 1080.0);
        REQUIRE(f.scale == Catch::Approx(expected));
        REQUIRE(f.offset_y == Catch::Approx(0.0).margin(1e-9));
        REQUIRE(f.offset_x == Catch::Approx((1600.0 - 1920.0 * expected) * 0.5));
    }

    SECTION("zero dimensions → identity scale")
    {
        const CanvasFit f = fit_canvas(1920, 1080, 0, 1080);
        REQUIRE(f.scale == Catch::Approx(1.0));
        REQUIRE(nearly_eq(f.offset_x, 0.0));
        REQUIRE(nearly_eq(f.offset_y, 0.0));
    }
}
