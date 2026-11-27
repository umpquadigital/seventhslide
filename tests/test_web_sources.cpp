// Web pages live: a page goes into a service and onto the stacks, one
// renderer draws each page showing, and its pictures reach the outputs'
// previews and the controller (with a stand-in renderer; see
// fake_web_renderer.hpp).

#include "session_facade_fixture.hpp"

#include "fake_web_renderer.hpp"
#include "output_preview_controller.hpp"
#include "web_library_controller.hpp"
#include "web_sources.hpp"

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/model/web_page.hpp>

#include <QTest>

#include <cmath>
#include <vector>

using namespace seventhslide::test::facade;
using seventhslide::test::FakeWebRenderers;

namespace {

qint64 save_page(SessionFacade& facade, const QString& url, int width = 1920, int height = 1080,
                 double zoom = 1.0)
{
    const auto saved = facade.webLibrary()->savePage(-1, {{QStringLiteral("url"), url},
                                                          {QStringLiteral("width"), width},
                                                          {QStringLiteral("height"), height},
                                                          {QStringLiteral("zoom"), zoom}});
    REQUIRE(saved.contains(QStringLiteral("id")));
    return saved.value(QStringLiteral("id")).toLongLong();
}

QColor centre(const QImage& image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

} // namespace

TEST_CASE("A web page plays live, drawn by one renderer at its own size", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto id = save_page(facade, QStringLiteral("example.com/clock"), 1280, 720, 1.25);

    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.program()->webMode());
    CHECK(facade.live()->mode() == QStringLiteral("web"));
    REQUIRE(renderers.running() == 1);
    const auto* page = renderers.last();
    CHECK(page->url_ == QUrl(QStringLiteral("https://example.com/clock")));
    CHECK(page->size_ == QSize(1280, 720));
    CHECK(page->zoom_ == 1.25);

    // The frame owns the canvas, black behind the page, which fills it.
    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Web);
    REQUIRE(frames[0].web.has_value());
    CHECK(frames[0].web->source == facade.program()->webSource().toStdString());
    CHECK(frames[0].background.color == "#000000");

    // The output's preview and the controller show the page's picture.
    const auto out_id = QString::fromStdString(facade.session().primary_output()->id);
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          == QColor(Qt::red));
    CHECK(facade.outputPreviews()->live_web_frame(facade.program()->webSource()).size()
          == QSize(1280, 720));

    // Reload asks the page's renderer, and its new picture shows.
    QSignalSpy pictures(&facade, &SessionFacade::webFrameChanged);
    REQUIRE(facade.program()->reloadWeb());
    CHECK(renderers.last()->reloads_ == 1);
    CHECK(pictures.count() == 1);
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          == QColor(Qt::green));

    // Something else live on that stack stops the page.
    REQUIRE(facade.webLibrary()->savePage(id, {{QStringLiteral("url"), QStringLiteral("example.com")}})
                .contains(QStringLiteral("id")));
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    REQUIRE(facade.live()->playImage(QStringLiteral("a.png")));
    CHECK_FALSE(facade.program()->webMode());
    CHECK(renderers.running() == 0);
    CHECK(facade.web_sources()->frames().empty());
}

