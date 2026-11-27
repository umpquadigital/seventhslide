// Live sources (step 5): a source found, played and put in a service; one
// receiver per source live, its pictures on the outputs' previews and the
// Controller, placed by the theme's Video area; its sound on its stack's
// strip. With a stand-in receiver (fake_live_receiver.hpp), and with NDI
// from this machine's own NDI output where the runtime is installed.

#include "session_facade_fixture.hpp"

#include "fake_live_receiver.hpp"
#include "live_library_controller.hpp"
#include "live_sources.hpp"
#include "live_thumb_item.hpp"
#include "open_library.hpp"
#include "output_preview_controller.hpp"
#include "test_wait.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/model/live_source.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/live_painter.hpp>
#include <seventhslide/render/ndi_output.hpp>
#include <seventhslide/render/ndi_receive.hpp>
#include <seventhslide/render/scene_compositor.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QPointer>
#include <QThread>
#include <QProcess>
#include <QStandardPaths>
#include <QTest>

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

using namespace seventhslide::test::facade;
using seventhslide::model::LiveSource;
using seventhslide::test::FakeLiveReceivers;

namespace {

QColor centre(const QImage& image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

QStringList rundown_titles(SessionFacade& facade)
{
    QStringList out;
    auto* m = facade.serviceItems();
    for (int r = 0; r < m->rowCount(); ++r) {
        out.push_back(m->data(m->index(r, 0), seventhslide::app::ServiceItemModel::TitleRole).toString());
    }
    return out;
}

std::vector<LiveSource> two_cameras()
{
    return {{"ndi", "STAGE (Camera 2)"}, {"ndi", "STAGE (Camera 1)"}};
}

} // namespace

TEST_CASE("A live source is remembered by its kind and name", "[live]")
{
    using seventhslide::model::ContentKind;
    CHECK(seventhslide::model::to_string(ContentKind::Live) == "live");
    CHECK(seventhslide::model::content_kind_from_string("live") == ContentKind::Live);

    const LiveSource camera{"ndi", "STAGE (Camera 1)"};
    const auto data = seventhslide::model::live_item_data(camera);
    CHECK(seventhslide::model::live_source_from_item_data(data) == camera);
    CHECK(seventhslide::model::live_source_key(camera) == "ndi:STAGE (Camera 1)");

    seventhslide::model::ServiceItem item;
    item.item_type = "live";
    item.data = data;
    CHECK(item.title() == "STAGE (Camera 1)");

    // A capture device, by its name too.
    const LiveSource webcam{"device", "HD Pro Webcam C920"};
    CHECK(seventhslide::model::live_source_from_item_data(seventhslide::model::live_item_data(webcam))
          == webcam);
    CHECK(seventhslide::model::live_source_key(webcam) == "device:HD Pro Webcam C920");

    // A DeckLink input, by its device's name.
    const LiveSource card{"decklink", "DeckLink Mini Recorder"};
    CHECK(seventhslide::model::live_source_from_item_data(seventhslide::model::live_item_data(card))
          == card);

    // A kind this build does not know, or no name: none.
    CHECK(seventhslide::model::live_source_from_item_data({{"kind", "sdi"}, {"name", "x"}}).empty());
    CHECK(seventhslide::model::live_source_from_item_data({{"kind", "ndi"}, {"name", ""}}).empty());
    CHECK(seventhslide::model::live_source_key({}).empty());
}

TEST_CASE("A live source's picture fills the Video area, contained", "[live][render]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::scene::SceneFrame frame;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.mode = seventhslide::scene::SceneMode::Live;
    frame.live = seventhslide::scene::SceneLiveMedia{.source = "ndi:CAM", .name = "CAM"};
    // A square picture in the right half of the canvas.
    frame.live->area = {.enabled = true, .x = 960, .y = 0, .width = 960, .height = 1080};
    QImage square(100, 100, QImage::Format_RGB32);
    square.fill(Qt::green);
    const seventhslide::render::SourceFrames pictures{{"ndi:CAM", {square, {}}}};
    seventhslide::render::SceneResources res;
    res.source_frames = &pictures;
    const QImage out = seventhslide::render::render_scene_frame_image(frame, 192, 108,
                                                                      QColor(Qt::black), res);
    CHECK(out.pixelColor(144, 54) == QColor(Qt::green));
    CHECK(out.pixelColor(40, 54) == QColor(Qt::black));
    CHECK(seventhslide::render::frame_sources_of(frame) == std::vector<std::string>{"ndi:CAM"});

    // No Video area in the theme: nothing.
    frame.live->area.enabled = false;
    CHECK(seventhslide::render::render_scene_frame_image(frame, 192, 108, QColor(Qt::black), res)
              .pixelColor(144, 54)
          == QColor(Qt::black));
}

TEST_CASE("The Live tab lists the sources found, by name", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    std::vector<LiveSource> on_network = two_cameras();
    live->set_finder([&] { return on_network; });
    CHECK(live->unavailable().isEmpty());
    const auto names = [&] {
        QStringList out;
        for (const auto& v : live->sources()) {
            out.push_back(v.toMap().value(QStringLiteral("name")).toString());
        }
        return out;
    };
    CHECK(names() == QStringList{QStringLiteral("STAGE (Camera 1)"), QStringLiteral("STAGE (Camera 2)")});

    // A source gone from the network leaves the list when it is looked at
    // again; looking happens while the tab shows.
    QSignalSpy changed(live, &seventhslide::app::LiveLibraryController::sourcesChanged);
    on_network.pop_back();
    live->refresh();
    CHECK(changed.count() == 1);
    CHECK(names() == QStringList{QStringLiteral("STAGE (Camera 2)")});
    live->refresh();
    CHECK(changed.count() == 1);
    live->setSearching(true);
    CHECK(live->searching());
    live->setSearching(false);
}

TEST_CASE("The Live tab takes a still of each source each time it shows", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    auto* live = facade.liveLibrary();
    std::vector<LiveSource> on_network = two_cameras();
    live->set_finder([&] { return on_network; });
    const auto row = [&](const QString& name) {
        for (const auto& v : live->sources()) {
            if (v.toMap().value(QStringLiteral("name")).toString() == name) {
                return v.toMap();
            }
        }
        return QVariantMap{};
    };
    const QString camera1 = QStringLiteral("ndi:STAGE (Camera 1)");

    // Hidden, the tab opens no streams.
    CHECK(live->previews() == 0);
    CHECK(live->thumbnail(camera1).isNull());

    // Showing: a preview stream each, closed as soon as it gives a 16:9
    // still.
    live->setSearching(true);
    // Then each looked at in full, one at a time, for its quality (NDI's
    // preview stream is smaller), closed at its first picture.
    using seventhslide::app::LiveQuality;
    REQUIRE(receivers.made.size() == 4);
    CHECK(receivers.qualities
          == std::vector{LiveQuality::Preview, LiveQuality::Preview, LiveQuality::Full, LiveQuality::Full});
    CHECK(live->previews() == 0);
    CHECK_FALSE(live->measuring());
    CHECK(receivers.running() == 0);
    CHECK(live->thumbnail(camera1).size()
          == QSize(seventhslide::app::LiveLibraryController::k_thumb_w,
                   seventhslide::app::LiveLibraryController::k_thumb_h));
    // Its quality, as it is in full, and frame rate. Its picture came.
    const auto camera = row(QStringLiteral("STAGE (Camera 1)"));
    CHECK(camera.value(QStringLiteral("key")).toString() == camera1);
    CHECK(camera.value(QStringLiteral("detail")).toString() == QStringLiteral("720p, 29.97 fps"));
    CHECK(camera.value(QStringLiteral("signal")).toString() == QStringLiteral("ok"));
    CHECK(row(QStringLiteral("STAGE (Camera 2)")).value(QStringLiteral("signal")).toString()
          == QStringLiteral("ok"));
    CHECK_FALSE(camera.value(QStringLiteral("live")).toBool());
    // Looked at again, it is not taken again.
    live->refresh();
    CHECK(receivers.made.size() == 4);

    // Live: marked, and its quality said from the stream received in full.
    REQUIRE(live->playSource(QStringLiteral("ndi"), QStringLiteral("STAGE (Camera 1)")));
    const auto shown = row(QStringLiteral("STAGE (Camera 1)"));
    CHECK(shown.value(QStringLiteral("live")).toBool());
    CHECK(shown.value(QStringLiteral("detail")).toString()
          == QStringLiteral("720p, 29.97 fps"));
    CHECK_FALSE(row(QStringLiteral("STAGE (Camera 2)")).value(QStringLiteral("live")).toBool());

    // Shown again: stills taken again, the one live from its picture (no
    // second stream), the other from a preview stream.
    live->setSearching(false);
    receivers.last()->draw(QColor(Qt::red));
    live->setSearching(true);
    // The live one, then Camera 2's preview and its look in full.
    CHECK(receivers.made.size() == 7);
    CHECK(live->thumbnail(camera1).pixelColor(160, 90) == QColor(Qt::red));

    // A source gone: its still dropped.
    on_network.pop_back();
    live->refresh();
    CHECK(live->thumbnail(camera1).isNull());

    // Sources with no picture yet keep their preview streams open, at most
    // a limit of them at once.
    live->setSearching(false);
    receivers.on = false;
    on_network.clear();
    for (int i = 0; i < 20; ++i) {
        on_network.push_back({"ndi", "HOST (Camera " + std::to_string(i) + ")"});
    }
    live->setSearching(true);
    CHECK(live->previews() == seventhslide::app::LiveLibraryController::k_max_previews);
    // One with a picture gives its still, and its place goes to the next;
    // it is looked at in full.
    receivers.last()->draw(QColor(Qt::green));
    QTest::qWait(10);
    CHECK(live->previews() == seventhslide::app::LiveLibraryController::k_max_previews);
    CHECK(receivers.made.size() == 7 + 17 + 1);
    CHECK(live->measuring());
    live->setSearching(false);
    CHECK(live->previews() == 0);
    CHECK_FALSE(live->measuring());
}

namespace {

/// A source's thumbnail item, whose picture a test can look at.
class ShownThumb : public seventhslide::app::LiveThumbItem {
public:
    /// The middle of the picture it shows; invalid for none.
    [[nodiscard]] QColor shown() const { return frame().isNull() ? QColor() : centre(frame()); }
};

} // namespace

TEST_CASE("A source's thumbnail is its session's, and follows only the one it holds", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    FakeLiveReceivers first_receivers;
    FakeLiveReceivers second_receivers;
    // The second's source gives no picture until the test draws one.
    second_receivers.on = false;
    SessionFacade first;
    REQUIRE(first.open_memory_database());
    first.set_live_receiver_factory(first_receivers.factory());
    auto second = std::make_unique<SessionFacade>();
    REQUIRE(second->open_memory_database());
    second->set_live_receiver_factory(second_receivers.factory());
    const std::vector<LiveSource> camera{LiveSource{"ndi", "STAGE (Camera 1)"}};
    first.liveLibrary()->set_finder([&] { return camera; });
    second->liveLibrary()->set_finder([&] { return camera; });

    ShownThumb thumb;
    thumb.setSourceKey(QStringLiteral("ndi:STAGE (Camera 1)"));
    QSignalSpy changed(&thumb, &seventhslide::app::SessionBoundItem::sessionChanged);
    thumb.setSession(&first);
    CHECK(changed.size() == 1);
    // No still taken yet: nothing shown, and no size.
    CHECK_FALSE(thumb.shown().isValid());
    CHECK(thumb.implicitWidth() == 0);
    // The session's still, as it is taken.
    first.liveLibrary()->setSearching(true);
    CHECK(thumb.shown() == QColor(Qt::blue));
    CHECK(thumb.implicitWidth() == seventhslide::app::LiveLibraryController::k_thumb_w);

    // Another session, with no still of the source yet: none shown, until
    // its own is taken.
    thumb.setSession(second.get());
    CHECK(changed.size() == 2);
    CHECK_FALSE(thumb.shown().isValid());
    second->liveLibrary()->setSearching(true);
    CHECK_FALSE(thumb.shown().isValid());
    REQUIRE(second_receivers.last() != nullptr);
    second_receivers.last()->draw(QColor(Qt::green));
    CHECK(seventhslide::test::wait_until([&] { return thumb.shown() == QColor(Qt::green); }));
    // The first's stills are no longer what it shows.
    first.liveLibrary()->setSearching(false);
    first.liveLibrary()->setSearching(true);
    CHECK(thumb.shown() == QColor(Qt::green));

    // The session going while the item holds it: it lets go of it as it
    // goes, as if set to none, and shows nothing of it.
    second.reset();
    CHECK(thumb.session() == nullptr);
    CHECK(changed.size() == 3);
    CHECK_FALSE(thumb.shown().isValid());
    thumb.setSourceKey(QStringLiteral("ndi:STAGE (Camera 2)"));
    CHECK_FALSE(thumb.shown().isValid());
    CHECK(thumb.implicitWidth() == 0);
    // And it takes another.
    thumb.setSourceKey(QStringLiteral("ndi:STAGE (Camera 1)"));
    thumb.setSession(&first);
    CHECK(thumb.shown() == QColor(Qt::blue));
}

TEST_CASE("A capture device's preview stream lets go of it before it goes live", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    receivers.on = false;
    facade.set_live_receiver_factory(receivers.factory());
    auto* live = facade.liveLibrary();
    live->set_finder([] { return std::vector<LiveSource>{{"device", "Webcam"}}; });
    live->setSearching(true);
    REQUIRE(live->previews() == 1);
    QPointer<seventhslide::test::FakeLiveReceiver> preview = receivers.last();

    // When the outputs' receiver is made, the preview's is gone already.
    bool preview_open_then = true;
    auto factory = receivers.factory();
    facade.set_live_receiver_factory([&](const LiveSource& source, seventhslide::app::LiveQuality quality) {
        if (quality == seventhslide::app::LiveQuality::Full) {
            preview_open_then = !preview.isNull();
        }
        return factory(source, quality);
    });
    REQUIRE(live->previews() == 1);
    preview = receivers.last();
    REQUIRE(live->playSource(QStringLiteral("device"), QStringLiteral("Webcam")));
    CHECK_FALSE(preview_open_then);
    CHECK(live->previews() == 0);
    // Its still, once it has a picture, is of that.
    receivers.last()->draw(QColor(Qt::blue));
    QTest::qWait(10);
    CHECK(live->thumbnail(QStringLiteral("device:Webcam")).pixelColor(160, 90) == QColor(Qt::blue));
    CHECK(live->previews() == 0);
    live->setSearching(false);
}

TEST_CASE("A capture device's quality is its preview stream's", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    auto* live = facade.liveLibrary();
    live->set_finder([] { return std::vector<LiveSource>{{"device", "Webcam"}}; });
    live->setSearching(true);
    // A device opens in full for its preview too: not looked at again.
    CHECK(receivers.made.size() == 1);
    CHECK_FALSE(live->measuring());
    // (The fake's preview is smaller, as NDI's is.)
    CHECK(live->sources().value(0).toMap().value(QStringLiteral("detail")).toString()
          == QStringLiteral("360p, 29.97 fps"));
    live->setSearching(false);
}

TEST_CASE("The Live tab looks for sources off the GUI thread", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    std::atomic<bool> on_gui{true};
    live->set_finder(
        [&] {
            on_gui = QThread::currentThread() == qApp->thread();
            return two_cameras();
        },
        true);
    CHECK(live->scanning());
    QDeadlineTimer deadline(5000);
    while (live->sources().size() < 2) {
        REQUIRE_FALSE(deadline.hasExpired());
        QTest::qWait(5);
    }
    CHECK_FALSE(on_gui);
    CHECK_FALSE(live->scanning());
    // A stream saved shows at once, without waiting for a search.
    REQUIRE(live->saveStream({}, {}, QStringLiteral("srt://stage.local:9000")).contains(QStringLiteral("key")));
    CHECK(live->sources().size() == 3);
}

TEST_CASE("The Live tab looks again on the thread it looked on", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    // A thread's first search finds its mark unset: each new thread counts.
    std::atomic<int> threads{0};
    std::atomic<int> searches{0};
    live->set_finder(
        [&] {
            static thread_local bool counted = false;
            if (!counted) {
                counted = true;
                ++threads;
            }
            ++searches;
            return two_cameras();
        },
        true);
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(seventhslide::test::wait_until([&] { return !live->scanning() && searches == i; }, 5000));
        if (i < 5) {
            live->refresh();
        }
    }
    CHECK(threads == 1);
}

