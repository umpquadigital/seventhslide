#include <catch2/catch_test_macros.hpp>

#include "destinations_controller.hpp"
#include "library_registry_controller.hpp"
#include "mixer_controller.hpp"
#include "output_settings_controller.hpp"
#include "session_facade.hpp"
#include "stack_controller.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/library/audio_mix_store.hpp>
#include <seventhslide/media/audio_levels.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window_host.hpp>

#include <QTest>

#include <algorithm>
#include <cmath>
#include <string>

using seventhslide::app::MixerChannelModel;
using seventhslide::app::SessionFacade;
using seventhslide::library::AudioMixStore;
using seventhslide::library::ChannelMix;
namespace audio = seventhslide::audio;

namespace {

std::string first_output(SessionFacade& facade)
{
    return facade.session().outputs().front().id;
}

ChannelMix engine_strip(SessionFacade& facade, audio::StripKind kind, const std::string& id)
{
    return facade.mixer()->engine().mixer().strip(kind, id);
}

/// What the tab shows for ``id``: {gainDb, muted, listen}.
ChannelMix model_strip(const MixerChannelModel& model, const std::string& id)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto at = model.index(row);
        if (model.data(at, MixerChannelModel::ChannelIdRole).toString().toStdString() == id) {
            return {model.data(at, MixerChannelModel::GainDbRole).toDouble(),
                    model.data(at, MixerChannelModel::MutedRole).toBool(),
                    model.data(at, MixerChannelModel::ListenRole).toBool()};
        }
    }
    FAIL("no strip " << id);
    return {};
}

QString new_stack_id(SessionFacade& facade)
{
    return facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
}

}  // namespace

TEST_CASE("A new library's first output Listens, and what is left is kept",
          "[app][audio][libraries]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    std::string main_output;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        auto& mixer = *facade.mixer();
        main_output = first_output(facade);

        // Main's output plays on this computer; nothing else does.
        REQUIRE(model_strip(*mixer.outputs(), main_output) == ChannelMix{0.0, false, true});
        REQUIRE(engine_strip(facade, audio::StripKind::Output, main_output).listen);
        REQUIRE(model_strip(*mixer.stacks(), "main") == ChannelMix{});
        REQUIRE_FALSE(engine_strip(facade, audio::StripKind::Stack, "main").listen);
        // Kept at once, so the rule is not applied to this library again.
        REQUIRE(AudioMixStore(tmp.path).load("default").has_value());

        REQUIRE(mixer.setListen(QStringLiteral("output"), QString::fromStdString(main_output), false)
                    .isEmpty());
        REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -12.0).isEmpty());
        REQUIRE(mixer.setMuted(QStringLiteral("stack"), QStringLiteral("main"), true).isEmpty());
        REQUIRE(engine_strip(facade, audio::StripKind::Stack, "main")
                == ChannelMix{-12.0, true, false});
        REQUIRE(model_strip(*mixer.stacks(), "main") == ChannelMix{-12.0, true, false});

        // Written half a second after the last change, not at once.
        REQUIRE(AudioMixStore(tmp.path).load("default")->stack("main") == ChannelMix{});
        REQUIRE(QTest::qWaitFor(
            [&] {
                return AudioMixStore(tmp.path).load("default")->stack("main").gain_db == -12.0;
            },
            3000));

        // The last change before quitting is written on the way out.
        REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -9.0).isEmpty());
    }

    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    REQUIRE(first_output(restarted) == main_output);
    // Listen stays off where it was turned off: first use is over.
    REQUIRE(engine_strip(restarted, audio::StripKind::Output, main_output) == ChannelMix{});
    REQUIRE(engine_strip(restarted, audio::StripKind::Stack, "main")
            == ChannelMix{-9.0, true, false});
    REQUIRE(model_strip(*restarted.mixer()->stacks(), "main") == ChannelMix{-9.0, true, false});
}