TEST_CASE("A web page in a service plays as it was added", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Web svc")) > 0);
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto id = save_page(facade, QStringLiteral("https://example.com/welcome"));
    REQUIRE(facade.webLibrary()->savePage(id, {{QStringLiteral("url"),
                                                QStringLiteral("https://example.com/welcome")},
                                               {QStringLiteral("name"), QStringLiteral("Welcome")}})
                .contains(QStringLiteral("id")));

    REQUIRE(facade.webLibrary()->addPageToService(id));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    const auto row = facade.serviceItems()->index(0);
    CHECK(facade.serviceItems()->data(row, seventhslide::app::ServiceItemModel::TitleRole).toString()
          == QStringLiteral("Welcome"));
    CHECK(facade.serviceItems()->data(row, seventhslide::app::ServiceItemModel::ItemTypeRole).toString()
          == QStringLiteral("web"));

    // The library's page changes; the service keeps what it added.
    REQUIRE(facade.webLibrary()->savePage(id, {{QStringLiteral("url"),
                                                QStringLiteral("https://example.com/other")}})
                .contains(QStringLiteral("id")));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->webMode());
    REQUIRE(renderers.running() == 1);
    CHECK(renderers.last()->url_ == QUrl(QStringLiteral("https://example.com/welcome")));

    // A folder adds its pages in order, one item each.
    const auto folder = facade.webLibrary()->createFolder(QStringLiteral("Screens"));
    const auto a = save_page(facade, QStringLiteral("a.example.com"));
    const auto b = save_page(facade, QStringLiteral("b.example.com"));
    REQUIRE(facade.webLibrary()->moveItems({QVariant(a), QVariant(b)}, folder).isEmpty());
    REQUIRE(facade.webLibrary()->addFolderToService(folder));
    REQUIRE(facade.serviceItems()->rowCount() == 3);
    CHECK(facade.serviceItems()->data(facade.serviceItems()->index(2),
                                      seventhslide::app::ServiceItemModel::TitleRole)
              .toString()
          == QStringLiteral("b.example.com"));
}

TEST_CASE("The controller says why a live page is not showing", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = save_page(facade, QStringLiteral("example.com"));

    // No renderer in this build: the plate stays black, and the controller says why.
    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.program()->webProblem().contains(QStringLiteral("cannot be shown")));
    CHECK_FALSE(facade.program()->reloadWeb());

    // With one, the page is loading until it draws.
    FakeWebRenderers renderers;
    renderers.draw_on_load = false;
    facade.set_web_renderer_factory(renderers.factory());
    REQUIRE(renderers.running() == 1);
    CHECK(facade.program()->webProblem().isEmpty());
    CHECK(facade.program()->webLoading());
    QSignalSpy state(facade.program(), &seventhslide::app::SideView::webStateChanged);
    renderers.last()->draw(QColor(Qt::red));
    CHECK(state.count() == 1);
    CHECK_FALSE(facade.program()->webLoading());

    // A page that fails to load says so.
    renderers.last()->fail(QStringLiteral("The page could not be loaded: no such host"));
    CHECK(state.count() == 2);
    CHECK(facade.program()->webProblem() == QStringLiteral("The page could not be loaded: no such host"));
}

