// The output window's view (PresenterView): it sleeps while nothing moves on
// its presenter, wakes for each change, and after each comes to show what the
// presenter composites (its own surface, at the view's size).

#include <catch2/catch_test_macros.hpp>

#include "pixel_compare.hpp"
#include "scene_object_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_wait.hpp"
#include "view_wake_count.hpp"

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/render/presenter_view.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/session/session.hpp>

#include <QCoreApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>

#include <memory>
#include <string>
#include <utility>

using seventhslide::render::PresenterView;
using seventhslide::render::ScenePresenter;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::test::wait_until;

namespace {

constexpr int k_view_w = 320;
constexpr int k_view_h = 180;

/// A canvas the view's shape, in ``background``, with a square of ``ink`` in
/// its middle.
[[nodiscard]] SceneFrame still(const std::string& background, const std::string& ink = "#ffffff")
{
    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.transition_key = "song";
    frame.canvas_width = k_view_w;
    frame.canvas_height = k_view_h;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = background;
    seventhslide::scene::SceneObject square;
    seventhslide::canvas::Object shape;
    shape.type = seventhslide::canvas::ObjectType::Shape;
    shape.x = 120;
    shape.y = 50;
    shape.w = 80;
    shape.h = 80;
    shape.fill.kind = seventhslide::canvas::FillKind::Solid;
    shape.fill.color = ink;
    square.object = shape;
    frame.objects.push_back(std::move(square));
    return frame;
}

/// A view on its presenter, in a window of its own.
struct Shown {
    ScenePresenter presenter;
    QQuickWindow window;
    PresenterView* view = nullptr;
    QColor letterbox{Qt::black};

    Shown()
    {
        window.resize(k_view_w, k_view_h);
        window.setColor(Qt::magenta);  // never shown, unless the view misses its corner
        view = new PresenterView(window.contentItem());
        view->setSize(QSizeF(k_view_w, k_view_h));
        view->set_presenter(&presenter);
        window.show();
    }
    ~Shown() { view->set_presenter(nullptr); }
    Shown(const Shown&) = delete;
    Shown& operator=(const Shown&) = delete;

    void resize(int w, int h)
    {
        window.resize(w, h);
        view->setSize(QSizeF(w, h));
    }

    void set_letterbox(const QColor& color)
    {
        letterbox = color;
        view->set_letterbox(color);
    }

    [[nodiscard]] static QImage shown(QQuickWindow& w)
    {
        return w.grabWindow().convertToFormat(QImage::Format_RGB32);
    }

    /// What the presenter shows now, at the view's size.
    [[nodiscard]] QImage composite()
    {
        return presenter.surface(view->pixel_size(), letterbox).convertToFormat(QImage::Format_RGB32);
    }

    /// ``w`` comes to show what the presenter does now.
    [[nodiscard]] bool shows_it(QQuickWindow& w)
    {
        return wait_until([&] { return seventhslide::test::mean_abs_diff(shown(w), composite()) < 0.5; });
    }
    [[nodiscard]] bool shows_it() { return shows_it(window); }

    [[nodiscard]] QTimer* timer() const { return view->findChild<QTimer*>(); }
};

/// The centre's colour in ``window``.
[[nodiscard]] QRgb centre(QQuickWindow& window)
{
    const QImage shown = Shown::shown(window);
    return shown.pixel(shown.width() / 2, shown.height() / 2);
}

}  // namespace

TEST_CASE("An output view with nothing moving stops waking, and wakes for a change",
          "[render][window][view]")
{
    seventhslide::test::ensure_qt_application();
    Shown out;
    out.presenter.present(seventhslide::scene::share_frame(still("#2040a0")));
    REQUIRE(out.shows_it());

    QTimer* timer = out.timer();
    REQUIRE(timer != nullptr);
    REQUIRE(wait_until([&] { return !timer->isActive(); }));
    {
        const seventhslide::test::WakeCount wakes({out.view});
        seventhslide::test::run_events_for(150);  // over four of the old ticks
        CHECK(wakes.wakes == 0);
    }

    // A change wakes it at the first pass of the window's thread: no tick
    // there, as nothing ticks.
    out.presenter.present(seventhslide::scene::share_frame(still("#a02020")));
    QCoreApplication::sendPostedEvents(out.view);
    CHECK(timer->isActive());
    REQUIRE(out.shows_it());
    CHECK(qRed(centre(out.window)) > 200);  // the square, white
    REQUIRE(wait_until([&] { return !timer->isActive(); }));
}