TEST_CASE("Each library keeps its own Audio Mixer", "[app][audio][libraries]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    auto& mixer = *facade.mixer();
    const auto sunday_output = first_output(facade);
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -20.0).isEmpty());
    REQUIRE(mixer.setMuted(QStringLiteral("output"), QString::fromStdString(sunday_output), true)
                .isEmpty());

    // Every library has a stack "main". A switch must not carry Sunday's
    // level over to it, nor the engine's strip keep it: the new library
    // starts as new, its own first output on Listen.
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    const auto kids_output = first_output(facade);
    REQUIRE(kids_output != sunday_output);
    REQUIRE(engine_strip(facade, audio::StripKind::Stack, "main") == ChannelMix{});
    REQUIRE(model_strip(*mixer.stacks(), "main") == ChannelMix{});
    REQUIRE(engine_strip(facade, audio::StripKind::Output, kids_output)
            == ChannelMix{0.0, false, true});
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -6.0).isEmpty());

    // Back to Sunday, at once (the switch writes what is pending).
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(engine_strip(facade, audio::StripKind::Stack, "main") == ChannelMix{-20.0, false, false});
    REQUIRE(engine_strip(facade, audio::StripKind::Output, sunday_output)
            == ChannelMix{0.0, true, true});
    const AudioMixStore store(tmp.path);
    REQUIRE(store.load("kids")->stack("main").gain_db == -6.0);
    REQUIRE(store.load("default")->stack("main").gain_db == -20.0);

    // A library forgotten by the registry loses its mixer, so one made later
    // with the same id starts as new.
    REQUIRE(facade.libraries()->removeLibrary(QStringLiteral("kids")).isEmpty());
    REQUIRE_FALSE(store.load("kids").has_value());
    REQUIRE(store.load("default").has_value());
}

TEST_CASE("A deleted stack or output loses its Audio Mixer strip", "[app][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    auto& mixer = *facade.mixer();

    // A stack added gets a strip at the defaults, in the stacks' order.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = new_stack_id(facade);
    REQUIRE(mixer.stacks()->rows().back().id == lobby.toStdString());
    REQUIRE(mixer.stacks()->rows().back().name == QStringLiteral("Lobby"));
    REQUIRE(model_strip(*mixer.stacks(), lobby.toStdString()) == ChannelMix{});
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), lobby, -30.0).isEmpty());
    // A rename shows in the tab and keeps the level.
    REQUIRE(facade.stacks()->renameStack(lobby, QStringLiteral("Foyer")).isEmpty());
    REQUIRE(mixer.stacks()->rows().back().name == QStringLiteral("Foyer"));
    REQUIRE(model_strip(*mixer.stacks(), lobby.toStdString()).gain_db == -30.0);

    const auto created = facade.outputSettings()->createOutput(QStringLiteral("Stream"));
    REQUIRE(created.value(QStringLiteral("error")).toString().isEmpty());
    const auto stream = facade.session().outputs().back().id;
    REQUIRE(mixer.outputs()->contains(stream));
    REQUIRE(mixer.setGainDb(QStringLiteral("output"), QString::fromStdString(stream), -3.0)
                .isEmpty());
    mixer.flush();
    const AudioMixStore store(tmp.path);
    REQUIRE(store.load("default")->stacks.contains(lobby.toStdString()));
    REQUIRE(store.load("default")->outputs.contains(stream));

    REQUIRE(facade.stacks()->removeStack(lobby).isEmpty());
    REQUIRE(facade.outputSettings()->deleteOutput(created.value(QStringLiteral("name")).toString()).isEmpty());
    REQUIRE_FALSE(mixer.stacks()->contains(lobby.toStdString()));
    REQUIRE_FALSE(mixer.outputs()->contains(stream));
    REQUIRE_FALSE(mixer.library_mix().stacks.contains(lobby.toStdString()));
    REQUIRE_FALSE(mixer.library_mix().outputs.contains(stream));
    mixer.flush();
    REQUIRE_FALSE(store.load("default")->stacks.contains(lobby.toStdString()));
    REQUIRE_FALSE(store.load("default")->outputs.contains(stream));
    // The first output keeps its Listen.
    REQUIRE(store.load("default")->output(first_output(facade)).listen);
}