TEST_CASE("The controller sends the live page to an address typed", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = save_page(facade, QStringLiteral("example.com/welcome"));

    // No page live: there is nothing to send.
    CHECK_FALSE(facade.program()->webGoTo(QStringLiteral("example.org")).isEmpty());

    // No renderer in this build: it says why.
    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.program()->webGoTo(QStringLiteral("example.org"))
              .contains(QStringLiteral("cannot be shown")));

    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    REQUIRE(renderers.running() == 1);
    auto* page = renderers.last();

    // Read as the Browser tab reads an address, and gone to as a link is:
    // the controller's address follows, and Previous comes back.
    QSignalSpy state(facade.program(), &seventhslide::app::SideView::webStateChanged);
    CHECK(facade.program()->webGoTo(QStringLiteral("  example.org/next ")).isEmpty());
    REQUIRE(page->navigations_.size() == 1);
    CHECK(page->navigations_[0] == QUrl(QStringLiteral("https://example.org/next")));
    CHECK(state.count() == 1);
    CHECK(facade.program()->webUrl() == QStringLiteral("https://example.org/next"));
    REQUIRE(facade.program()->webCanGoBack());
    REQUIRE(facade.program()->webGoBack());
    CHECK(facade.program()->webUrl() == QStringLiteral("https://example.com/welcome"));

    // A file's path is that file, whatever its name holds; the address
    // shown has its spaces as %20, so it can be edited and gone to again.
    const QString file = QStringLiteral("/home/me/My Slides/wall #2.html");
    CHECK(facade.program()->webGoTo(file).isEmpty());
    REQUIRE(page->navigations_.size() == 2);
    CHECK(page->navigations_.back().toLocalFile() == file);
    const QString shown = facade.program()->webUrl();
    CHECK(shown == QStringLiteral("file:///home/me/My%20Slides/wall%20%232.html"));
    CHECK(facade.program()->webGoTo(shown).isEmpty());
    REQUIRE(page->navigations_.size() == 3);
    CHECK(page->navigations_.back().toLocalFile() == file);
    // A Windows path names the file there (the drive in the path, a share's
    // server as the host), and is shown so that it can be gone to again.
    CHECK(facade.program()->webGoTo(QStringLiteral("C:\\Users\\me\\My Slides\\wall.html")).isEmpty());
    REQUIRE(page->navigations_.size() == 4);
    CHECK(page->navigations_.back().scheme() == QStringLiteral("file"));
    CHECK(page->navigations_.back().path() == QStringLiteral("/C:/Users/me/My Slides/wall.html"));
    CHECK(facade.program()->webUrl() == QStringLiteral("file:///C:/Users/me/My%20Slides/wall.html"));
    CHECK(facade.program()->webGoTo(QStringLiteral("\\\\church-nas\\Media\\wall.html")).isEmpty());
    REQUIRE(page->navigations_.size() == 5);
    CHECK(page->navigations_.back().host() == QStringLiteral("church-nas"));
    CHECK(page->navigations_.back().path() == QStringLiteral("/Media/wall.html"));
    const QString share = facade.program()->webUrl();
    CHECK(share == QStringLiteral("file://church-nas/Media/wall.html"));
    CHECK(facade.program()->webGoTo(share).isEmpty());
    REQUIRE(page->navigations_.size() == 6);
    CHECK(page->navigations_.back() == page->navigations_[4]);

    // What is not a page's address is refused, and the page stays.
    for (const char* text :
         {"", "   ", "javascript:alert(1)", "mailto:a@example.com", "two words"}) {
        CAPTURE(text);
        CHECK_FALSE(facade.program()->webGoTo(QString::fromUtf8(text)).isEmpty());
    }
    CHECK(page->navigations_.size() == 6);
    CHECK(facade.program()->webUrl() == share);

    // A renderer that could not start goes nowhere, and says why.
    page->started_ = false;
    page->fail(QStringLiteral("Web pages cannot be drawn on this computer's graphics."));
    CHECK(facade.program()->webGoTo(QStringLiteral("example.org"))
          == QStringLiteral("Web pages cannot be drawn on this computer's graphics."));
    CHECK(page->navigations_.size() == 6);
}

TEST_CASE("Two stacks showing one page each draw it in a tab of their own", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto id = save_page(facade, QStringLiteral("example.com"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Second")).isEmpty());
    const auto first = facade.session().stack_defs().front().id;
    const auto second = facade.session().stack_defs().back().id;

    facade.stacks()->armSendTo(QString::fromStdString(first));
    REQUIRE(facade.webLibrary()->playPage(id));
    facade.stacks()->armSendTo(QString::fromStdString(second));
    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.session().occupied_stacks().size() == 2);
    REQUIRE(renderers.running() == 2);

    const auto source_of = [&](const std::string& stack) {
        return facade.session().runtime({stack})->scene_extras().web->source;
    };
    REQUIRE(source_of(first) != source_of(second));
    auto* one = facade.web_sources()->renderer(source_of(first));
    auto* two = facade.web_sources()->renderer(source_of(second));
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK(one != two);
    // A link followed on one leaves the other where it was.
    static_cast<seventhslide::test::FakeWebRenderer*>(one)->follow(
        QUrl(QStringLiteral("https://example.com/next")));
    CHECK(one->can_go_back());
    CHECK_FALSE(two->can_go_back());
    // Each page's sound is on its own stack's strip.
    CHECK(facade.web_sources()->sound_strip(source_of(first)) == first);
    CHECK(facade.web_sources()->sound_strip(source_of(second)) == second);
}

TEST_CASE("A page sent again in place of itself stays in its tab", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto id = save_page(facade, QStringLiteral("example.com"));
    const auto other = save_page(facade, QStringLiteral("example.org"));

    REQUIRE(facade.webLibrary()->playPage(id));
    const auto source = facade.program()->webSource();
    auto* page = renderers.last();
    page->follow(QUrl(QStringLiteral("https://example.com/next")));

    // Again: the same tab, not loaded again, still where the link took it.
    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.program()->webSource() == source);
    CHECK(renderers.made.size() == 1);
    CHECK(renderers.last() == page);
    CHECK(page->can_go_back());

    // Another page in between: a tab of its own, and the first one again
    // later is a new one.
    REQUIRE(facade.webLibrary()->playPage(other));
    CHECK(facade.program()->webSource() != source);
    REQUIRE(facade.webLibrary()->playPage(id));
    CHECK(facade.program()->webSource() != source);
    CHECK(renderers.made.size() == 3);
    CHECK(renderers.running() == 1);
}