TEST_CASE("A live source plays, received once, and shows where its frame does", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    receivers.on = false;
    facade.set_live_receiver_factory(receivers.factory());

    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("STAGE (Camera 1)")));
    CHECK(facade.program()->liveSourceMode());
    CHECK(facade.live()->mode() == QStringLiteral("live"));
    CHECK(facade.session().focused_stack().program.scene_extras().live.value().name == "STAGE (Camera 1)");
    CHECK(facade.program()->live_source_key() == QStringLiteral("ndi:STAGE (Camera 1)"));
    REQUIRE(receivers.running() == 1);
    CHECK(receivers.last()->source_ == LiveSource{"ndi", "STAGE (Camera 1)"});

    // Off the network: black, and the Controller says it is waiting.
    const auto out_id = QString::fromStdString(facade.session().primary_output()->id);
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          == QColor(Qt::black));
    CHECK(facade.program()->liveSourceProblem().contains(QStringLiteral("Waiting")));

    // On: its picture, in the frame and for the Controller.
    QSignalSpy state(facade.program(), &seventhslide::app::SideView::liveSourceStateChanged);
    QSignalSpy pictures(&facade, &SessionFacade::liveSourceFrameChanged);
    receivers.last()->draw(QColor(Qt::blue));
    CHECK(state.count() >= 1);
    CHECK(pictures.count() == 1);
    CHECK(facade.program()->liveSourceProblem().isEmpty());
    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Live);
    REQUIRE(frames[0].live.has_value());
    CHECK(frames[0].live->area.enabled);  // the default theme's Video area
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          == QColor(Qt::blue));
    CHECK(facade.outputPreviews()->live_source_frame(facade.program()->live_source_key()).size()
          == QSize(1280, 720));

    // Played again: the same receiver. Something else on that stack: it stops.
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("STAGE (Camera 1)")));
    CHECK(receivers.made.size() == 1);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    REQUIRE(facade.live()->playImage(QStringLiteral("a.png")));
    CHECK(receivers.running() == 0);
    CHECK(facade.live_sources()->frames().empty());
}

