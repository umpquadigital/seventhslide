// What an output window's view (PresenterView) costs. Hidden from the default
// run:
//   ./seventhslide_tests_dev "bench: presenter view*"
// Prints what it measures; asserts nothing about speed.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_wait.hpp"
#include "view_wake_count.hpp"

#include <seventhslide/render/presenter_view.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

// QRhi is in Qt's GuiPrivate, which the texture bench needs.
#if SEVENTHSLIDE_RENDER_RHI
#include <QOffscreenSurface>
#include <rhi/qrhi.h>
#endif

using seventhslide::render::PresenterView;
using seventhslide::render::ScenePresenter;

namespace {

[[nodiscard]] seventhslide::scene::SharedSceneFrame still_frame()
{
    seventhslide::scene::SceneFrame frame;
    frame.mode = seventhslide::scene::SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#204080";
    return seventhslide::scene::share_frame(std::move(frame));
}

}  // namespace

TEST_CASE("bench: presenter view while nothing moves", "[.bench]")
{
    // Windows showing a still picture: how often each view is woken, and what
    // a wake costs the window's thread (its timer's event and the look at the
    // presenter that finds nothing new; the system's own wake is not in it).
    seventhslide::test::ensure_qt_application();
    std::printf("\npresenter view while nothing moves (2 s, a still picture)\n");
    for (const int count : {1, 4, 16}) {
        ScenePresenter presenter;
        presenter.present(still_frame());
        std::vector<std::unique_ptr<QQuickWindow>> windows;
        std::vector<PresenterView*> views;
        for (int i = 0; i < count; ++i) {
            auto window = std::make_unique<QQuickWindow>();
            window->resize(480, 270);
            auto* view = new PresenterView(window->contentItem());
            view->setSize(QSizeF(480, 270));
            view->set_presenter(&presenter);
            window->show();
            views.push_back(view);
            windows.push_back(std::move(window));
        }
        for (auto& window : windows) {
            REQUIRE(seventhslide::test::wait_until([&] {
                const QImage shown = window->grabWindow();
                return !shown.isNull() && qBlue(shown.pixel(10, 10)) > 100;
            }));
        }
        {
            const seventhslide::test::WakeCount wakes({views.begin(), views.end()});
            const auto start = std::chrono::steady_clock::now();
            seventhslide::test::run_events_for(2000);
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("  %2d window(s): %5.1f wakes a second each\n", count,
                        wakes.wakes / seconds / count);
        }

        auto* timer = views.front()->findChild<QTimer*>();
        REQUIRE(timer != nullptr);
        if (timer->isActive()) {
            constexpr int k_wakes = 20000;
            QTimerEvent wake(timer->timerId());
            const auto timing = std::chrono::steady_clock::now();
            for (int i = 0; i < k_wakes; ++i) {
                QCoreApplication::sendEvent(timer, &wake);
            }
            const double us = std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - timing)
                                  .count()
                              / k_wakes;
            std::printf("  %2d window(s): %6.3f us a wake\n", count, us);
        }
        for (auto* view : views) {
            view->set_presenter(nullptr);
        }
    }
}