TEST_CASE("An output switched off has no Audio Mixer strip, and keeps its level",
          "[app][audio][outputs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    std::string stage;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        auto& mixer = *facade.mixer();
        REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));
        stage = facade.session().outputs().back().id;
        REQUIRE(mixer.setGainDb(QStringLiteral("output"), QString::fromStdString(stage), -12.0)
                    .isEmpty());

        // Off: no strip, and no sound routed to it.
        REQUIRE(facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), false).isEmpty());
        CHECK_FALSE(mixer.outputs()->contains(stage));
        CHECK(mixer.outputs()->contains(first_output(facade)));
        CHECK(mixer.library_mix().outputs.contains(stage));
        mixer.flush();
        CHECK(AudioMixStore(tmp.path).load("default")->output(stage).gain_db == -12.0);
    }
    // Still off after a restart; on again, its strip has its level.
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    auto& mixer = *restarted.mixer();
    CHECK_FALSE(mixer.outputs()->contains(stage));
    REQUIRE(restarted.outputSettings()->setOutputOn(QStringLiteral("Stage"), true).isEmpty());
    REQUIRE(mixer.outputs()->contains(stage));
    CHECK(model_strip(*mixer.outputs(), stage).gain_db == -12.0);
    CHECK(engine_strip(restarted, audio::StripKind::Output, stage).gain_db == -12.0);
}

TEST_CASE("The Audio Mixer's entry points say what they refuse", "[app][audio]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto& mixer = *facade.mixer();

    REQUIRE_FALSE(mixer.setGainDb(QStringLiteral("bus"), QStringLiteral("main"), -6.0).isEmpty());
    REQUIRE_FALSE(mixer.setMuted(QStringLiteral("stack"), QStringLiteral("nope"), true).isEmpty());
    REQUIRE_FALSE(mixer.setListen(QStringLiteral("output"), QStringLiteral("nope"), true).isEmpty());

    // Not a number is refused, not read as full level.
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -6.0).isEmpty());
    REQUIRE_FALSE(
        mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), std::nan("")).isEmpty());
    REQUIRE(model_strip(*mixer.stacks(), "main").gain_db == -6.0);

    // No boost: above 0 dB is 0 dB. Below the fader's floor is silence.
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), 6.0).isEmpty());
    REQUIRE(model_strip(*mixer.stacks(), "main").gain_db == 0.0);
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QStringLiteral("main"), -200.0).isEmpty());
    const auto silent = model_strip(*mixer.stacks(), "main").gain_db;
    REQUIRE((std::isinf(silent) && silent < 0.0));
    const auto row = mixer.stacks()->index(0);
    REQUIRE(mixer.stacks()->data(row, MixerChannelModel::GainTextRole).toString()
            == QStringLiteral("-inf dB"));
    REQUIRE(mixer.stacks()->data(row, MixerChannelModel::FaderPositionRole).toDouble() == 0.0);

    // The fader's taper for the view, back from the position a row gives.
    REQUIRE(mixer.faderDb(1.0) == 0.0);
    REQUIRE(std::abs(mixer.faderDb(seventhslide::media::fader_position(-12.0)) + 12.0) < 1e-9);

    // An in-memory library keeps nothing, but still starts its first output
    // on Listen.
    REQUIRE(engine_strip(facade, audio::StripKind::Output, first_output(facade)).listen);
}