TEST_CASE("A live source goes into a service and plays from it", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Live svc")) > 0);
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());

    REQUIRE(facade.liveLibrary()->addSourceToService(QStringLiteral("ndi"),
                                                     QStringLiteral("STAGE (Camera 2)")));
    CHECK_FALSE(facade.liveLibrary()->addSourceToService(QStringLiteral("sdi"), QStringLiteral("x")));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    const auto row = facade.serviceItems()->index(0);
    CHECK(facade.serviceItems()->data(row, seventhslide::app::ServiceItemModel::TitleRole).toString()
          == QStringLiteral("STAGE (Camera 2)"));
    CHECK(facade.serviceItems()->data(row, seventhslide::app::ServiceItemModel::ItemTypeRole).toString()
          == QStringLiteral("live"));
    REQUIRE(facade.live()->selectServiceItem(0));
    CHECK(facade.program()->liveSourceMode());
    REQUIRE(receivers.running() == 1);
    CHECK(receivers.last()->source_.name == "STAGE (Camera 2)");
}

TEST_CASE("A live source's sound plays on the strip of the stack showing it", "[app][live][audio]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("CAM")));
    const auto stack = facade.session().focused_stack().id();

    seventhslide::audio::Mixer mixer;
    seventhslide::audio::Routing routing;
    for (const auto& def : facade.session().stack_defs()) {
        routing.stacks.push_back(def.id);
    }
    routing.outputs.push_back({"leaves-out", routing.stacks, {"live"}});
    routing.outputs.push_back({"carries", routing.stacks});
    mixer.set_routing(routing);
    FakeLiveReceivers receivers;
    seventhslide::app::LiveSources sources(facade.session(), &mixer);
    sources.set_factory(receivers.factory());
    sources.sync();
    REQUIRE(receivers.running() == 1);
    CHECK(sources.sound_strip("ndi:CAM") == stack);

    std::vector<float> tone(2048 * seventhslide::audio::k_channels, 0.25f);
    for (int i = 0; i < 6; ++i) {
        receivers.last()->sound_.push(tone.data(), 2048, seventhslide::audio::k_sample_rate);
    }
    std::vector<float> listen(seventhslide::audio::k_block_frames * seventhslide::audio::k_channels);
    for (int i = 0; i < 5; ++i) {
        mixer.process(listen.data(), seventhslide::audio::k_block_frames);
    }
    CHECK(mixer.meter(seventhslide::audio::StripKind::Stack, stack).level_db[0] > -20.0f);
    // An output leaving live sources out does not carry it.
    CHECK(std::isinf(mixer.meter(seventhslide::audio::StripKind::Output, "leaves-out").level_db[0]));
    CHECK(mixer.meter(seventhslide::audio::StripKind::Output, "carries").level_db[0] > -20.0f);
}

