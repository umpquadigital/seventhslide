// Qt WebEngine's renderer drawing a real page off screen, and that page on an
// output's preview. It needs a display and Qt WebEngine's set-up before the
// application exists, so it runs only when asked:
//
//   SEVENTHSLIDE_TEST_WEBENGINE=1 ./build/bin/seventhslide_tests_app "[webengine]"

#include <catch2/catch_test_macros.hpp>

#include "session_facade_fixture.hpp"

#ifdef SEVENTHSLIDE_HAS_WEBENGINE
#include "webengine_renderer.hpp"
#endif
#include "output_preview_controller.hpp"
#include "web_library_controller.hpp"
#include "web_thumbs.hpp"

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/audio_source.hpp>

#include <QDeadlineTimer>
#include <QTest>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>

using namespace seventhslide::test::facade;

#ifdef SEVENTHSLIDE_HAS_WEBENGINE
namespace {

/// A page that is one colour edge to edge.
QString solid_page(const char* css_colour)
{
    return QStringLiteral("data:text/html,<body style='margin:0;background:%1'></body>")
        .arg(QString::fromUtf8(css_colour));
}

} // namespace
#endif

TEST_CASE("Qt WebEngine draws a page off screen for the outputs", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();

    // The renderer alone: the page's colour at the page's size.
    auto renderer = seventhslide::app::webengine_renderer_factory()();
    renderer->load(QUrl(solid_page("%23ff0000")), QSize(640, 360), 1.0);
    QDeadlineTimer deadline(20000);
    while (renderer->frame().isNull()
           || renderer->frame().pixelColor(320, 180) != QColor(Qt::red)) {
        REQUIRE_FALSE(deadline.hasExpired());
        QTest::qWait(50);
    }
    CHECK(renderer->frame().size() == QSize(640, 360));
    CHECK(renderer->problem().isEmpty());

    // A page that cannot load says why.
    auto missing = seventhslide::app::webengine_renderer_factory()();
    missing->load(QUrl(QStringLiteral("file:///nonexistent/seventhslide/page.html")),
                  QSize(320, 180), 1.0);
    QDeadlineTimer fail_deadline(20000);
    while (missing->problem().isEmpty()) {
        REQUIRE_FALSE(fail_deadline.hasExpired());
        QTest::qWait(50);
    }

    // Through the session: a page from the library, live, shows on the
    // output's preview.
    const seventhslide::test::TempDir dir;
    const auto file = dir.path / "page.html";
    {
        std::ofstream out(file);
        out << "<body style='margin:0;background:#0000ff'></body>";
    }
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_web_renderer_factory(seventhslide::app::webengine_renderer_factory());
    const auto saved = facade.webLibrary()->savePage(
        -1, {{QStringLiteral("url"), QString::fromStdString(file.string())},
             {QStringLiteral("width"), 640},
             {QStringLiteral("height"), 360}});
    REQUIRE(saved.contains(QStringLiteral("id")));
    REQUIRE(facade.webLibrary()->playPage(saved.value(QStringLiteral("id")).toLongLong()));
    const auto out_id = QString::fromStdString(facade.session().primary_output()->id);
    QDeadlineTimer live_deadline(20000);
    QImage preview;
    while (true) {
        preview = facade.outputPreviews()->render_output_preview(out_id, QSize(160, 90));
        if (!preview.isNull() && preview.pixelColor(80, 45) == QColor(Qt::blue)) {
            break;
        }
        REQUIRE_FALSE(live_deadline.hasExpired());
        QTest::qWait(50);
    }
#endif
}

