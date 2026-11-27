// A web page's picture on the canvas: fitted into its area over a black
// plate, under a stack and over one; and an output's presenter re-composites
// only when a page it shows draws.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/web_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QImage>

using seventhslide::render::SourceFrames;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneWebMedia;

namespace {

QImage solid(int w, int h, Qt::GlobalColor colour)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

SceneFrame web_frame(const std::string& source)
{
    SceneFrame frame;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.mode = SceneMode::Web;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneWebMedia web;
    web.source = source;
    web.url = "https://example.com";
    web.width = 1080;
    web.height = 1080;
    frame.web = web;
    return frame;
}

QImage composite(const SceneFrame& frame, const SourceFrames& pictures)
{
    seventhslide::render::SceneResources res;
    res.source_frames = &pictures;
    return seventhslide::render::render_scene_frame_image(frame, 192, 108, QColor(Qt::black), res);
}

} // namespace

TEST_CASE("A web page's picture is contained in the canvas over black", "[render][web]")
{
    seventhslide::test::ensure_qt_application();
    const SceneFrame frame = web_frame("page");
    const SourceFrames pictures{{"page", {solid(1080, 1080, Qt::red), {}}}};
    const QImage out = composite(frame, pictures);
    // A square page in a 16:9 canvas: red in the middle, black either side.
    CHECK(out.pixelColor(96, 54) == QColor(Qt::red));
    CHECK(out.pixelColor(5, 54) == QColor(Qt::black));
    CHECK(out.pixelColor(186, 54) == QColor(Qt::black));

    // Cover fills the canvas, cropping the page.
    SceneFrame cover = frame;
    cover.web->fit = "cover";
    CHECK(composite(cover, pictures).pixelColor(5, 54) == QColor(Qt::red));

    // No picture yet (loading, or no renderer): the plate alone.
    CHECK(composite(frame, {}).pixelColor(96, 54) == QColor(Qt::black));
    // Blank paints none of it.
    SceneFrame blank = frame;
    blank.is_blank = true;
    blank.background.type = seventhslide::layout::BackgroundType::Transparent;
    CHECK(composite(blank, pictures).pixelColor(96, 54) == QColor(Qt::black));
}

TEST_CASE("A web page on a stack below shows its own picture", "[render][web]")
{
    seventhslide::test::ensure_qt_application();
    SceneFrame top;
    top.canvas_width = 1920;
    top.canvas_height = 1080;
    top.mode = SceneMode::Empty;
    top.stacked_below.push_back(seventhslide::scene::share_frame(web_frame("page")));
    const SourceFrames pictures{{"page", {solid(1080, 1080, Qt::blue), {}}}};
    CHECK(composite(top, pictures).pixelColor(96, 54) == QColor(Qt::blue));
    CHECK(seventhslide::render::frame_sources_of(top) == std::vector<std::string>{"page"});
}

TEST_CASE("A presenter re-composites only when a page it shows draws", "[render][web]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(seventhslide::scene::share_frame(web_frame("page"))));
    const auto shown = presenter.revision();

    // Another page drawing does not move this output.
    presenter.set_source_frames({{"other", {solid(10, 10, Qt::red), {}}}});
    CHECK(presenter.revision() == shown);

    // Its own page drawing does, once per new picture.
    const SourceFrames first{{"page", {solid(10, 10, Qt::red), {}}},
                             {"other", {solid(10, 10, Qt::red), {}}}};
    presenter.set_source_frames(first);
    const auto drawn = presenter.revision();
    CHECK(drawn > shown);
    presenter.set_source_frames(first);
    CHECK(presenter.revision() == drawn);
    CHECK(presenter.surface(QSize(192, 108), QColor(Qt::black)).pixelColor(96, 54)
          == QColor(Qt::red));
}

TEST_CASE("A frozen output holds its page, and shows the page as it is on thawing",
          "[render][web]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::render::ScenePresenter presenter;
    SceneFrame frame = web_frame("page");
    REQUIRE(presenter.present(seventhslide::scene::share_frame(frame)));
    presenter.set_source_frames({{"page", {solid(10, 10, Qt::red), {}}}});

    SceneFrame frozen = frame;
    frozen.is_frozen = true;
    presenter.present(seventhslide::scene::share_frame(frozen));
    // The page changes while frozen, then stands still.
    presenter.set_source_frames({{"page", {solid(10, 10, Qt::blue), {}}}});
    CHECK(presenter.surface(QSize(192, 108), QColor(Qt::black)).pixelColor(96, 54)
          == QColor(Qt::red));

    const auto before = presenter.revision();
    presenter.present(seventhslide::scene::share_frame(frame));
    CHECK(presenter.revision() > before);
    CHECK(presenter.surface(QSize(192, 108), QColor(Qt::black)).pixelColor(96, 54)
          == QColor(Qt::blue));
}