TEST_CASE("A source cued in Preview is on no output; the take puts the same receiver live",
          "[app][live][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    const auto main = facade.stacks()->focusedStackId();
    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    const auto stack = main.toStdString();
    auto* sources = facade.live_sources();
    const std::string one = "ndi:STAGE (Camera 1)";
    const std::string two = "ndi:STAGE (Camera 2)";

    // Live on Main, then the other camera cued beside it.
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("STAGE (Camera 1)"),
                                             /*live=*/true));
    receivers.last()->draw(QColor(Qt::blue));
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("STAGE (Camera 2)")));
    REQUIRE(facade.preview()->liveSourceMode());
    REQUIRE(receivers.running() == 2);
    auto* cued = receivers.last();
    cued->draw(QColor(Qt::green));
    // Its picture for the previews alone, at full quality; its sound on the
    // cue strip.
    CHECK(receivers.qualities.back() == seventhslide::app::LiveQuality::Full);
    CHECK(sources->frames().contains(two));
    CHECK_FALSE(sources->output_frames().contains(two));
    CHECK(sources->output_frames().contains(one));
    CHECK(sources->sound_strip(two) == seventhslide::audio::cue_strip_id(stack));
    CHECK(sources->sound_strip(one) == stack);
    CHECK_FALSE(facade.outputPreviews()->live_source_frame(facade.preview()->live_source_key()).isNull());

    // Taken with a fade: the same receiver, now on the outputs and the
    // stack's strip; the camera taken off is cued, still received, and on the
    // outputs until the fade is over.
    REQUIRE(facade.stacks()->transitionWith(main, QStringLiteral("fade"), 400, QString()).isEmpty());
    CHECK(sources->receiver(two) == cued);
    CHECK(receivers.made.size() == 2);
    CHECK(sources->output_frames().contains(two));
    CHECK(sources->sound_strip(two) == stack);
    CHECK(sources->sound_strip(one) == seventhslide::audio::cue_strip_id(stack));
    CHECK(sources->output_frames().contains(one));
    REQUIRE(QTest::qWaitFor([&] { return !sources->output_frames().contains(one); }, 3000));
    CHECK(sources->receiver(one) != nullptr);
    CHECK(sources->frames().contains(one));
}