TEST_CASE("A page cued in Preview is on no output, and the same tab goes live at the take",
          "[app][web][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto main = facade.stacks()->focusedStackId();
    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    const auto live_id = save_page(facade, QStringLiteral("example.com/live"), 1280, 720);
    const auto cued_id = save_page(facade, QStringLiteral("example.com/cued"), 1280, 720);
    auto* web = facade.web_sources();
    const auto main_stack = main.toStdString();

    // The same page live and cued beside it: two tabs.
    REQUIRE(facade.webLibrary()->playPage(live_id, /*live=*/true));
    const auto live = facade.program()->webSource().toStdString();
    REQUIRE(facade.webLibrary()->playPage(live_id));
    REQUIRE(facade.preview()->webMode());
    CHECK(facade.preview()->webSource().toStdString() != live);
    CHECK(renderers.running() == 2);
    REQUIRE(facade.stacks()->clearPreview(main).isEmpty());
    CHECK(renderers.running() == 1);

    // Cued: drawn at the size it will show, for the previews alone, its sound
    // on the cue strip.
    REQUIRE(facade.webLibrary()->playPage(cued_id));
    const auto cued = facade.preview()->webSource().toStdString();
    auto* page = renderers.last();
    REQUIRE(page != nullptr);
    CHECK(page->size_ == QSize(1280, 720));
    CHECK(web->frames().contains(cued));
    CHECK_FALSE(web->output_frames().contains(cued));
    CHECK(web->output_frames().contains(live));
    CHECK(web->sound_strip(cued) == seventhslide::audio::cue_strip_id(main_stack));
    CHECK_FALSE(facade.outputPreviews()->live_web_frame(facade.preview()->webSource()).isNull());
    // Worked in Preview: a link, typed there.
    REQUIRE(facade.preview()->webGoTo(QStringLiteral("example.com/cued/2")).isEmpty());
    CHECK(facade.preview()->webCanGoBack());

    // Taken with a fade: the same renderer, where it was left, on the
    // outputs and its stack's strip; nothing loads again.
    REQUIRE(facade.stacks()
                ->transitionWith(main, QStringLiteral("fade"), 400, QString())
                .isEmpty());
    CHECK(facade.program()->webSource().toStdString() == cued);
    CHECK(web->renderer(cued) == page);
    CHECK(renderers.made.size() == 3);
    CHECK(page->reloads_ == 0);
    CHECK(facade.program()->webCanGoBack());
    CHECK(web->output_frames().contains(cued));
    CHECK(web->sound_strip(cued) == main_stack);

    // What was live is back in Preview, still loaded, its sound on the cue
    // strip; it stays on the outputs for the fade it goes out with.
    CHECK(facade.preview()->webSource().toStdString() == live);
    CHECK(web->renderer(live) != nullptr);
    CHECK(web->sound_strip(live) == seventhslide::audio::cue_strip_id(main_stack));
    CHECK(web->output_frames().contains(live));
    REQUIRE(QTest::qWaitFor([&] { return !web->output_frames().contains(live); }, 3000));
    CHECK(web->frames().contains(live));
    CHECK(renderers.running() == 2);
}