TEST_CASE("bench: presenter view new pictures", "[.bench]")
{
    // A 1080p window showing a video: what each new picture costs its render
    // thread, from synchronising the scene to the end of its rendering, and
    // to the end of the swap (where a driver may do the upload). Through whichever renderer the platform gives: under
    // Xvfb (SEVENTHSLIDE_TEST_WEBENGINE=1 QT_QPA_PLATFORM=xcb xvfb-run -a),
    // OpenGL; offscreen, the software one.
    seventhslide::test::ensure_qt_application();
    ScenePresenter presenter;
    seventhslide::scene::SceneFrame frame;
    frame.mode = seventhslide::scene::SceneMode::Video;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    frame.video = seventhslide::scene::SceneVideoMedia{.source = "content 1",
                                                       .url = "/static/videos/clip.mp4"};
    presenter.present(seventhslide::scene::share_frame(std::move(frame)));
    std::vector<QImage> pictures;
    for (int i = 0; i < 8; ++i) {
        QImage picture(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        picture.fill(QColor::fromHsv(i * 40, 160, 200));
        pictures.push_back(picture);
    }

    QQuickWindow window;
    window.resize(1920, 1080);
    auto* view = new PresenterView(window.contentItem());
    view->setSize(QSizeF(1920, 1080));
    view->set_presenter(&presenter);
    std::atomic<int> frames{0};
    std::atomic<qint64> render_ns{0};
    std::atomic<qint64> swap_ns{0};
    QElapsedTimer started;  // the render thread's alone
    QObject::connect(&window, &QQuickWindow::beforeSynchronizing, &window,
                     [&] { started.start(); }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::afterRendering, &window, [&] {
        render_ns += started.nsecsElapsed();
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        swap_ns += started.nsecsElapsed();
        ++frames;
    }, Qt::DirectConnection);
    window.show();
    REQUIRE(seventhslide::test::wait_until([&] { return frames.load() > 0; }));

    std::size_t next = 0;
    const auto show_next = [&] {
        const int before = frames.load();
        presenter.set_source_frames({{"content 1", {pictures[++next % pictures.size()], {}}}});
        REQUIRE(seventhslide::test::wait_until([&] { return frames.load() > before; }));
    };
    for (int i = 0; i < 10; ++i) {
        show_next();  // warm: the first texture, the shaders, the compositor's buffers
    }
    constexpr int k_pictures = 120;
    const int frames_before = frames.load();
    render_ns = 0;
    swap_ns = 0;
    for (int i = 0; i < k_pictures; ++i) {
        show_next();
    }
    const int rendered = frames.load() - frames_before;
    std::printf("\npresenter view new pictures (1080p, %s)\n",
                window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software
                    ? "software"
                    : "graphics API");
    std::printf("  %d frames for %d pictures, a frame's render thread: %8.3f ms to rendered, "
                "%8.3f ms to swapped\n",
                rendered, k_pictures,
                static_cast<double>(render_ns.load()) / 1e6 / std::max(1, rendered),
                static_cast<double>(swap_ns.load()) / 1e6 / std::max(1, rendered));
    view->set_presenter(nullptr);
}

#if SEVENTHSLIDE_RENDER_RHI
TEST_CASE("bench: presenter view texture per picture", "[.bench]")
{
    // What the view's texture costs the render thread for each new picture,
    // as Qt's image texture does it (a new texture each time, the old one
    // released) and kept (the picture uploaded into the same one). Through
    // OpenGL where there is one (under Xvfb, llvmpipe), and Qt's Null
    // backend, which counts Qt's own work alone. Each frame waits for the
    // graphics to finish, so the upload is in it.
    seventhslide::test::ensure_qt_application();
    std::printf("\npresenter view texture per picture\n");
    std::unique_ptr<QOffscreenSurface> surface(QRhiGles2InitParams::newFallbackSurface());
    for (const QRhi::Implementation backend : {QRhi::OpenGLES2, QRhi::Null}) {
        std::unique_ptr<QRhi> rhi;
        if (backend == QRhi::OpenGLES2) {
            QRhiGles2InitParams params;
            params.fallbackSurface = surface.get();
            rhi.reset(QRhi::create(backend, &params));
        } else {
            QRhiNullInitParams params;
            rhi.reset(QRhi::create(backend, &params));
        }
        if (!rhi) {
            std::printf("  %s: none here\n", backend == QRhi::Null ? "Null" : "OpenGL");
            continue;
        }
        std::printf("  %s (%s)\n", rhi->backendName(), rhi->driverInfo().deviceName.constData());
        for (const QSize size : {QSize(1920, 1080), QSize(320, 180)}) {
            std::vector<QImage> pictures;
            for (int i = 0; i < 8; ++i) {
                QImage picture(size, QImage::Format_ARGB32_Premultiplied);
                picture.fill(QColor::fromHsv(i * 40, 160, 200));
                pictures.push_back(picture);
            }
            // As QSGPlainTexture picks: BGRA where it can, as it is.
            const bool bgra = rhi->isTextureFormatSupported(QRhiTexture::BGRA8);
            const QRhiTexture::Format format = bgra ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8;
            const auto upload = [&](QRhiTexture* texture, const QImage& picture) {
                QRhiCommandBuffer* cb = nullptr;
                REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
                QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
                batch->uploadTexture(texture, bgra ? picture
                                                   : picture.convertToFormat(
                                                         QImage::Format_RGBA8888_Premultiplied));
                cb->resourceUpdate(batch);
                rhi->endOffscreenFrame();
                rhi->finish();
            };
            constexpr int k_pictures = 120;
            std::size_t next = 0;
            // Kept first, then a new one each time: neither is helped by
            // coming second.
            QRhiTexture* shown = rhi->newTexture(format, size);
            REQUIRE(shown->create());
            const auto kept = std::chrono::steady_clock::now();
            for (int i = 0; i < k_pictures; ++i) {
                upload(shown, pictures[++next % pictures.size()]);
            }
            const double kept_ms = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - kept)
                                       .count()
                                   / k_pictures;
            const auto each = std::chrono::steady_clock::now();
            for (int i = 0; i < k_pictures; ++i) {
                QRhiTexture* texture = rhi->newTexture(format, size);
                REQUIRE(texture->create());
                upload(texture, pictures[++next % pictures.size()]);
                shown->deleteLater();
                shown = texture;
            }
            const double each_ms = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - each)
                                       .count()
                                   / k_pictures;
            shown->deleteLater();
            std::printf("    %4dx%-4d a new texture each picture %8.3f ms, one kept %8.3f ms\n",
                        size.width(), size.height(), each_ms, kept_ms);
        }
    }
}
#endif