TEST_CASE("A source live on one stack and cued on another is received once, heard live",
          "[app][live][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(facade.stacks()->setStackPreview(lobby, true).isEmpty());
    auto* sources = facade.live_sources();
    const std::string cam = "ndi:CAM";

    // Cued on Lobby first: heard on Lobby's cue strip.
    facade.stacks()->armSendTo(lobby);
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("CAM")));
    REQUIRE(receivers.running() == 1);
    CHECK(sources->sound_strip(cam) == seventhslide::audio::cue_strip_id(lobby.toStdString()));
    CHECK(sources->output_frames().empty());

    // Then live on Main: one receiver still, its sound now Main's, its
    // picture on the outputs.
    facade.stacks()->armSendTo(QStringLiteral("main"));
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), QStringLiteral("CAM")));
    CHECK(receivers.made.size() == 1);
    CHECK(sources->sound_strip(cam) == "main");
    receivers.last()->draw(QColor(Qt::blue));
    CHECK(sources->output_frames().contains(cam));

    // Main cleared: cued on Lobby alone, heard on its cue strip again.
    REQUIRE(facade.stacks()->clearStack(QStringLiteral("main")).isEmpty());
    CHECK(receivers.running() == 1);
    CHECK(sources->sound_strip(cam) == seventhslide::audio::cue_strip_id(lobby.toStdString()));
}

