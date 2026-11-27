// BackgroundImageCache under concurrent use: an output's GUI thread
// prefetches while its render thread paints, so a decode must not hold the
// cache against every other lookup.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QImage>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using seventhslide::render::BackgroundImageCache;
using namespace std::chrono_literals;

namespace {

/// Holds every decode of ``slow.png`` until ``release()``; any other file
/// decodes at once. Counts the decodes of each.
class GatedDecoder {
public:
    QImage decode(const std::filesystem::path& path, int /*edge*/)
    {
        if (path.filename() == "slow.png") {
            ++slow_decodes;
            entered_.set_value_once();
            released_.wait();
        } else {
            ++other_decodes;
        }
        QImage img(4, 4, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(Qt::red));
        return img;
    }

    /// Blocks until a decode of slow.png has started (false after 2 s).
    bool wait_entered() { return entered_.future.wait_for(2s) == std::future_status::ready; }
    void release() { release_.set_value_once(); }

    std::atomic<int> slow_decodes = 0;
    std::atomic<int> other_decodes = 0;

private:
    struct Once {
        std::promise<void> promise;
        std::shared_future<void> future = promise.get_future().share();
        std::once_flag flag;
        void set_value_once()
        {
            std::call_once(flag, [this] { promise.set_value(); });
        }
    };
    Once entered_;
    Once release_;
    std::shared_future<void> released_ = release_.future;
};

struct CacheFixture {
    seventhslide::test::TempDir root;
    GatedDecoder gate;
    BackgroundImageCache cache{[this](const std::filesystem::path& p, int edge) {
        return gate.decode(p, edge);
    }};

    CacheFixture()
    {
        seventhslide::test::ensure_qt_application();
        std::filesystem::create_directories(root.path / "images");
        for (const char* name : {"slow.png", "fast.png", "other.png"}) {
            std::ofstream(root.path / "images" / name) << "x";
        }
        cache.set_static_root(root.path);
    }
};

constexpr const char* k_slow = "/static/images/slow.png";
constexpr const char* k_fast = "/static/images/fast.png";
constexpr const char* k_other = "/static/images/other.png";

}  // namespace

TEST_CASE("Background image cache serves other images while one decodes", "[render][image-cache]")
{
    CacheFixture f;
    REQUIRE(!f.cache.get(k_fast).isNull());

    auto slow = std::async(std::launch::async, [&] { return f.cache.get(k_slow); });
    REQUIRE(f.gate.wait_entered());

    // An image already cached, and one that is not, both come back while
    // slow.png is still decoding.
    auto hit = std::async(std::launch::async, [&] { return f.cache.get(k_fast); });
    auto miss = std::async(std::launch::async, [&] { return f.cache.get(k_other); });
    const bool hit_ready = hit.wait_for(2s) == std::future_status::ready;
    const bool miss_ready = miss.wait_for(2s) == std::future_status::ready;

    f.gate.release();
    CHECK(hit_ready);
    CHECK(miss_ready);
    CHECK(!hit.get().isNull());
    CHECK(!miss.get().isNull());
    CHECK(!slow.get().isNull());
}

TEST_CASE("Background image cache decodes an image once for concurrent requests", "[render][image-cache]")
{
    CacheFixture f;
    auto first = std::async(std::launch::async, [&] { return f.cache.get(k_slow); });
    REQUIRE(f.gate.wait_entered());
    auto second = std::async(std::launch::async, [&] { return f.cache.get(k_slow); });
    // Give the second request time to find the decode in flight.
    std::this_thread::sleep_for(50ms);
    f.gate.release();

    CHECK(!first.get().isNull());
    CHECK(!second.get().isNull());
    CHECK(f.gate.slow_decodes == 1);
    CHECK(f.cache.ensure(k_slow));
    CHECK(f.gate.slow_decodes == 1);
}

TEST_CASE("Background image cache does not keep a decode that outlived a clear", "[render][image-cache]")
{
    CacheFixture f;
    auto decode = std::async(std::launch::async, [&] { return f.cache.get(k_slow); });
    REQUIRE(f.gate.wait_entered());

    // The library changed under the decode: clearing does not wait for it.
    auto clear = std::async(std::launch::async, [&] { f.cache.clear(); });
    const bool clear_ready = clear.wait_for(2s) == std::future_status::ready;
    f.gate.release();
    CHECK(clear_ready);
    CHECK(!decode.get().isNull());

    // The caller got its image, but the cache did not keep it.
    CHECK(!f.cache.get(k_slow).isNull());
    CHECK(f.gate.slow_decodes == 2);
}

TEST_CASE("Background image cache remembers a missing file", "[render][image-cache]")
{
    CacheFixture f;
    CHECK(f.cache.get("/static/images/absent.png").isNull());
    CHECK(!f.cache.ensure("/static/images/absent.png"));
    CHECK(f.gate.other_decodes == 0);
}

