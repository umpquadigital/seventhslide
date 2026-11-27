#include <catch2/catch_test_macros.hpp>

#include <seventhslide/library/audio_mix_store.hpp>

#include <nlohmann/json.hpp>

#include <QTemporaryDir>

#include <fstream>
#include <limits>

using seventhslide::library::AudioMixStore;
using seventhslide::library::ChannelMix;
using seventhslide::library::LibraryMix;
using nlohmann::json;

namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("A strip nobody touched is at 0 dB, unmuted, off Listen", "[library][audio]")
{
    const LibraryMix mix;
    REQUIRE(mix.stack("main") == ChannelMix{});
    REQUIRE(mix.output("out-main") == ChannelMix{});
    REQUIRE(mix.stack("main").gain_db == 0.0);
    REQUIRE_FALSE(mix.stack("main").muted);
    REQUIRE_FALSE(mix.stack("main").listen);
}

TEST_CASE("First use puts only the first output on Listen", "[library][audio]")
{
    const auto mix = LibraryMix::first_use({"out-main", "out-stage", "out-stream"});
    REQUIRE(mix.output("out-main").listen);
    REQUIRE(mix.output("out-main").gain_db == 0.0);
    REQUIRE_FALSE(mix.output("out-main").muted);
    REQUIRE_FALSE(mix.output("out-stage").listen);
    REQUIRE_FALSE(mix.output("out-stream").listen);
    REQUIRE(mix.stacks.empty());

    REQUIRE(LibraryMix::first_use({}) == LibraryMix{});
}

TEST_CASE("A strip reads back what it wrote, levels normalized", "[library][audio]")
{
    const ChannelMix strip{-12.5, true, true};
    REQUIRE(ChannelMix::from_json(strip.to_json()) == strip);

    // Silence is written as "-inf", since JSON has no infinity.
    const ChannelMix silent{-k_inf, false, false};
    REQUIRE(silent.to_json().at("gain_db") == "-inf");
    REQUIRE(ChannelMix::from_json(silent.to_json()).gain_db == -k_inf);

    // What makes no sense falls back to the defaults, field by field.
    REQUIRE(ChannelMix::from_json(json{{"gain_db", 9.0}}).gain_db == 0.0);
    REQUIRE(ChannelMix::from_json(json{{"gain_db", -500.0}}).gain_db == -k_inf);
    REQUIRE(ChannelMix::from_json(json{{"gain_db", "loud"}, {"muted", "yes"}, {"listen", true}})
            == ChannelMix{0.0, false, true});
    REQUIRE(ChannelMix::from_json(json::array()) == ChannelMix{});
}

TEST_CASE("AudioMixStore keeps each library's mixer apart", "[library][audio]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const AudioMixStore store(tmp.path().toStdString());

    // Nothing kept yet: the caller applies first use.
    REQUIRE_FALSE(store.load("default").has_value());

    LibraryMix sunday;
    sunday.stacks["main"] = {-6.0, false, false};
    sunday.stacks["lower-thirds"] = {-k_inf, true, false};
    sunday.outputs["out-main"] = {0.0, false, true};
    REQUIRE(store.save("default", sunday));

    LibraryMix kids;
    kids.stacks["main"] = {-20.0, false, true};
    REQUIRE(store.save("kids", kids));

    REQUIRE(store.load("default") == sunday);
    REQUIRE(store.load("kids") == kids);

    // A save replaces the whole library's mixer, and only that library's.
    LibraryMix quieter = sunday;
    quieter.stacks["main"].gain_db = -10.0;
    quieter.outputs.erase("out-main");
    REQUIRE(store.save("default", quieter));
    REQUIRE(store.load("default") == quieter);
    REQUIRE(store.load("kids") == kids);
}

TEST_CASE("A library left all at the defaults is still known", "[library][audio]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const AudioMixStore store(tmp.path().toStdString());

    // The operator turned the first output's Listen off again: that is a
    // choice, and first use must not undo it at the next start.
    auto mix = LibraryMix::first_use({"out-main"});
    mix.outputs["out-main"].listen = false;
    REQUIRE(store.save("default", mix));

    const auto loaded = store.load("default");
    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->output("out-main").listen);
    // Strips at the defaults are not written out.
    REQUIRE(loaded->outputs.empty());
}

TEST_CASE("AudioMixStore forgets removed stacks, outputs and libraries", "[library][audio]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const AudioMixStore store(tmp.path().toStdString());

    LibraryMix mix;
    mix.stacks["main"] = {-6.0, false, false};
    mix.stacks["titles"] = {-3.0, true, false};
    mix.outputs["out-main"] = {0.0, false, true};
    mix.outputs["out-stage"] = {-12.0, false, false};
    REQUIRE(store.save("default", mix));
    REQUIRE(store.save("kids", mix));

    REQUIRE(store.remove_stack("default", "titles"));
    REQUIRE(store.remove_output("default", "out-stage"));
    auto loaded = store.load("default");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stacks.size() == 1);
    REQUIRE(loaded->stacks.count("main") == 1);
    REQUIRE(loaded->outputs.size() == 1);
    REQUIRE(loaded->outputs.count("out-main") == 1);
    REQUIRE(store.load("kids") == mix);

    // Removing what is not there changes nothing, and does not make an
    // unknown library known (first use still applies to it).
    REQUIRE(store.remove_stack("default", "never-existed"));
    REQUIRE(store.remove_output("unseen", "out-main"));
    REQUIRE_FALSE(store.load("unseen").has_value());

    REQUIRE(store.remove_library("kids"));
    REQUIRE_FALSE(store.load("kids").has_value());
    REQUIRE(store.load("default").has_value());
    REQUIRE(store.remove_library("never-existed"));
}

TEST_CASE("AudioMixStore reads a damaged file as nothing kept", "[library][audio]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const AudioMixStore store(tmp.path().toStdString());
    {
        std::ofstream out(store.file());
        out << "{ not json";
    }
    REQUIRE_FALSE(store.load("default").has_value());

    // And the next save replaces it with a good one.
    LibraryMix mix;
    mix.outputs["out-main"] = {0.0, false, true};
    REQUIRE(store.save("default", mix));
    REQUIRE(store.load("default") == mix);

    const auto doc = json::parse(std::ifstream(store.file()));
    REQUIRE(doc.at("version") == 1);
    REQUIRE(doc.at("libraries").at("default").at("outputs").at("out-main").at("listen") == true);
}