TEST_CASE("NDI from this machine plays live on the outputs", "[app][live][ndi][receive]")
{
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::render::ndi_receive_available()) {
        SKIP("no NDI runtime that receives");
    }
    seventhslide::render::NdiSink sink;
    REQUIRE(sink.start(QStringLiteral("SeventhSlide LiveTest"), QSize(320, 180)));
    seventhslide::scene::SceneFrame green;
    green.canvas_width = 320;
    green.canvas_height = 180;
    green.background.type = seventhslide::layout::BackgroundType::Color;
    green.background.color = "#00ff00";
    sink.present(seventhslide::scene::share_frame(green));

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    // Found through the Live tab, as the operator would.
    facade.liveLibrary()->setSearching(true);
    QString name;
    (void)seventhslide::test::wait_until([&] {
        for (const auto& v : facade.liveLibrary()->sources()) {
            const auto n = v.toMap().value(QStringLiteral("name")).toString();
            if (n.contains(QStringLiteral("SeventhSlide LiveTest"))) {
                name = n;
            }
        }
        return !name.isEmpty();
    }, 10000);
    facade.liveLibrary()->setSearching(false);
    if (name.isEmpty()) {
        sink.stop();
        SKIP("this machine does not see its own NDI sources (mDNS)");
    }
    REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"), name));
    const auto out_id = QString::fromStdString(facade.session().primary_output()->id);
    QDeadlineTimer showing(10000);
    QColor seen;
    while (true) {
        seen = centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)));
        if (seen.green() > 230 && seen.red() < 40 && seen.blue() < 40) {
            break;
        }
        INFO("preview centre " << seen.name().toStdString());
        REQUIRE_FALSE(showing.hasExpired());
        QTest::qWait(50);
    }
    CHECK(facade.program()->liveSourceProblem().isEmpty());
    sink.stop();
}

TEST_CASE("Capture devices are listed after NDI, and one not plugged in is waited for",
          "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    live->set_finder([] {
        return std::vector<LiveSource>{{"device", "A Webcam"}, {"ndi", "Z (Camera)"}};
    });
    const auto sources = live->sources();
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("ndi"));
    CHECK(sources[0].toMap().value(QStringLiteral("kindLabel")).toString() == QStringLiteral("NDI"));
    CHECK(sources[1].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("device"));
    CHECK(sources[1].toMap().value(QStringLiteral("kindLabel")).toString() == QStringLiteral("Capture"));

    // The app's own receiver, for a device this computer has not got: it
    // waits for it to be plugged in.
    REQUIRE(live->playSource(QStringLiteral("device"), QStringLiteral("No Such Webcam 9000")));
    CHECK(facade.program()->liveSourceMode());
    CHECK(facade.program()->liveSourceProblem().contains(QStringLiteral("plugged in")));
    CHECK(facade.live_sources()->count() == 1);
}

TEST_CASE("A DeckLink input is listed last, and without Desktop Video says so", "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    live->set_finder([] {
        return std::vector<LiveSource>{{"decklink", "Card"}, {"device", "Cam"}, {"ndi", "N (1)"}};
    });
    const auto sources = live->sources();
    REQUIRE(sources.size() == 3);
    CHECK(sources[2].toMap().value(QStringLiteral("kindLabel")).toString() == QStringLiteral("DeckLink"));
    if (seventhslide::render::bmd_runtime_available()) {
        SKIP("this machine has Desktop Video");
    }
    REQUIRE(live->playSource(QStringLiteral("decklink"), QStringLiteral("DeckLink Mini Recorder")));
    CHECK(facade.program()->liveSourceMode());
    CHECK(facade.program()->liveSourceProblem() == seventhslide::render::bmd_missing_message());
}

