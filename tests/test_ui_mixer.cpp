// The Audio Mixer panel, driven as the operator drives it: a strip per stack
// or output, Mute and Listen, the fader, the meters and the tabs.

#include <catch2/catch_test_macros.hpp>

#include "audio_meter_item.hpp"
#include "dock_layout.hpp"
#include "live_controller.hpp"
#include "media_controller.hpp"
#include "mixer_controller.hpp"
#include "operator_ui.hpp"
#include "stack_controller.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_test_helpers.hpp"
#include "wav_fixture.hpp"

#include <seventhslide/media/audio_levels.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QTest>

#include <cmath>
#include <filesystem>

using seventhslide::app::AudioMeterItem;
using seventhslide::app::DockLayout;
using seventhslide::app::MixerController;
using seventhslide::app::SessionFacade;
using seventhslide::library::ChannelMix;
using seventhslide::test::OperatorUi;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::drag;
using seventhslide::test::list_view_of;
using seventhslide::test::row_of;
using seventhslide::test::visible_items_of;

namespace {

/// The DockPanel holding panel ``id``.
QQuickItem* dock_panel(const OperatorUi& ui, const QString& id)
{
    for (QQuickItem* item : ui.window()->findChildren<QQuickItem*>()) {
        if (QString::fromLatin1(item->metaObject()->className())
                    .startsWith(QStringLiteral("DockPanel_QMLTYPE_"))
            && item->property("dockId").toString() == id) {
            return item;
        }
    }
    FAIL("no panel " << id.toStdString());
    return nullptr;
}

/// The first visible item under ``root`` whose ``property`` is ``value``.
QQuickItem* item_with(QQuickItem* root, const char* property, const QVariant& value)
{
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (item->property(property) == value) {
            return item;
        }
        if (QQuickItem* found = item_with(item, property, value)) {
            return found;
        }
    }
    return nullptr;
}

/// The first visible QML item of type ``type`` under ``root``.
QQuickItem* drawn(QQuickItem* root, const char* type)
{
    const QString prefix = QString::fromLatin1(type) + QStringLiteral("_QMLTYPE_");
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (QString::fromLatin1(item->metaObject()->className()).startsWith(prefix)) {
            return item;
        }
        if (QQuickItem* found = drawn(item, type)) {
            return found;
        }
    }
    return nullptr;
}

/// Strip ``index`` of the tab showing ``model``.
QQuickItem* strip_of(const OperatorUi& ui, QObject* model, int index)
{
    QQuickItem* view = list_view_of(ui.window(), model);
    REQUIRE(view != nullptr);
    return row_of(view, index);
}

/// A strip's button, by the glyph it shows.
QPoint button_of(QQuickItem* strip, const char* icon)
{
    QQuickItem* button = item_with(strip, "icon", QString::fromUtf8(icon));
    INFO("button " << icon);
    REQUIRE(button != nullptr);
    return centre_of(button);
}

void forget_tab()
{
    QSettings().remove(QStringLiteral("audioMixer/tab"));
}

}  // namespace