TEST_CASE("A page replaced on the air keeps its last picture for the change", "[app][web]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto main = facade.stacks()->focusedStackId();
    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    const auto id = save_page(facade, QStringLiteral("example.com"));
    REQUIRE(facade.webLibrary()->playPage(id, /*live=*/true));
    const auto source = facade.program()->webSource().toStdString();
    auto* web = facade.web_sources();

    // A song cued and taken with a fade over it, and the page cleared from
    // the Preview it went back to: its renderer stops at once, and its last
    // picture is on the outputs until the fade is over.
    auto song = demo_song();
    REQUIRE(facade.live()->playSong(facade.song_repo()->insert(song)));
    REQUIRE(facade.stacks()
                ->transitionWith(main, QStringLiteral("fade"), 400, QString())
                .isEmpty());
    REQUIRE(facade.stacks()->clearPreview(main).isEmpty());
    CHECK(renderers.running() == 0);
    CHECK_FALSE(web->frames().contains(source));
    CHECK(web->output_frames().contains(source));
    REQUIRE(QTest::qWaitFor([&] { return !web->output_frames().contains(source); }, 3000));
}

TEST_CASE("A page's sound plays on the strip of the stack showing it", "[app][web][audio]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = save_page(facade, QStringLiteral("example.com"));
    REQUIRE(facade.webLibrary()->playPage(id));
    const auto stack = facade.session().focused_stack().id();

    // Its own mixer, run by hand, and its own WebSources over the session.
    seventhslide::audio::Mixer mixer;
    seventhslide::audio::Routing routing;
    for (const auto& def : facade.session().stack_defs()) {
        routing.stacks.push_back(def.id);
    }
    routing.outputs.push_back({"leaves-out", routing.stacks, {"web"}});
    routing.outputs.push_back({"carries", routing.stacks});
    mixer.set_routing(routing);
    FakeWebRenderers renderers;
    seventhslide::app::WebSources sources(facade.session(), &mixer);
    sources.set_factory(renderers.factory());
    sources.sync();
    REQUIRE(renderers.running() == 1);
    const auto source = facade.program()->webSource().toStdString();
    CHECK(sources.sound_strip(source) == stack);

    const auto level = [&] {
        return mixer.meter(seventhslide::audio::StripKind::Stack, stack).level_db[0];
    };
    std::vector<float> listen(seventhslide::audio::k_block_frames * seventhslide::audio::k_channels);
    mixer.process(listen.data(), seventhslide::audio::k_block_frames);
    CHECK(std::isinf(level()));

    // A quarter second of a page's tone, in its 2048-frame chunks.
    std::vector<float> tone(2048 * seventhslide::audio::k_channels, 0.25f);
    for (int i = 0; i < 6; ++i) {
        renderers.last()->sound_.push(tone.data(), 2048, seventhslide::audio::k_sample_rate);
    }
    for (int i = 0; i < 5; ++i) {
        mixer.process(listen.data(), seventhslide::audio::k_block_frames);
    }
    CHECK(level() > -20.0f);
    // An output leaving web pages out does not carry it.
    CHECK(std::isinf(mixer.meter(seventhslide::audio::StripKind::Output, "leaves-out").level_db[0]));
    CHECK(mixer.meter(seventhslide::audio::StripKind::Output, "carries").level_db[0] > -20.0f);

    // Something else live on that stack: the page and its sound go.
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    REQUIRE(facade.live()->playImage(QStringLiteral("a.png")));
    sources.sync();
    CHECK(renderers.running() == 0);
    CHECK(sources.sound_strip(source).empty());
}