TEST_CASE("A stream's address is one of the kinds a stream has", "[live]")
{
    using seventhslide::model::stream_url_from_text;
    CHECK(stream_url_from_text("  SRT://stage.local:9000?latency=200  ")
          == "srt://stage.local:9000?latency=200");
    CHECK(stream_url_from_text("rtmp://live.example.com/app/key") == "rtmp://live.example.com/app/key");
    CHECK(stream_url_from_text("rtsp://192.168.1.50:554/stream1") == "rtsp://192.168.1.50:554/stream1");
    CHECK(stream_url_from_text("https://example.com/live/index.m3u8")
          == "https://example.com/live/index.m3u8");
    CHECK(stream_url_from_text("udp://@:1234") == "udp://@:1234");
    CHECK_FALSE(stream_url_from_text("example.com/live"));
    CHECK_FALSE(stream_url_from_text("file:///tmp/a.ts"));
    CHECK_FALSE(stream_url_from_text("srt://"));
    CHECK_FALSE(stream_url_from_text("srt://host 9000"));

    // Remembered by its address; named, or by its address.
    const auto data = seventhslide::model::live_item_data({"stream", "Lobby camera", "rtsp://cam/1"});
    const auto stream = seventhslide::model::live_source_from_item_data(data);
    CHECK(stream.url == "rtsp://cam/1");
    CHECK(stream.name == "Lobby camera");
    CHECK(seventhslide::model::live_source_key(stream) == "stream:rtsp://cam/1");
    CHECK(seventhslide::model::live_source_from_item_data({{"kind", "stream"}, {"name", ""}, {"url", "srt://a:1"}})
              .name
          == "srt://a:1");
    CHECK(seventhslide::model::live_source_from_item_data({{"kind", "stream"}, {"name", "x"}, {"url", "ftp://a"}})
              .empty());

    // Shown without what lets someone else send or watch it; read with it.
    using seventhslide::model::stream_display_name;
    const std::string dots = "\u2022\u2022\u2022";
    CHECK(stream_display_name("rtsp://admin:hunter2@192.168.1.50:554/stream1")
          == "rtsp://admin:" + dots + "@192.168.1.50:554/stream1");
    CHECK(stream_display_name("rtsp://viewer@cam.local/live") == "rtsp://viewer@cam.local/live");
    CHECK(stream_display_name("srt://stage.local:9000?latency=200&passphrase=s3cret-phrase")
          == "srt://stage.local:9000?latency=200&passphrase=" + dots);
    CHECK(stream_display_name("rtmp://live.example.com/app/abcd-1234-efgh")
          == "rtmp://live.example.com/app/" + dots);
    CHECK(stream_display_name("rtmp://live.example.com/app") == "rtmp://live.example.com/app");
    CHECK(stream_display_name("https://cdn.example.com/live/index.m3u8?Token=xyz&lang=en#t=5")
          == "https://cdn.example.com/live/index.m3u8?Token=" + dots + "&lang=en");
    CHECK(stream_display_name("udp://@:1234") == "udp://@:1234");
    const auto unnamed = seventhslide::model::live_source_from_item_data(
        {{"kind", "stream"}, {"name", "rtsp://u:p@cam/1"}, {"url", "rtsp://u:p@cam/1"}});
    CHECK(unnamed.name == "rtsp://u:" + dots + "@cam/1");
    CHECK(unnamed.url == "rtsp://u:p@cam/1");
}

