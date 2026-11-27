// The Browser tab's thumbnails: each page drawn off screen and kept once it
// has loaded, named by its look (with a stand-in renderer; see
// fake_web_renderer.hpp).

#include "session_facade_fixture.hpp"

#include "fake_web_renderer.hpp"
#include "web_library_controller.hpp"
#include "web_thumbs.hpp"
#include "test_wait.hpp"

#include <seventhslide/model/web_page.hpp>

#include <QDeadlineTimer>
#include <QTest>

#include <filesystem>

using namespace seventhslide::test::facade;
using seventhslide::app::WebThumbs;
using seventhslide::model::WebPage;
using seventhslide::test::FakeWebRenderers;

namespace {

WebPage page_at(std::string url, int width = 1920)
{
    WebPage p;
    p.url = std::move(url);
    p.width = width;
    return p;
}

using seventhslide::test::wait_until;

} // namespace

TEST_CASE("Each page's thumbnail is drawn once, kept by its look", "[app][web][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    WebThumbs thumbs([&] { return dir.path / "web"; });
    thumbs.set_settle_ms(0);
    FakeWebRenderers renderers;

    const WebPage a = page_at("https://example.com/a");
    const WebPage a_again = page_at("https://example.com/a");
    const WebPage b = page_at("https://example.com/b");
    const WebPage wide = page_at("https://example.com/a", 1280);
    CHECK(WebThumbs::key_of(a) == WebThumbs::key_of(a_again));
    CHECK(WebThumbs::key_of(a) != WebThumbs::key_of(b));
    CHECK(WebThumbs::key_of(a) != WebThumbs::key_of(wide));
    CHECK(thumbs.url_for(a).isEmpty());

    // Without a way to draw, nothing is drawn; asked again once there is.
    thumbs.want({a, a_again, b});
    CHECK(renderers.made.empty());
    thumbs.set_factory(renderers.factory());
    QSignalSpy made(&thumbs, &WebThumbs::made);
    REQUIRE(wait_until([&] { return thumbs.pending() == 0; }));
    // Two looks, two thumbnails, one at a time.
    CHECK(renderers.made.size() == 2);
    CHECK(made.count() == 2);
    CHECK(std::filesystem::exists(thumbs.path_of(a)));
    CHECK(thumbs.url_for(a).startsWith(QStringLiteral("file://")));
    CHECK(thumbs.url_for(a).contains(QStringLiteral("?v=")));
    CHECK(thumbs.url_for(a) == thumbs.url_for(a_again));
    // Kept at most 512 pixels on its longest edge.
    const QImage kept(QString::fromStdString(thumbs.path_of(a).string()));
    CHECK(kept.width() == WebThumbs::k_max_edge);

    // Asked again: there is one already.
    thumbs.want({a, b});
    CHECK(thumbs.pending() == 0);

    // A page that never loads is given up on, and not asked again this run.
    renderers.draw_on_load = false;
    thumbs.set_give_up_ms(10);
    thumbs.want({wide});
    REQUIRE(wait_until([&] { return thumbs.pending() == 0; }));
    CHECK_FALSE(std::filesystem::exists(thumbs.path_of(wide)));
    const auto drawn = renderers.made.size();
    thumbs.want({wide});
    CHECK(renderers.made.size() == drawn);

    // Thumbnails no page has any more go.
    thumbs.prune({b});
    CHECK_FALSE(std::filesystem::exists(thumbs.path_of(a)));
    CHECK(std::filesystem::exists(thumbs.path_of(b)));
}