TEST_CASE("An output view shows each change as its presenter composites it", "[render][window][view]")
{
    seventhslide::test::ensure_qt_application();
    Shown out;
    out.presenter.present(seventhslide::scene::share_frame(still("#2040a0")));
    REQUIRE(out.shows_it());
    QTimer* timer = out.timer();
    REQUIRE(wait_until([&] { return !timer->isActive(); }));

    SECTION("a new picture")
    {
        out.presenter.present(seventhslide::scene::share_frame(still("#20a040", "#000000")));
        REQUIRE(out.shows_it());
    }

    SECTION("a theme change: another look, a canvas of another shape, and its bars")
    {
        SceneFrame square = still("#c08000", "#4000ff");
        square.canvas_width = 200;
        square.canvas_height = 200;
        out.presenter.present(seventhslide::scene::share_frame(square));
        out.set_letterbox(QColor(0xc0, 0x80, 0x00));
        REQUIRE(out.shows_it());
    }

    SECTION("a fade, step by step")
    {
        SceneFrame next = still("#a02020", "#000000");
        next.background_transition = seventhslide::model::transition_from_fade(true, 400);
        next.content_transition = seventhslide::model::transition_from_fade(true, 400);
        out.presenter.present(seventhslide::scene::share_frame(next));
        REQUIRE(out.presenter.is_fading());
        for (const double t : {0.25, 0.5, 0.75, 1.0}) {
            out.presenter.set_fade_progress_for_test(t);
            INFO("at " << t);
            REQUIRE(out.shows_it());
        }
    }

    SECTION("a fade on its own clock, which only the view moves")
    {
        SceneFrame next = still("#a02020", "#a02020");
        next.background_transition = seventhslide::model::transition_from_fade(true, 1000);
        next.content_transition = seventhslide::model::transition_from_fade(true, 1000);
        out.presenter.present(seventhslide::scene::share_frame(next));
        REQUIRE(out.presenter.is_fading());
        // Part way: some red and some blue, from blue to red.
        const bool between = wait_until([&] {
            const QRgb at = Shown::shown(out.window).pixel(10, 10);
            return qRed(at) > 0x30 && qRed(at) < 0x90 && qBlue(at) > 0x30 && qBlue(at) < 0x90;
        });
        CHECK(between);
        REQUIRE(wait_until([&] { return !out.presenter.is_fading(); }));
        REQUIRE(out.shows_it());
        CHECK(seventhslide::test::nearly_rgb(centre(out.window), 0xa0, 0x20, 0x20));
        REQUIRE(wait_until([&] { return !timer->isActive(); }));
    }

    SECTION("a clock, re-presented every second as the session does")
    {
        SceneFrame clocked = still("#000000");
        seventhslide::scene::SceneObject clock;
        clock.object = seventhslide::test::scene_clock();
        clock.object->w = k_view_w;
        clock.object->h = k_view_h;
        clock.object->font_size = 60;
        clock.object->font_color = "#ffffff";
        clock.object->clock_seconds = true;
        clocked.objects.push_back(std::move(clock));
        const auto shared = seventhslide::scene::share_frame(clocked);
        out.presenter.present(shared);
        REQUIRE(out.shows_it());
        const QImage first = Shown::shown(out.window);
        const auto inked = [](const QImage& img) {
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (qGray(img.pixel(x, y)) > 128) {
                        return true;
                    }
                }
            }
            return false;
        };
        CHECK(inked(first));  // the time, in white
        // The next second's text, from presenting the same frame.
        REQUIRE(wait_until([&] {
            out.presenter.present(shared);
            return Shown::shown(out.window) != first;
        }));
        REQUIRE(wait_until([&] {
            out.presenter.present(shared);
            return Shown::shown(out.window) == out.composite();
        }));
    }

    SECTION("a video's pictures")
    {
        SceneFrame video = still("#000000");
        video.mode = SceneMode::Video;
        video.objects.clear();
        video.video = seventhslide::scene::SceneVideoMedia{.source = "content 1",
                                                           .url = "/static/videos/clip.mp4"};
        out.presenter.present(seventhslide::scene::share_frame(video));
        for (const QColor colour : {QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue)}) {
            QImage picture(k_view_w, k_view_h, QImage::Format_ARGB32_Premultiplied);
            picture.fill(colour);
            out.presenter.set_source_frames({{"content 1", {picture, {}}}});
            INFO("picture " << colour.name().toStdString());
            REQUIRE(out.shows_it());
            CHECK(seventhslide::test::nearly_rgb(centre(out.window), colour.red(), colour.green(),
                                                 colour.blue()));
        }
    }

    SECTION("a background video's pictures")
    {
        SceneFrame moving = still("#000000");
        moving.background.type = seventhslide::layout::BackgroundType::Video;
        moving.background.video = "/static/videos/motion.mp4";
        out.presenter.present(seventhslide::scene::share_frame(moving));
        for (const QColor colour : {QColor(Qt::yellow), QColor(Qt::cyan)}) {
            QImage picture(k_view_w, k_view_h, QImage::Format_RGB32);
            picture.fill(colour);
            out.presenter.set_background_video_frame(picture);
            REQUIRE(out.shows_it());
            const QRgb corner = Shown::shown(out.window).pixel(10, 10);
            CHECK(seventhslide::test::nearly_rgb(corner, colour.red(), colour.green(), colour.blue()));
        }
    }

    SECTION("a resize")
    {
        out.resize(200, 150);
        REQUIRE(out.shows_it());
        CHECK(Shown::shown(out.window).size() == QSize(200, 150));
    }
}