TEST_CASE("The Audio Mixer sits under Outputs, a strip per stack", "[ui][audio]")
{
    seventhslide::test::ensure_qt_application();
    forget_tab();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    OperatorUi ui(facade);
    QTest::qWait(50);
    MixerController& mixer = *facade.mixer();

    // Bottom right: the right column, under the Outputs panel.
    QQuickItem* outputs = dock_panel(ui, QStringLiteral("outputs"));
    QQuickItem* panel = dock_panel(ui, QStringLiteral("mixer"));
    CHECK(panel->x() == outputs->x());
    CHECK(panel->y() > outputs->y());
    CHECK(panel->width() > 0);

    // The Stacks tab first: Main and Lobby.
    CHECK(mixer.shownTab() == QStringLiteral("stacks"));
    QQuickItem* main = strip_of(ui, mixer.stacks(), 0);
    CHECK(seventhslide::test::visible_text_under(main, QStringLiteral("0.0 dB")) != nullptr);
    CHECK(strip_of(ui, mixer.stacks(), 1)->property("name").toString() == QStringLiteral("Lobby"));

    // Mute and Listen, each a click; again, off.
    click(ui.window(), button_of(main, "audio"));
    CHECK(mixer.library_mix().stack("main").muted);
    click(ui.window(), button_of(main, "speakerMuted"));
    CHECK_FALSE(mixer.library_mix().stack("main").muted);
    click(ui.window(), button_of(main, "headphones"));
    CHECK(mixer.library_mix().stack("main").listen);
    click(ui.window(), button_of(main, "headphones"));
    CHECK_FALSE(mixer.library_mix().stack("main").listen);

    // The fader: pressed below the handle, it jumps there and the level falls;
    // a double-click is 0 dB again.
    QQuickItem* fader = drawn(main, "VerticalFader");
    REQUIRE(fader != nullptr);
    const QPoint low = fader->mapToScene(QPointF(fader->width() / 2, fader->height() * 0.6)).toPoint();
    click(ui.window(), low);
    const double pressed = mixer.library_mix().stack("main").gain_db;
    CHECK(pressed < -3.0);
    CHECK(std::isfinite(pressed));
    CHECK(seventhslide::test::visible_text_under(
              main, QString::fromStdString(seventhslide::media::format_gain_db(pressed)))
          != nullptr);
    // Dragged by the handle, it follows: to the bottom is silence.
    const QPoint handle = fader->mapToScene(QPointF(fader->width() / 2,
                                                    fader->property("handleY").toReal() + 5))
                              .toPoint();
    drag(ui.window(), handle, handle + QPoint(0, int(fader->height())));
    CHECK(std::isinf(mixer.library_mix().stack("main").gain_db));
    QTest::mouseDClick(ui.window(), Qt::LeftButton, {}, centre_of(fader));
    QTest::qWait(10);
    CHECK(mixer.library_mix().stack("main").gain_db == 0.0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A stack with Preview on shows its cue strip under its own", "[ui][audio][preview]")
{
    seventhslide::test::ensure_qt_application();
    forget_tab();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->setStackPreview(QStringLiteral("main"), true).isEmpty());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    OperatorUi ui(facade);
    QTest::qWait(50);
    MixerController& mixer = *facade.mixer();

    // Main, then Main's Preview, then Lobby.
    QQuickItem* cue = strip_of(ui, mixer.stacks(), 1);
    CHECK(cue->property("name").toString() == QStringLiteral("Main: Preview"));
    CHECK(strip_of(ui, mixer.stacks(), 2)->property("name").toString() == QStringLiteral("Lobby"));
    CHECK(visible_items_of(cue, "seventhslide::app::AudioMeterItem").size() >= 1);

    // Listen on it, a click: kept as any strip's is.
    const auto id = seventhslide::audio::cue_strip_id("main");
    click(ui.window(), button_of(cue, "headphones"));
    CHECK(mixer.library_mix().stack(id).listen);
    CHECK_FALSE(mixer.library_mix().stack("main").listen);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The Audio Mixer's Outputs tab, and the tab kept for next time", "[ui][audio]")
{
    seventhslide::test::ensure_qt_application();
    forget_tab();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    MixerController& mixer = *facade.mixer();
    const auto main_output = facade.session().outputs().front().id;
    {
        OperatorUi ui(facade);
        QTest::qWait(50);
        QQuickItem* panel = dock_panel(ui, QStringLiteral("mixer"));
        QQuickItem* tab = seventhslide::test::visible_text_under(panel, QStringLiteral("Outputs"));
        REQUIRE(tab != nullptr);
        click(ui.window(), centre_of(tab));
        CHECK(mixer.shownTab() == QStringLiteral("outputs"));
        // Main's output strip, on Listen as a new library starts it.
        QQuickItem* strip = strip_of(ui, mixer.outputs(), 0);
        CHECK(strip->property("listen").toBool());
        click(ui.window(), button_of(strip, "headphones"));
        CHECK_FALSE(mixer.library_mix().output(main_output).listen);
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }
    // The next window opens on the tab left showing.
    MixerController again(facade.session(), facade.open_library());
    CHECK(again.shownTab() == QStringLiteral("outputs"));
    forget_tab();
}

TEST_CASE("The Audio Mixer's meters read the engine while they show", "[ui][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto audio = facade.media_root() / "audio";
    std::filesystem::create_directories(audio);
    seventhslide::test::write_tone_wav(audio / "bed.wav", 2.0);
    facade.refresh_library();
    OperatorUi ui(facade);
    QTest::qWait(50);
    MixerController& mixer = *facade.mixer();
    CHECK(mixer.meters_running());

    QQuickItem* main = strip_of(ui, mixer.stacks(), 0);
    const auto meters = visible_items_of(main, "seventhslide::app::AudioMeterItem");
    REQUIRE(meters.size() == 1);
    auto* meter = qobject_cast<AudioMeterItem*>(meters.front());
    REQUIRE(meter != nullptr);
    CHECK(std::isinf(meter->reading().level_db[0]));

    // Playing on Main, its meter rises.
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    REQUIRE(QTest::qWaitFor([&] { return meter->reading().level_db[0] > -40.0f; }, 5000));
    // Muted, the meter still reads, drawn grey.
    REQUIRE(mixer.setMuted(QStringLiteral("stack"), QStringLiteral("main"), true).isEmpty());
    QTest::qWait(50);
    CHECK(meter->dimmed());
    CHECK(meter->reading().level_db[0] > -40.0f);

    // Hidden by Layout, no meter shows and the meters stop reading.
    ui.dock_layout()->setShown(QStringLiteral("mixer"), false);
    QTest::qWait(20);
    CHECK_FALSE(mixer.meters_running());
    ui.dock_layout()->setShown(QStringLiteral("mixer"), true);
    REQUIRE(QTest::qWaitFor([&] { return mixer.meters_running(); }, 2000));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