TEST_CASE("Background image cache looks again for a missing file once one has arrived",
          "[render][image-cache]")
{
    CacheFixture f;
    constexpr const char* late = "/static/images/late.png";
    CHECK(f.cache.get(late).isNull());
    CHECK(f.cache.misses() == 1);

    // Remembered missing until told: not looked for on every paint.
    std::ofstream(f.root.path / "images" / "late.png") << "x";
    CHECK(f.cache.get(late).isNull());
    CHECK(f.cache.misses() == 2);

    CHECK(!f.cache.get(k_fast).isNull());
    // Another picture arriving: nothing asked for it, and this one is still
    // taken for missing.
    CHECK_FALSE(f.cache.forget_missing({"/static/images/other.png"}));
    CHECK(f.cache.get(late).isNull());
    CHECK(f.cache.misses() == 3);
    CHECK(f.cache.forget_missing({"/static/images/other.png", late}));
    CHECK(!f.cache.get(late).isNull());
    CHECK(f.cache.misses() == 3);
    // What was here is kept.
    CHECK(!f.cache.get(k_fast).isNull());
    CHECK(f.gate.other_decodes == 2);
    CHECK_FALSE(f.cache.forget_missing({late}));
}

TEST_CASE("Background image cache does not keep a decode begun before a file arrived",
          "[render][image-cache]")
{
    CacheFixture f;
    auto decode = std::async(std::launch::async, [&] { return f.cache.get(k_slow); });
    REQUIRE(f.gate.wait_entered());
    // It may have found the file missing: the paint waiting on it is drawn again.
    CHECK_FALSE(f.cache.forget_missing({"/static/images/other.png"}));
    CHECK(f.cache.forget_missing({k_slow}));
    f.gate.release();
    CHECK(!decode.get().isNull());
    CHECK(!f.cache.get(k_slow).isNull());
    CHECK(f.gate.slow_decodes == 2);
}

TEST_CASE("Background image cache never reads an unsafe URL", "[render][image-cache]")
{
    // A hit skips the URL check (only checked URLs are cached), so a
    // rejected URL must still never reach the decoder, however often it is
    // asked for.
    CacheFixture f;
    for (int i = 0; i < 2; ++i) {
        CHECK(f.cache.get("/static/../images/fast.png").isNull());
        CHECK(f.cache.get("/etc/passwd").isNull());
        CHECK(!f.cache.ensure("/static/images/\"fast.png"));
    }
    CHECK(f.gate.other_decodes == 0);
}

TEST_CASE("Background image cache finds a padded URL under its checked form", "[render][image-cache]")
{
    CacheFixture f;
    REQUIRE(!f.cache.get(k_fast).isNull());
    CHECK(!f.cache.get("  /static/images/fast.png ").isNull());
    CHECK(f.gate.other_decodes == 1);
}

TEST_CASE("Background image cache does not serve one file for another that extends its name", "[render][image-cache]")
{
    // "@" is legal in a file name ("photo.png@2x.png" is a common retina
    // twin), and the cache keys entries as name@edge. A smaller request for
    // photo.png must never be served the twin's larger decode.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    for (const char* name : {"photo.png", "photo.png@2x.png"}) {
        std::ofstream(root.path / "images" / name) << "x";
    }
    BackgroundImageCache cache([](const std::filesystem::path& path, int) {
        QImage img(4, 4, QImage::Format_ARGB32_Premultiplied);
        img.fill(path.filename() == "photo.png" ? QColor(Qt::red) : QColor(Qt::blue));
        return img;
    });
    cache.set_static_root(root.path);

    REQUIRE(cache.get("/static/images/photo.png@2x.png").pixelColor(0, 0) == QColor(Qt::blue));
    CHECK(cache.get("/static/images/photo.png", 800).pixelColor(0, 0) == QColor(Qt::red));
}

// Every output's presenter draws from the one cache (OutputSurfaces), so a
// picture shown on two outputs is decoded once, not once an output.
TEST_CASE("Presenters sharing a cache decode a picture once", "[render][image-cache]")
{
    seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    std::ofstream(root.path / "images" / "fast.png") << "x";
    seventhslide::test::ensure_qt_application();
    GatedDecoder gate;
    auto cache = std::make_shared<BackgroundImageCache>(
        [&gate](const std::filesystem::path& p, int edge) { return gate.decode(p, edge); });
    cache->set_static_root(root.path);

    seventhslide::scene::SceneFrame frame;
    frame.mode = seventhslide::scene::SceneMode::Song;
    frame.canvas_width = 160;
    frame.canvas_height = 90;
    frame.background.type = seventhslide::layout::BackgroundType::Image;
    frame.background.image = k_fast;
    seventhslide::render::ScenePresenter main;
    seventhslide::render::ScenePresenter stage;
    for (auto* presenter : {&main, &stage}) {
        presenter->share_image_cache(cache);
        presenter->present(seventhslide::scene::share_frame(frame));
    }
    CHECK(main.surface(QSize(160, 90), Qt::black).pixelColor(80, 45) == QColor(Qt::red));
    CHECK(stage.surface(QSize(160, 90), Qt::black).pixelColor(80, 45) == QColor(Qt::red));
    CHECK(gate.other_decodes == 1);
}