TEST_CASE("The facade's destinations send their output's mix", "[app][audio][ndi]")
{
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::render::ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    const seventhslide::test::TempDir tmp;
    seventhslide::render::OutputWindowHost host;
    SessionFacade facade;
    facade.set_output_host(&host);
    REQUIRE(facade.init_libraries(tmp.path));
    const QString main_output = QString::fromStdString(first_output(facade));
    const QString ndi = host.add_destination(main_output, QStringLiteral("ndi"));
    REQUIRE_FALSE(ndi.isEmpty());
    REQUIRE(host.set_destination_ndi_name(ndi, QStringLiteral("SeventhSlide FacadeAudioTest")));
    REQUIRE(facade.destinations()->setEnabled(ndi, true).isEmpty());
    CHECK(host.outputs_sending_audio() == QStringList{main_output});
    REQUIRE(facade.destinations()->setAudio(ndi, false).isEmpty());
    CHECK(host.outputs_sending_audio().isEmpty());
    REQUIRE(facade.destinations()->setAudio(ndi, true).isEmpty());
    CHECK(host.outputs_sending_audio() == QStringList{main_output});
    host.shutdown();
}

TEST_CASE("A stack with Preview on has a cue strip under it, kept with the others",
          "[app][audio][preview]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    auto& mixer = *facade.mixer();
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = new_stack_id(facade);
    const auto cue = audio::cue_strip_id(lobby.toStdString());
    const auto cue_row = [&]() -> const MixerChannelModel::Row* {
        const auto& rows = mixer.stacks()->rows();
        const auto it = std::ranges::find(rows, cue, &MixerChannelModel::Row::id);
        return it != rows.end() ? &*it : nullptr;
    };
    REQUIRE(cue_row() == nullptr);

    // On: a strip of its own, under its stack's, named for it.
    REQUIRE(facade.stacks()->setStackPreview(lobby, true).isEmpty());
    const auto& rows = mixer.stacks()->rows();
    REQUIRE(rows.size() >= 2);
    CHECK(rows[rows.size() - 2].id == lobby.toStdString());
    REQUIRE(cue_row() == &rows.back());
    CHECK(rows.back().cue);
    CHECK_FALSE(rows[rows.size() - 2].cue);
    CHECK(rows.back().name == QStringLiteral("Lobby: Preview"));

    // Its level and Listen are set as a stack's, and kept.
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QString::fromStdString(cue), -12.0).isEmpty());
    REQUIRE(mixer.setListen(QStringLiteral("stack"), QString::fromStdString(cue), true).isEmpty());
    CHECK(engine_strip(facade, audio::StripKind::Stack, cue) == ChannelMix{-12.0, false, true});
    mixer.flush();
    const AudioMixStore store(tmp.path);
    CHECK(store.load("default")->stack(cue) == ChannelMix{-12.0, false, true});

    // Off, the strip goes and its settings stay for when it is on again.
    REQUIRE(facade.stacks()->setStackPreview(lobby, false).isEmpty());
    CHECK(cue_row() == nullptr);
    CHECK(mixer.library_mix().stacks.contains(cue));
    REQUIRE(facade.stacks()->setStackPreview(lobby, true).isEmpty());
    REQUIRE(cue_row() != nullptr);
    CHECK(model_strip(*mixer.stacks(), cue) == ChannelMix{-12.0, false, true});

    // The stack deleted, they go with it, and as much with its Preview off.
    REQUIRE(facade.stacks()->removeStack(lobby).isEmpty());
    CHECK(cue_row() == nullptr);
    CHECK_FALSE(mixer.library_mix().stacks.contains(cue));

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Hall"), QStringLiteral("info")).isEmpty());
    const auto hall = new_stack_id(facade);
    const auto hall_cue = QString::fromStdString(audio::cue_strip_id(hall.toStdString()));
    REQUIRE(facade.stacks()->setStackPreview(hall, true).isEmpty());
    REQUIRE(mixer.setGainDb(QStringLiteral("stack"), hall_cue, -6.0).isEmpty());
    REQUIRE(facade.stacks()->setStackPreview(hall, false).isEmpty());
    REQUIRE(mixer.library_mix().stacks.contains(hall_cue.toStdString()));
    REQUIRE(facade.stacks()->removeStack(hall).isEmpty());
    CHECK_FALSE(mixer.library_mix().stacks.contains(hall_cue.toStdString()));
}