TEST_CASE("Streams are added by hand, kept with the library, edited and forgotten",
          "[app][live]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Streams")) > 0);
    FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    auto* live = facade.liveLibrary();
    live->set_finder([] { return std::vector<LiveSource>{{"ndi", "STAGE (Camera 1)"}}; });

    CHECK(live->streamUrlFromText(QStringLiteral(" srt://a:1 ")) == QStringLiteral("srt://a:1"));
    CHECK(live->streamUrlFromText(QStringLiteral("nope")).isEmpty());
    CHECK(live->saveStream({}, QStringLiteral("x"), QStringLiteral("nope")).contains(QStringLiteral("error")));
    const auto saved = live->saveStream({}, QStringLiteral("Lobby camera"),
                                        QStringLiteral("rtsp://192.168.1.50/stream1"));
    REQUIRE(saved.value(QStringLiteral("key")).toString()
            == QStringLiteral("stream:rtsp://192.168.1.50/stream1"));
    // The same address twice is refused.
    CHECK(live->saveStream({}, QStringLiteral("Again"), QStringLiteral("rtsp://192.168.1.50/stream1"))
              .contains(QStringLiteral("error")));

    // Listed after the sources found, as a stream; nothing known of it yet.
    const auto sources = live->sources();
    REQUIRE(sources.size() == 2);
    const auto row = sources[1].toMap();
    CHECK(row.value(QStringLiteral("kind")).toString() == QStringLiteral("stream"));
    CHECK(row.value(QStringLiteral("name")).toString() == QStringLiteral("Lobby camera"));
    CHECK(row.value(QStringLiteral("url")).toString() == QStringLiteral("rtsp://192.168.1.50/stream1"));
    CHECK(row.value(QStringLiteral("detail")).toString().isEmpty());
    CHECK(row.value(QStringLiteral("signal")).toString().isEmpty());
    // Kept in the library's settings, so it syncs with it.
    const auto stored = facade.open_library().config()->get_setting("live_streams");
    REQUIRE(stored.has_value());
    CHECK(stored->size() == 1);

    // Played by its address, under its name; into the service with it.
    REQUIRE(live->playSource(QStringLiteral("stream"), QStringLiteral("rtsp://192.168.1.50/stream1")));
    CHECK(facade.session().focused_stack().program.scene_extras().live.value().name == "Lobby camera");
    REQUIRE(receivers.last() != nullptr);
    CHECK(receivers.last()->source_.url == "rtsp://192.168.1.50/stream1");
    REQUIRE(live->addSourceToService(QStringLiteral("stream"), QStringLiteral("rtsp://192.168.1.50/stream1")));
    CHECK(rundown_titles(facade).last() == QStringLiteral("Lobby camera"));

    // Renamed and moved; then forgotten (the service keeps its own copy).
    REQUIRE(live->saveStream(QStringLiteral("rtsp://192.168.1.50/stream1"), QStringLiteral("Foyer"),
                             QStringLiteral("rtsp://192.168.1.51/stream1"))
                .contains(QStringLiteral("key")));
    CHECK(live->sources()[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Foyer"));
    CHECK(live->deleteStream(QStringLiteral("rtsp://192.168.1.51/stream1")).isEmpty());
    CHECK_FALSE(live->deleteStream(QStringLiteral("rtsp://192.168.1.51/stream1")).isEmpty());
    CHECK(live->sources().size() == 1);
    CHECK(rundown_titles(facade).last() == QStringLiteral("Lobby camera"));

    // Saved with no name: named by its address, without its password, in
    // the list and the service.
    const QString secret = QStringLiteral("rtsp://admin:hunter2@192.168.1.60/main");
    REQUIRE(live->saveStream({}, {}, secret).contains(QStringLiteral("key")));
    REQUIRE(live->addSourceToService(QStringLiteral("stream"), secret));
    CHECK_FALSE(rundown_titles(facade).last().contains(QStringLiteral("hunter2")));
    for (const auto& v : live->sources()) {
        CHECK_FALSE(v.toMap().value(QStringLiteral("name")).toString().contains(QStringLiteral("hunter2")));
    }
}

TEST_CASE("A stream on this machine plays live on the outputs", "[app][live][ffmpeg][stream]")
{
    seventhslide::test::ensure_qt_application();
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        SKIP("no ffmpeg to send a stream");
    }
    // FFmpeg sends a test picture and tone as MPEG-TS over UDP, here.
    QProcess sender;
    sender.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-re"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                          QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x180:rate=25"),
                          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("sine=frequency=1000:sample_rate=48000"),
                          QStringLiteral("-c:v"), QStringLiteral("mpeg2video"), QStringLiteral("-c:a"),
                          QStringLiteral("mp2"), QStringLiteral("-f"), QStringLiteral("mpegts"),
                          QStringLiteral("udp://127.0.0.1:23999?pkt_size=1316")});
    REQUIRE(sender.waitForStarted(5000));

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* live = facade.liveLibrary();
    REQUIRE(live->saveStream({}, QStringLiteral("Here"), QStringLiteral("udp://127.0.0.1:23999"))
                .contains(QStringLiteral("key")));
    REQUIRE(live->playSource(QStringLiteral("stream"), QStringLiteral("udp://127.0.0.1:23999")));
    QDeadlineTimer deadline(20000);
    while (facade.live_sources()->frames().empty()) {
        INFO("problem: " << facade.program()->liveSourceProblem().toStdString());
        REQUIRE_FALSE(deadline.hasExpired());
        QTest::qWait(50);
    }
    CHECK(facade.live_sources()->frames().begin()->second.image.size() == QSize(320, 180));
    CHECK(facade.program()->liveSourceProblem().isEmpty());
    const auto out_id = QString::fromStdString(facade.session().primary_output()->id);
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          != QColor(Qt::black));
    const auto* receiver = facade.live_sources()->receiver("stream:udp://127.0.0.1:23999");
    REQUIRE(receiver != nullptr);
    CHECK_FALSE(receiver->lost());
    CHECK_FALSE(facade.program()->liveSourceLost());
    // The stream's own row: the list also has whatever this computer can
    // capture (a webcam), and that can come first.
    const auto signal = [&] {
        const auto sources = live->sources();
        for (const auto& source : sources) {
            const auto row = source.toMap();
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("stream:udp://127.0.0.1:23999")) {
                return row.value(QStringLiteral("signal")).toString();
            }
        }
        QStringList keys;
        for (const auto& source : sources) {
            keys << source.toMap().value(QStringLiteral("key")).toString();
        }
        UNSCOPED_INFO("no row for the stream among: " << keys.join(QStringLiteral(", ")).toStdString());
        return QString();
    };
    CHECK(signal() == QStringLiteral("ok"));

    // It stops sending: lost, its last picture kept on the outputs.
    QSignalSpy state(facade.live_sources(), &seventhslide::app::LiveSources::stateChanged);
    sender.kill();
    sender.waitForFinished(3000);
    deadline.setRemainingTime(seventhslide::app::k_live_lost_ms + 3000);
    while (!receiver->lost()) {
        REQUIRE_FALSE(deadline.hasExpired());
        QTest::qWait(50);
    }
    CHECK(state.count() >= 1);
    CHECK_FALSE(receiver->frame().isNull());
    CHECK(facade.program()->liveSourceProblem().isEmpty());
    // The Controller's rim and the Live tab's dot say so.
    CHECK(facade.program()->liveSourceLost());
    CHECK(signal() == QStringLiteral("lost"));
    CHECK(centre(facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90)))
          != QColor(Qt::black));
}