TEST_CASE("Qt WebEngine's page answers a click, a key and the wheel", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    // Red; a click turns it green, a key blue, and scrolling (the page is
    // taller than the view) yellow.
    const QString page = QStringLiteral(
        "data:text/html,<body style='margin:0;height:3000px;background:red'>"
        "<script>"
        "onclick=()=>document.body.style.background='lime';"
        "onkeydown=()=>document.body.style.background='blue';"
        "onscroll=()=>document.body.style.background='yellow';"
        "</script></body>");
    auto renderer = seventhslide::app::webengine_renderer_factory()();
    renderer->load(QUrl(page), QSize(1280, 720), 1.0);
    const auto wait_for = [&](const QColor& colour) {
        QDeadlineTimer deadline(20000);
        while (renderer->frame().isNull() || renderer->frame().pixelColor(100, 100) != colour) {
            INFO("waiting for " << colour.name().toStdString() << ", page is "
                                << (renderer->frame().isNull()
                                        ? std::string("not drawn")
                                        : renderer->frame().pixelColor(100, 100).name().toStdString()));
            REQUIRE_FALSE(deadline.hasExpired());
            QTest::qWait(50);
        }
    };
    wait_for(QColor(Qt::red));

    // Each event is made as it is sent: a device's point is shared by its
    // events, so a release made early would turn the press into one.
    const QPointF at(100, 100);
    {
        QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton, {});
        REQUIRE(renderer->send_input(&press));
    }
    {
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton, Qt::NoButton,
                            {});
        REQUIRE(renderer->send_input(&release));
    }
    wait_for(QColor(0, 255, 0));

    QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    REQUIRE(renderer->send_input(&key));
    wait_for(QColor(Qt::blue));

    QWheelEvent wheel(at, at, QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    REQUIRE(renderer->send_input(&wheel));
    wait_for(QColor(Qt::yellow));
#endif
}

TEST_CASE("Qt WebEngine's own pop-ups do not reach the page's picture", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    // One colour; a click turns it green after an alert.
    const QString page = QStringLiteral(
        "data:text/html,<body style='margin:0;background:red'>"
        "<script>onclick=()=>{alert('hello');document.body.style.background='lime'}</script>"
        "</body>");
    auto renderer = seventhslide::app::webengine_renderer_factory()();
    renderer->load(QUrl(page), QSize(640, 360), 1.0);
    const auto settle_on = [&](const QColor& colour) {
        QDeadlineTimer deadline(20000);
        while (renderer->frame().isNull() || renderer->frame().pixelColor(5, 5) != colour) {
            REQUIRE_FALSE(deadline.hasExpired());
            QTest::qWait(50);
        }
        QTest::qWait(500);  // time for anything Chromium would draw over it
    };
    const auto uniform = [&](const QColor& colour) {
        const QImage f = renderer->frame();
        for (int y = 0; y < f.height(); y += 8) {
            for (int x = 0; x < f.width(); x += 8) {
                if (f.pixelColor(x, y) != colour) {
                    return false;
                }
            }
        }
        return true;
    };
    settle_on(QColor(Qt::red));

    // A right-click: no menu.
    const QPointF at(320, 180);
    {
        QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::RightButton, Qt::RightButton, {});
        REQUIRE(renderer->send_input(&press));
    }
    {
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::RightButton, Qt::NoButton,
                            {});
        REQUIRE(renderer->send_input(&release));
    }
    QTest::qWait(1000);
    CHECK(uniform(QColor(Qt::red)));

    // An alert: no box, and the page carries on past it.
    {
        QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton, {});
        REQUIRE(renderer->send_input(&press));
    }
    {
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton, Qt::NoButton,
                            {});
        REQUIRE(renderer->send_input(&release));
    }
    settle_on(QColor(0, 255, 0));
    CHECK(uniform(QColor(0, 255, 0)));
#endif
}

TEST_CASE("Qt WebEngine's pages keep what they store", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto file = dir.path / "page.html";
    {
        std::ofstream out(file);
        // Red the first time the page is seen; green once it has stored that.
        out << "<body style='margin:0'><script>"
               "document.body.style.background = localStorage.getItem('seen') ? 'lime' : 'red';"
               "localStorage.setItem('seen', '1');</script></body>";
    }
    const auto storage = dir.path / "store";
    const auto factory = seventhslide::app::webengine_renderer_factory(
        QString::fromStdString(storage.string()));
    const auto shows = [&](const QColor& colour) {
        auto renderer = factory();
        renderer->load(QUrl::fromLocalFile(QString::fromStdString(file.string())), QSize(320, 180),
                       1.0);
        QDeadlineTimer deadline(20000);
        while (renderer->frame().isNull() || renderer->frame().pixelColor(5, 5) != colour) {
            REQUIRE_FALSE(deadline.hasExpired());
            QTest::qWait(50);
        }
    };
    shows(QColor(Qt::red));
    // Another page, later, finds it: one profile, kept on disk.
    shows(QColor(0, 255, 0));
    CHECK(std::filesystem::exists(storage));
    CHECK_FALSE(std::filesystem::is_empty(storage));
#endif
}