TEST_CASE("An output view plays a slide's builds", "[render][window][view][builds]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    session.set_outputs({main});
    // A red box that fades in on a click, over a second after 100 ms: long
    // enough to be seen part way on a slow renderer.
    auto doc = seventhslide::freeform::make_document("Builds", {"out-main"});
    seventhslide::freeform::Object box;
    box.id = "box";
    box.type = seventhslide::canvas::ObjectType::Shape;
    box.x = 0;
    box.y = 0;
    box.w = 1920;
    box.h = 1080;
    box.fill.kind = seventhslide::canvas::FillKind::Solid;
    box.fill.color = "#ff0000";
    seventhslide::canvas::Animation fade;
    fade.order = 1;
    fade.effect = seventhslide::canvas::AnimationEffect::Fade;
    fade.trigger = seventhslide::canvas::AnimationTrigger::OnClick;
    fade.duration_ms = 1000;
    fade.delay_ms = 100;
    box.animations = {fade};
    doc.decks[0].slides[0].objects = {box};
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(std::move(live));

    Shown out;
    out.presenter.present(seventhslide::scene::share_frame(session.scene_frames().front()));
    REQUIRE(out.shows_it());
    CHECK(qRed(centre(out.window)) < 30);
    REQUIRE(session.focused_stack().program.next_slide());
    out.presenter.present(seventhslide::scene::share_frame(session.scene_frames().front()));
    REQUIRE(out.presenter.is_fading());

    SECTION("step by step")
    {
        for (const double t : {0.1, 0.6, 1.0}) {
            out.presenter.set_fade_progress_for_test(t);
            INFO("at " << t);
            REQUIRE(out.shows_it());
        }
        CHECK(qRed(centre(out.window)) > 230);
    }

    SECTION("on its own clock, which only the view moves")
    {
        const bool part_way = wait_until([&] {
            const int red = qRed(centre(out.window));
            return red > 40 && red < 220;
        });
        CHECK(part_way);
        REQUIRE(wait_until([&] { return !out.presenter.is_fading(); }));
        REQUIRE(out.shows_it());
        CHECK(qRed(centre(out.window)) > 230);
        REQUIRE(wait_until([&] { return !out.timer()->isActive(); }));
    }
}

TEST_CASE("An output view moved to another window shows its picture there", "[render][window][view]")
{
    seventhslide::test::ensure_qt_application();
    Shown out;
    out.presenter.present(seventhslide::scene::share_frame(still("#2040a0")));
    REQUIRE(out.shows_it());
    QTimer* timer = out.timer();
    REQUIRE(wait_until([&] { return !timer->isActive(); }));

    // Its nodes stay with the window it leaves: in the other it asks for its
    // picture again, though the presentation has not changed.
    QQuickWindow other;
    other.resize(k_view_w, k_view_h);
    other.setColor(Qt::magenta);
    other.show();
    out.view->setParentItem(other.contentItem());
    REQUIRE(out.shows_it(other));
    CHECK(qRed(centre(other)) > 200);  // the square, white
    REQUIRE(wait_until([&] { return !timer->isActive(); }));

    out.view->setParentItem(out.window.contentItem());
    REQUIRE(out.shows_it());
    CHECK(qRed(centre(out.window)) > 200);
}

TEST_CASE("An output view lets go of a presenter that has gone", "[render][window][view]")
{
    seventhslide::test::ensure_qt_application();
    QQuickWindow window;
    window.resize(k_view_w, k_view_h);
    auto* view = new PresenterView(window.contentItem());
    view->setSize(QSizeF(k_view_w, k_view_h));
    view->set_letterbox(QColor(0x00, 0xa0, 0x00));
    window.show();
    QTimer* timer = view->findChild<QTimer*>();
    REQUIRE(timer != nullptr);
    {
        ScenePresenter other;
        auto gone = std::make_unique<ScenePresenter>();
        view->set_presenter(gone.get());
        view->set_presenter(&other);
        view->set_presenter(gone.get());
        gone->present(seventhslide::scene::share_frame(still("#2040a0")));
        REQUIRE(wait_until([&] { return qBlue(Shown::shown(window).pixel(10, 10)) > 0x80; }));
        REQUIRE(wait_until([&] { return !timer->isActive(); }));

        // Asleep, it looks at the presenter only when woken, so once the
        // presenter has gone it is never woken and asks nothing of it (which
        // ASan would catch).
        gone.reset();
        {
            const seventhslide::test::WakeCount wakes({view});
            seventhslide::test::run_events_for(150);
            CHECK(wakes.wakes == 0);
        }
        CHECK_FALSE(timer->isActive());
        // The previews' items let go once the presenter has gone (its owner's
        // destroyed signal).
        view->set_presenter(nullptr);
    }
    CHECK(view->presenter() == nullptr);
    // With none, the letterbox, and asleep.
    REQUIRE(wait_until([&] { return seventhslide::test::nearly_rgb(centre(window), 0x00, 0xa0, 0x00); }));
    REQUIRE(wait_until([&] { return !timer->isActive(); }));
}