TEST_CASE("Qt WebEngine's page is heard through its sound, not the speakers", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto write = [&](const char* name, const std::string& text) {
        std::ofstream out(dir.path / name, std::ios::binary);
        out << text;
    };
    // A half-scale 440 Hz tone, ten seconds of 16-bit stereo WAV.
    {
        constexpr int rate = 48000;
        constexpr int frames = rate * 10;
        std::string wav = "RIFF____WAVEfmt ";
        const auto u32 = [&wav](std::uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                wav.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
            }
        };
        const auto u16 = [&wav](std::uint16_t v) {
            wav.push_back(static_cast<char>(v & 0xff));
            wav.push_back(static_cast<char>(v >> 8));
        };
        u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
        wav += "data";
        u32(frames * 4);
        for (int i = 0; i < frames; ++i) {
            const auto v = static_cast<std::int16_t>(16384 * std::sin(2 * std::numbers::pi * 440.0 * i / rate));
            u16(static_cast<std::uint16_t>(v));
            u16(static_cast<std::uint16_t>(v));
        }
        const auto size = static_cast<std::uint32_t>(wav.size() - 8);
        for (int i = 0; i < 4; ++i) {
            wav[4 + i] = static_cast<char>((size >> (8 * i)) & 0xff);
        }
        write("tone.wav", wav);
    }
    write("synth.html", "<body><script>const c = new AudioContext(); const o = c.createOscillator();"
                        "const g = c.createGain(); g.gain.value = 0.5; o.connect(g);"
                        "g.connect(c.destination); o.start();</script></body>");
    write("player.html", "<body><audio src='tone.wav' autoplay loop></audio></body>");
    write("framed.html", "<body><iframe src='player.html'></iframe></body>");

    write("both.html", "<body><audio src='tone.wav' autoplay loop></audio><script>"
                       "const c = new AudioContext(); const o = c.createOscillator();"
                       "o.frequency.value = 660; const g = c.createGain(); g.gain.value = 0.5;"
                       "o.connect(g); g.connect(c.destination); o.start();</script></body>");

    // The level a page settles at: ``least`` reached, never ``most``.
    const auto heard = [&](const char* page, double least = 0.3, double most = 0.4) {
        auto renderer = seventhslide::app::webengine_renderer_factory()();
        REQUIRE(renderer->sound() != nullptr);
        renderer->load(QUrl::fromLocalFile(QString::fromStdString((dir.path / page).string())),
                       QSize(320, 180), 1.0);
        // The level of the latest block, until it is that of the tone.
        std::vector<float> block(seventhslide::audio::k_block_frames * seventhslide::audio::k_channels);
        double rms = 0.0;
        QDeadlineTimer deadline(20000);
        while (rms < least) {
            REQUIRE_FALSE(deadline.hasExpired());
            QTest::qWait(10);
            renderer->sound()->read(block.data(), seventhslide::audio::k_block_frames);
            double sum = 0.0;
            for (const float v : block) {
                sum += static_cast<double>(v) * v;
            }
            rms = std::sqrt(sum / static_cast<double>(block.size()));
        }
        // A half-scale sine: 0.35.
        CHECK(rms < most);
        // And it stays there.
        QTest::qWait(500);
        for (int i = 0; i < 20; ++i) {
            renderer->sound()->read(block.data(), seventhslide::audio::k_block_frames);
        }
        double sum = 0.0;
        for (const float v : block) {
            sum += static_cast<double>(v) * v;
        }
        const double settled = std::sqrt(sum / static_cast<double>(block.size()));
        CHECK(settled > least);
        CHECK(settled < most);
    };
    SECTION("Web Audio") { heard("synth.html"); }
    SECTION("an audio element in a frame") { heard("framed.html"); }
    // Two half-scale sines at different pitches, summed: 0.5.
    SECTION("both at once, each its own stream") { heard("both.html", 0.45, 0.55); }
#endif
}

TEST_CASE("Qt WebEngine draws a page's thumbnail once it has loaded", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    {
        std::ofstream out(dir.path / "page.html");
        out << "<body style='margin:0;background:#00f'></body>";
    }
    seventhslide::app::WebThumbs thumbs([&] { return dir.path / "thumbs"; });
    thumbs.set_factory(seventhslide::app::webengine_renderer_factory());
    seventhslide::model::WebPage page;
    page.url = QUrl::fromLocalFile(QString::fromStdString((dir.path / "page.html").string()))
                   .toString()
                   .toStdString();
    page.width = 1280;
    page.height = 720;
    thumbs.want({page});
    QDeadlineTimer deadline(30000);
    while (thumbs.pending() > 0) {
        REQUIRE_FALSE(deadline.hasExpired());
        QTest::qWait(50);
    }
    const QImage kept(QString::fromStdString(thumbs.path_of(page).string()));
    REQUIRE_FALSE(kept.isNull());
    CHECK(kept.size() == QSize(512, 288));
    const QColor middle = kept.pixelColor(256, 144);
    CHECK(middle.blue() > 200);
    CHECK(middle.red() < 40);
#endif
}

TEST_CASE("Qt WebEngine's page follows its links, and goes back", "[webengine][.]")
{
#ifndef SEVENTHSLIDE_HAS_WEBENGINE
    SKIP("built without Qt WebEngine");
#else
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        SKIP("set SEVENTHSLIDE_TEST_WEBENGINE=1 to run");
    }
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto write = [&](const char* name, const char* text) {
        std::ofstream out(dir.path / name);
        out << text;
    };
    // Each page one link filling it: the next page, then one for a new window.
    write("one.html", "<body style='margin:0'><a href='two.html' style='display:block;"
                      "width:100vw;height:100vh;background:red'></a></body>");
    write("two.html", "<body style='margin:0'><a href='three.html' target='_blank' "
                      "style='display:block;width:100vw;height:100vh;background:lime'></a></body>");
    write("three.html", "<body style='margin:0;background:blue'></body>");
    const auto page = [&](const char* name) {
        return QUrl::fromLocalFile(QString::fromStdString((dir.path / name).string()));
    };
    auto renderer = seventhslide::app::webengine_renderer_factory()();
    QSignalSpy navigated(renderer.get(), &seventhslide::app::WebRenderer::navigated);
    renderer->load(page("one.html"), QSize(320, 180), 1.0);
    const auto wait_for = [&](const QColor& colour) {
        QDeadlineTimer deadline(20000);
        while (renderer->frame().isNull() || renderer->frame().pixelColor(160, 90) != colour) {
            INFO("waiting for " << colour.name().toStdString() << " at "
                                << renderer->url().toString().toStdString() << ", page is "
                                << (renderer->frame().isNull()
                                        ? std::string("not drawn")
                                        : renderer->frame().pixelColor(160, 90).name().toStdString()));
            REQUIRE_FALSE(deadline.hasExpired());
            QTest::qWait(50);
        }
    };
    const auto click = [&] {
        const QPointF at(160, 90);
        {
            QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton, {});
            REQUIRE(renderer->send_input(&press));
        }
        {
            QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton,
                                Qt::NoButton, {});
            REQUIRE(renderer->send_input(&release));
        }
    };
    wait_for(QColor(Qt::red));
    CHECK(renderer->url() == page("one.html"));
    CHECK_FALSE(renderer->can_go_back());

    click();
    wait_for(QColor(0, 255, 0));
    CHECK(renderer->url() == page("two.html"));
    CHECK(renderer->can_go_back());
    CHECK(navigated.count() >= 1);

    // A link to a new window opens in the page itself.
    click();
    wait_for(QColor(Qt::blue));
    CHECK(renderer->url() == page("three.html"));

    renderer->go_back();
    wait_for(QColor(0, 255, 0));
    CHECK(renderer->url() == page("two.html"));

    // An address gone to (typed in the controller's bar) is a link followed:
    // back comes back from it.
    REQUIRE(renderer->navigate(page("three.html")));
    wait_for(QColor(Qt::blue));
    CHECK(renderer->url() == page("three.html"));
    renderer->go_back();
    wait_for(QColor(0, 255, 0));
    CHECK(renderer->url() == page("two.html"));

    // An address that does not load says so; the next one gone to clears it
    // while it loads.
    REQUIRE(renderer->navigate(page("missing.html")));
    QDeadlineTimer failing(20000);
    while (renderer->problem().isEmpty()) {
        REQUIRE_FALSE(failing.hasExpired());
        QTest::qWait(50);
    }
    REQUIRE(renderer->navigate(page("one.html")));
    CHECK(renderer->problem().isEmpty());
    wait_for(QColor(Qt::red));
    CHECK(renderer->problem().isEmpty());
#endif
}

