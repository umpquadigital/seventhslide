// The mixer's plan, made on the changing thread and swapped in between
// blocks: a fade-out still to play is heard through any change made before
// its block, a strip leaving the routing goes quiet, a change or fade made
// while a block plays takes effect from the next block, levels a block moves
// on are kept through a plan made beside it, what the plan in names (meters,
// a fade-out, a sink) is played by it until the swap and not after, and
// changes nonstop beside the audio thread never let it touch a source or
// sink once removed.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "test_wait.hpp"

#include <seventhslide/audio/audio_source.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/media/audio_levels.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace audio = seventhslide::audio;
namespace library = seventhslide::library;
namespace media = seventhslide::media;
using audio::k_block_frames;
using audio::k_channels;
using Catch::Approx;
using seventhslide::test::wait_until;

namespace {

constexpr std::size_t k_block_samples = k_block_frames * k_channels;

/// Writes a constant frame and counts its reads. ``gone`` marks it removed,
/// so a read after that counts as late; ``gate`` holds a read until opened.
class TestSource final : public audio::AudioSource {
public:
    TestSource(float left, float right) : left_(left), right_(right) {}
    void read(float* out, std::size_t frames) override
    {
        if (gone.load()) {
            ++late_reads;
        }
        if (gate != nullptr) {
            gate->inside = true;
            while (!gate->open.load()) {
                std::this_thread::yield();
            }
        }
        ++reads;
        for (std::size_t f = 0; f < frames; ++f) {
            out[f * k_channels] = left_;
            out[f * k_channels + 1] = right_;
        }
    }
    struct Gate {
        std::atomic<bool> inside{false};
        std::atomic<bool> open{false};
    };
    Gate* gate = nullptr;
    std::atomic<int> reads{0};
    std::atomic<bool> gone{false};
    std::atomic<int> late_reads{0};

private:
    float left_;
    float right_;
};

/// Keeps the last block an output sends it, and counts them.
class TestSink final : public audio::OutputAudioSink {
public:
    void push(const float* frames, std::size_t count) override
    {
        if (gone.load()) {
            ++late_pushes;
        }
        last.assign(frames, frames + count * k_channels);
        ++pushes;
    }
    std::vector<float> last;
    std::atomic<int> pushes{0};
    std::atomic<bool> gone{false};
    std::atomic<int> late_pushes{0};
};

/// One block through the mixer; the Listen mix.
std::vector<float> mix_block(audio::Mixer& mixer)
{
    std::vector<float> listen(k_block_samples, 0.0f);
    mixer.process(listen.data(), k_block_frames);
    return listen;
}

/// How much of a removed source's last block is left at frame ``f``.
float fade(std::size_t f)
{
    return 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block_frames);
}

bool all_are(const std::vector<float>& frames, float value)
{
    return !frames.empty() && std::ranges::all_of(frames, [value](float v) { return v == value; });
}

/// Every frame of ``frames`` is ``steady`` plus ``fading`` faded out.
void require_fading(const std::vector<float>& frames, float steady, float fading)
{
    REQUIRE(frames.size() == k_block_samples);
    for (std::size_t f = 0; f < k_block_frames; ++f) {
        CAPTURE(f);
        REQUIRE(frames[f * k_channels] == Approx(steady + fading * fade(f)).margin(1e-6));
        REQUIRE(frames[f * k_channels + 1] == Approx(steady + fading * fade(f)).margin(1e-6));
    }
}

} // namespace

TEST_CASE("Mixer: a fade-out still to play is heard through a change made before its block",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    TestSource video(0.5f, 0.5f);
    TestSource bed(0.25f, 0.25f);
    TestSource next(0.125f, 0.125f);
    TestSink main;
    TestSink thirds;
    mixer.set_strip(audio::StripKind::Stack, "a", library::ChannelMix{0.0, false, true});
    mixer.set_routing({{"a"}, {{"main", {"a"}}}});
    mixer.add_source("a", &video, "video");
    mixer.add_source("a", &bed, "audio");
    mixer.add_sink("main", &main);
    mix_block(mixer);

    // Removed, then an output added that leaves video out and another video
    // put on the stack, all before the next block: that block still fades
    // the first video out, where video is carried, beside the second.
    mixer.remove_source(&video);
    mixer.set_routing({{"a"}, {{"main", {"a"}}, {"thirds", {"a"}, {"video"}}}});
    mixer.add_sink("thirds", &thirds);
    mixer.add_source("a", &next, "video");
    const auto heard = mix_block(mixer);
    require_fading(heard, 0.375f, 0.5f);
    require_fading(main.last, 0.375f, 0.5f);
    REQUIRE(all_are(thirds.last, 0.25f));
    REQUIRE(video.reads == 2);
    REQUIRE(all_are(mix_block(mixer), 0.375f));
    REQUIRE(all_are(main.last, 0.375f));

    // Two of a type removed before one block fade out together.
    mixer.add_source("a", &video, "video");
    REQUIRE(all_are(mix_block(mixer), 0.875f));
    mixer.remove_source(&video);
    mixer.remove_source(&next);
    require_fading(mix_block(mixer), 0.25f, 0.625f);
    REQUIRE(all_are(thirds.last, 0.25f));
    REQUIRE(all_are(mix_block(mixer), 0.25f));

    mixer.remove_source(&bed);
    mixer.remove_sink(&main);
    mixer.remove_sink(&thirds);
}

TEST_CASE("Mixer: a strip that leaves the routing comes back quiet, its fade-out dropped",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink sink;
    mixer.set_strip(audio::StripKind::Stack, "a", library::ChannelMix{0.0, false, true});
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &sink);
    mix_block(mixer);
    REQUIRE(mixer.meter(audio::StripKind::Stack, "a").level_db[0] == Approx(-6.02).margin(0.01));
    REQUIRE(mixer.meter(audio::StripKind::Output, "o1").level_db[0] == Approx(-6.02).margin(0.01));

    // Its fade-out still to play when the stack and output leave.
    mixer.remove_source(&source);
    mixer.set_routing({});
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    for (const auto kind : {audio::StripKind::Stack, audio::StripKind::Output}) {
        const auto reading = mixer.meter(kind, kind == audio::StripKind::Stack ? "a" : "o1");
        for (int c = 0; c < k_channels; ++c) {
            REQUIRE(std::isinf(reading.level_db[c]));
            REQUIRE(std::isinf(reading.peak_hold_db[c]));
        }
    }
    REQUIRE(all_are(mix_block(mixer), 0.0f));
    REQUIRE(all_are(sink.last, 0.0f));
    REQUIRE(std::isinf(mixer.meter(audio::StripKind::Stack, "a").level_db[0]));
    mixer.remove_sink(&sink);
}

TEST_CASE("Mixer: a change made while a block plays takes effect from the next block",
          "[audio][mixer]")
{
    // The audio thread is held in a block (in stack a's source's read) while
    // the window's thread makes each change. Stack b's video has only its
    // fade-out left, which that block plays. A change that plans makes its
    // plan before the block is let go, with that fade-out still to play; the
    // block plays it, and the plan swapped in after plays it as silence.
    struct Rig {
        audio::Mixer mixer;
        TestSource on_a{0.5f, 0.5f};
        TestSource video{0.25f, 0.25f};
        TestSource bed{0.125f, 0.125f};
        TestSource page{0.0625f, 0.0625f};
        TestSink o1;
        TestSink o2;
    };
    struct Case {
        const char* name;
        bool plans;
        std::function<void(Rig&)> change;
        /// The block after the change: its Listen mix, then o1's and o2's.
        std::function<void(Rig&, const std::vector<float>&)> after;
    };
    const auto steady = [](float listen, float o1, float o2) {
        return [=](Rig& r, const std::vector<float>& heard) {
            REQUIRE(all_are(heard, listen));
            REQUIRE(all_are(r.o1.last, o1));
            REQUIRE(all_are(r.o2.last, o2));
        };
    };
    const std::vector<Case> cases = {
        {"routing", true,
         [](Rig& r) {
             r.mixer.set_routing({{"a", "b", "c"}, {{"o1", {"b"}}, {"o2", {"a", "b"}}}});
         },
         steady(0.125f, 0.125f, 0.625f)},
        {"a source removed", true, [](Rig& r) { r.mixer.remove_source(&r.bed); },
         [](Rig& r, const std::vector<float>& heard) {
             require_fading(heard, 0.0f, 0.125f);
             require_fading(r.o1.last, 0.5f, 0.125f);
             require_fading(r.o2.last, 0.0f, 0.125f);
         }},
        {"a source added", true, [](Rig& r) { r.mixer.add_source("b", &r.page, "web"); },
         [&steady](Rig& r, const std::vector<float>& heard) {
             REQUIRE(r.page.reads == 1);
             steady(0.1875f, 0.6875f, 0.1875f)(r, heard);
         }},
        {"a source moved", true, [](Rig& r) { r.mixer.move_source(&r.bed, "a"); },
         steady(0.0f, 0.625f, 0.0f)},
        {"a sink removed", true, [](Rig& r) { r.mixer.remove_sink(&r.o2); },
         [](Rig& r, const std::vector<float>& heard) {
             REQUIRE(all_are(heard, 0.125f));
             REQUIRE(all_are(r.o1.last, 0.625f));
             REQUIRE(r.o2.pushes == 2);
         }},
        {"a strip set", false,
         [](Rig& r) {
             r.mixer.set_strip(audio::StripKind::Output, "o1", library::ChannelMix{-6.0, false, true});
         },
         [](Rig& r, const std::vector<float>& heard) {
             // Ramped across this block.
             const float o1 = 0.625f * static_cast<float>(media::db_to_gain(-6.0));
             const std::size_t end = (k_block_frames - 1) * k_channels;
             REQUIRE(r.o1.last[0] < 0.625f);
             REQUIRE(r.o1.last[end] == Approx(o1));
             REQUIRE(heard[end] == Approx(0.125f + o1));
         }},
        {"a level set", false, [](Rig& r) { r.mixer.set_source_level(&r.bed, 0.5f); },
         steady(0.0625f, 0.5625f, 0.0625f)},
        {"a fade on an output routed", false,
         [](Rig& r) { r.mixer.fade_source(&r.bed, "o2", 0.0f, std::chrono::milliseconds(0)); },
         steady(0.125f, 0.625f, 0.0f)},
        {"a fade on an output not routed", true,
         [](Rig& r) { r.mixer.fade_source(&r.bed, "o3", 0.0f, std::chrono::milliseconds(0)); },
         [&steady](Rig& r, const std::vector<float>& heard) {
             steady(0.125f, 0.625f, 0.125f)(r, heard);
             REQUIRE(r.mixer.source_level(&r.bed, "o3") == 0.0f);
         }},
    };
    for (const auto& c : cases) {
        CAPTURE(c.name);
        Rig r;
        r.mixer.set_strip(audio::StripKind::Stack, "b", library::ChannelMix{0.0, false, true});
        r.mixer.set_routing({{"a", "b"}, {{"o1", {"a", "b"}}, {"o2", {"b"}}}});
        r.mixer.add_source("a", &r.on_a, "audio");
        r.mixer.add_source("b", &r.video, "video");
        r.mixer.add_source("b", &r.bed, "audio");
        r.mixer.add_sink("o1", &r.o1);
        r.mixer.add_sink("o2", &r.o2);
        REQUIRE(all_are(mix_block(r.mixer), 0.375f));
        REQUIRE(all_are(r.o1.last, 0.875f));
        r.mixer.remove_source(&r.video);

        std::atomic<int> plans{0};
        r.mixer.set_planned_hook_for_tests([&plans] { ++plans; });
        TestSource::Gate gate;
        r.on_a.gate = &gate;
        auto held = std::vector<float>(k_block_samples, 0.0f);
        std::thread audio_thread([&] { r.mixer.process(held.data(), k_block_frames); });
        REQUIRE(wait_until([&] { return gate.inside.load(); }));
        std::atomic<bool> changing{false};
        std::thread window([&] {
            changing = true;
            c.change(r);
        });
        // A change that plans has made its plan while the block is held.
        REQUIRE(wait_until([&] { return c.plans ? plans.load() > 0 : changing.load(); }));
        gate.open = true;
        audio_thread.join();
        window.join();
        r.on_a.gate = nullptr;
        r.mixer.set_planned_hook_for_tests({});
        REQUIRE(plans == (c.plans ? 1 : 0));

        // The block held went on as it began, the video fading out.
        require_fading(held, 0.125f, 0.25f);
        require_fading(r.o1.last, 0.625f, 0.25f);
        REQUIRE(r.o2.pushes == 2);
        require_fading(r.o2.last, 0.125f, 0.25f);

        c.after(r, mix_block(r.mixer));
        for (auto* source : {&r.on_a, &r.bed, &r.page}) {
            r.mixer.remove_source(source);
        }
        r.mixer.remove_sink(&r.o1);
        r.mixer.remove_sink(&r.o2);
    }
}

namespace {

/// Every frame of ``frames`` is ``value`` times a gain moving in a straight
/// line from ``from`` to ``to`` across the block.
void require_ramp(const std::vector<float>& frames, float value, float from, float to)
{
    REQUIRE(frames.size() == k_block_samples);
    for (std::size_t f = 0; f < k_block_frames; ++f) {
        CAPTURE(f);
        const float g = from + (to - from) * static_cast<float>(f + 1) / static_cast<float>(k_block_frames);
        REQUIRE(frames[f * k_channels] == Approx(value * g).margin(1e-5));
        REQUIRE(frames[f * k_channels + 1] == Approx(value * g).margin(1e-5));
    }
}

/// Holds the audio thread in a block, inside ``source``'s read, until let go.
class HeldBlock {
public:
    HeldBlock(audio::Mixer& mixer, TestSource& source)
        : source_(source)
        , listen_(k_block_samples, 0.0f)
    {
        source_.gate = &gate_;
        thread_ = std::thread([&mixer, this] { mixer.process(listen_.data(), k_block_frames); });
        const bool held = wait_until([this] { return gate_.inside.load(); });
        if (!held) {
            let_go();  // a thread left running would end the test run
        }
        REQUIRE(held);
    }
    HeldBlock(const HeldBlock&) = delete;
    HeldBlock& operator=(const HeldBlock&) = delete;
    ~HeldBlock() { let_go(); }

    /// Lets the block finish; its Listen mix.
    const std::vector<float>& let_go()
    {
        if (thread_.joinable()) {
            gate_.open = true;
            thread_.join();
            source_.gate = nullptr;
        }
        return listen_;
    }

private:
    TestSource& source_;
    TestSource::Gate gate_;
    std::vector<float> listen_;
    std::thread thread_;
};

} // namespace

TEST_CASE("Mixer: levels moved on by blocks played while a plan is made are kept through its swap",
          "[audio][mixer][fades]")
{
    // 40 ms is four blocks, so a fade moves a quarter of the way a block.
    constexpr auto k_four_blocks = std::chrono::milliseconds(40);
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    TestSink o2;
    TestSink o3;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}, {"o2", {"a"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    mixer.add_sink("o3", &o3);
    // o2 and every output not named yet fade down over four blocks.
    mixer.fade_source(&source, "o2", 0.0f, k_four_blocks);
    mixer.fade_source(&source, {}, 0.0f, k_four_blocks);
    mix_block(mixer);
    require_ramp(o2.last, 0.5f, 1.0f, 0.75f);

    // o3 routed, and a block played once the plan is made, before its swap.
    int plans = 0;
    mixer.set_planned_hook_for_tests([&] {
        if (++plans == 1) {
            mix_block(mixer);
        }
    });
    mixer.set_routing({{"a"}, {{"o1", {"a"}}, {"o2", {"a"}}, {"o3", {"a"}}}});
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    require_ramp(o2.last, 0.5f, 0.75f, 0.5f);
    REQUIRE(o3.pushes == 0);

    // o2 carries on from where that block left it, and o3 starts where the
    // outputs not named had got to by the swap.
    mix_block(mixer);
    REQUIRE(all_are(o1.last, 0.5f));
    require_ramp(o2.last, 0.5f, 0.5f, 0.25f);
    require_ramp(o3.last, 0.5f, 0.5f, 0.25f);
    REQUIRE(mixer.source_level(&source, "o3").value() == Approx(0.25f).margin(1e-5));
    REQUIRE(mixer.source_level(&source, {}).value() == 1.0f);

    mixer.remove_source(&source);
    for (auto* sink : {&o1, &o2, &o3}) {
        mixer.remove_sink(sink);
    }
}

TEST_CASE("Mixer: a fade made while a block plays takes effect from the next, through a plan made meanwhile",
          "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    TestSource clock(0.0f, 0.0f);
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    TestSink o2;
    TestSink extra;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}, {"o2", {"a"}}}});
    mixer.add_source("a", &clock);
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    mixer.fade_source(&source, "o2", 0.0f, std::chrono::milliseconds(40));
    mix_block(mixer);

    // While a block plays, the window's thread adds a sink, whose plan waits
    // for that block, and a fade made meanwhile waits for the plan.
    std::atomic<int> plans{0};
    mixer.set_planned_hook_for_tests([&plans] { ++plans; });
    {
        HeldBlock held(mixer, clock);
        std::thread joining([&] { mixer.add_sink("o1", &extra); });
        REQUIRE(wait_until([&] { return plans.load() > 0; }));
        std::thread fade([&] { mixer.fade_source(&source, "o1", 0.0f, std::chrono::milliseconds(0)); });
        held.let_go();
        joining.join();
        fade.join();
    }
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    // The block held played on as it began.
    REQUIRE(all_are(o1.last, 0.5f));
    require_ramp(o2.last, 0.5f, 0.75f, 0.5f);
    REQUIRE(extra.pushes == 0);

    // The next has the sink, the fade, and o2 going on down.
    mix_block(mixer);
    REQUIRE(all_are(o1.last, 0.0f));
    REQUIRE(extra.pushes == 1);
    REQUIRE(all_are(extra.last, 0.0f));
    require_ramp(o2.last, 0.5f, 0.5f, 0.25f);

    // Every level set at once while a block plays: that block plays on, the
    // next has it.
    {
        HeldBlock held(mixer, clock);
        std::thread set([&] { mixer.set_source_level(&source, 1.0f); });
        held.let_go();
        set.join();
    }
    REQUIRE(all_are(o1.last, 0.0f));
    require_ramp(o2.last, 0.5f, 0.25f, 0.0f);
    mix_block(mixer);
    for (const auto* sink : {&o1, &o2, &extra}) {
        REQUIRE(all_are(sink->last, 0.5f));
    }
    REQUIRE(mixer.source_level(&source, {}) == 1.0f);

    mixer.remove_source(&clock);
    mixer.remove_source(&source);
    for (auto* sink : {&o1, &o2, &extra}) {
        mixer.remove_sink(sink);
    }
}

TEST_CASE("Mixer: a source removed mid-fade fades out from where blocks played meanwhile left it",
          "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    TestSink o2;
    mixer.set_strip(audio::StripKind::Stack, "a", library::ChannelMix{0.0, false, true});
    mixer.set_routing({{"a"}, {{"o1", {"a"}}, {"o2", {"a"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    // o1 down over four blocks, o2 to three quarters at once.
    mixer.fade_source(&source, "o1", 0.0f, std::chrono::milliseconds(40));
    mixer.fade_source(&source, "o2", 0.75f, std::chrono::milliseconds(0));
    mix_block(mixer);
    require_ramp(o1.last, 0.5f, 1.0f, 0.75f);

    // Removed, and a block played once the plan is made, before its swap:
    // the fade-out takes each output's level as that block leaves it, and
    // the block after the one it read.
    int plans = 0;
    mixer.set_planned_hook_for_tests([&] {
        if (++plans == 1) {
            mix_block(mixer);
        }
    });
    mixer.remove_source(&source);
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    require_ramp(o1.last, 0.5f, 0.75f, 0.5f);
    REQUIRE(source.reads == 3);

    // o1 had got to half, o2 was at three quarters, and the strip hears the
    // loudest.
    const auto heard = mix_block(mixer);
    require_fading(o1.last, 0.0f, 0.25f);
    require_fading(o2.last, 0.0f, 0.375f);
    require_fading(heard, 0.0f, 0.375f);
    REQUIRE(source.reads == 3);
    REQUIRE(all_are(mix_block(mixer), 0.0f));
    REQUIRE(all_are(o1.last, 0.0f));

    mixer.remove_sink(&o1);
    mixer.remove_sink(&o2);
}

TEST_CASE("Mixer: a strip the routing leaves is quiet from its swap, though a block meters it before",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    mixer.set_routing({{"a", "b"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mix_block(mixer);
    REQUIRE_FALSE(std::isinf(mixer.meter(audio::StripKind::Stack, "a").level_db[0]));

    // a and o1 leave the routing, and a block played once the plan is made,
    // from the plan still in, meters them both again.
    int plans = 0;
    mixer.set_planned_hook_for_tests([&] {
        if (++plans == 1) {
            mix_block(mixer);
        }
    });
    mixer.set_routing({{"b"}, {}});
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    REQUIRE(o1.pushes == 2);

    // Back in the routing, before any block: both quiet.
    mixer.set_routing({{"a", "b"}, {{"o1", {"a"}}}});
    for (const auto& [kind, id] : {std::pair{audio::StripKind::Stack, "a"},
                                   std::pair{audio::StripKind::Output, "o1"}}) {
        CAPTURE(id);
        const auto reading = mixer.meter(kind, id);
        REQUIRE(std::isinf(reading.level_db[0]));
        REQUIRE(std::isinf(reading.peak_hold_db[0]));
    }

    mixer.remove_source(&source);
    mixer.remove_sink(&o1);
}

TEST_CASE("Mixer: a fade-out its stack's leaving drops still plays from the plan in until the swap",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    mixer.set_strip(audio::StripKind::Stack, "a", library::ChannelMix{0.0, false, true});
    mixer.set_routing({{"a", "b"}, {{"o1", {"a", "b"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mix_block(mixer);
    mixer.remove_source(&source);

    // a leaves the routing, so the plan made drops the fade-out still to
    // play; a block played before its swap plays it from the plan in.
    int plans = 0;
    std::vector<float> heard;
    mixer.set_planned_hook_for_tests([&] {
        if (++plans == 1) {
            heard = mix_block(mixer);
        }
    });
    mixer.set_routing({{"b"}, {{"o1", {"b"}}}});
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    require_fading(heard, 0.0f, 0.5f);
    require_fading(o1.last, 0.0f, 0.5f);

    REQUIRE(all_are(mix_block(mixer), 0.0f));
    REQUIRE(all_are(o1.last, 0.0f));
    mixer.remove_sink(&o1);
}

TEST_CASE("Mixer: a sink is pushed to by the plan in until its removal's swap, and never after",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    TestSource source(0.5f, 0.5f);
    TestSink o1;
    TestSink extra;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &source);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o1", &extra);
    mix_block(mixer);
    REQUIRE(extra.pushes == 1);

    int plans = 0;
    mixer.set_planned_hook_for_tests([&] {
        if (++plans == 1) {
            mix_block(mixer);
        }
    });
    mixer.remove_sink(&extra);
    mixer.set_planned_hook_for_tests({});
    REQUIRE(plans == 1);
    REQUIRE(extra.pushes == 2);
    extra.gone = true;

    mix_block(mixer);
    REQUIRE(o1.pushes == 3);
    REQUIRE(extra.pushes == 2);
    REQUIRE(extra.late_pushes == 0);

    mixer.remove_source(&source);
    mixer.remove_sink(&o1);
}

namespace {

constexpr std::size_t k_sources = 6;
constexpr std::size_t k_sinks = 5;

const std::vector<std::string>& stack_ids()
{
    static const std::vector<std::string> ids = {"a", "b", "c", audio::cue_strip_id("a"),
                                                 audio::cue_strip_id("b"), "stray"};
    return ids;
}

const std::vector<std::string>& output_ids()
{
    static const std::vector<std::string> ids = {"o1", "o2", "o3", "unrouted"};
    return ids;
}

const std::vector<std::string>& types()
{
    static const std::vector<std::string> ids = {"", "audio", "video", "web"};
    return ids;
}

std::vector<audio::Routing> routings()
{
    const auto cue_a = audio::cue_strip_id("a");
    const auto cue_b = audio::cue_strip_id("b");
    return {
        {},
        {{"a"}, {{"o1", {"a"}}}},
        {{"a", "b"}, {{"o1", {"a"}}, {"o2", {"a", "b"}}}},
        {{"a", "b", "c"},
         {{"o1", {"a", "c"}, {"video"}}, {"o2", {"b", "a"}}, {"o3", {"c", "nope"}, {"audio", "web"}}},
         {cue_a, cue_b}},
        {{"b", "a"}, {{"o2", {"a", cue_a}}, {"o1", {"b"}, {""}}}, {cue_a, "a"}},
        {{"c"}, {{"o3", {"c"}}, {"o1", {}}, {"o2", {"c"}, {"video"}}}, {cue_b}},
        {{}, {{"o1", {"a"}}}, {cue_a}},
    };
}

/// One change from the window's thread, or a look at a meter and strip.
struct SetRouting {
    std::size_t routing;
};
struct AddSource {
    std::size_t source;
    std::string stack;
    std::string type;
};
struct RemoveSource {
    std::size_t source;
};
struct MoveSource {
    std::size_t source;
    std::string stack;
};
struct AddSink {
    std::size_t sink;
    std::string output;
};
struct RemoveSink {
    std::size_t sink;
};
struct SetStrip {
    audio::StripKind kind;
    std::string id;
    library::ChannelMix mix;
};
struct Look {
    audio::StripKind kind;
    std::string id;
};
/// ``output`` empty for the level of outputs not named.
struct FadeSource {
    std::size_t source;
    std::string output;
    float target;
    std::chrono::milliseconds over;
};
struct SetSourceLevel {
    std::size_t source;
    float level;
};
struct ReadSourceLevel {
    std::size_t source;
    std::string output;
};
using Step = std::variant<SetRouting, AddSource, RemoveSource, MoveSource, AddSink, RemoveSink, SetStrip,
                          Look, FadeSource, SetSourceLevel, ReadSourceLevel>;

/// ``steps`` steps from ``seed``, as an operator and the app might make them:
/// changes of every kind, in any order, then every source and sink removed.
std::vector<Step> script(std::uint32_t seed, int steps)
{
    std::mt19937 random(seed);
    const auto pick = [&](std::size_t n) { return static_cast<std::size_t>(random() % n); };
    const auto routing_count = routings().size();
    std::array<bool, k_sources> placed{};
    std::array<bool, k_sinks> joined{};
    const std::array<double, 6> gains = {0.0, -6.0, -20.5, -std::numeric_limits<double>::infinity(),
                                         -95.0, 3.0};
    std::vector<Step> out;
    for (int i = 0; i < steps; ++i) {
        const auto roll = pick(60);
        if (roll < 7) {
            out.emplace_back(SetRouting{pick(routing_count)});
        } else if (roll < 15) {
            const auto s = pick(k_sources);
            if (!placed[s]) {
                placed[s] = true;
                out.emplace_back(AddSource{s, stack_ids()[pick(stack_ids().size())], types()[pick(types().size())]});
            }
        } else if (roll < 23) {
            // Now and then one that is not there.
            const auto s = pick(k_sources);
            placed[s] = false;
            out.emplace_back(RemoveSource{s});
        } else if (roll < 27) {
            out.emplace_back(MoveSource{pick(k_sources), stack_ids()[pick(stack_ids().size())]});
        } else if (roll < 31) {
            const auto s = pick(k_sinks);
            if (!joined[s]) {
                joined[s] = true;
                out.emplace_back(AddSink{s, output_ids()[pick(output_ids().size())]});
            }
        } else if (roll < 34) {
            const auto s = pick(k_sinks);
            joined[s] = false;
            out.emplace_back(RemoveSink{s});
        } else if (roll < 40) {
            const bool stack = pick(2) == 0;
            // A strip never seen, now and then.
            auto id = stack ? stack_ids()[pick(stack_ids().size())] : output_ids()[pick(output_ids().size())];
            if (pick(10) == 0) {
                id += std::to_string(i);
            }
            out.emplace_back(SetStrip{stack ? audio::StripKind::Stack : audio::StripKind::Output, id,
                                      library::ChannelMix{gains[pick(gains.size())], pick(4) == 0,
                                                          pick(3) == 0}});
        } else if (roll < 45) {
            const bool stack = pick(2) == 0;
            out.emplace_back(Look{stack ? audio::StripKind::Stack : audio::StripKind::Output,
                                  stack ? stack_ids()[pick(stack_ids().size())]
                                        : output_ids()[pick(output_ids().size())]});
        } else if (roll < 52) {
            // On an output routed or not, now and then one never seen (which
            // plans), or the outputs not named; at once or over a few blocks.
            std::string output;
            if (const auto which = pick(output_ids().size() + 2); which < output_ids().size()) {
                output = output_ids()[which];
            } else if (which == output_ids().size()) {
                output = "faded" + std::to_string(i % 7);
            }
            out.emplace_back(FadeSource{pick(k_sources), output, static_cast<float>(pick(5)) / 4.0f,
                                        std::chrono::milliseconds(pick(3) == 0 ? 0 : 10 * pick(30))});
        } else if (roll < 55) {
            out.emplace_back(SetSourceLevel{pick(k_sources), static_cast<float>(pick(3)) / 2.0f});
        } else {
            std::string output;
            if (const auto which = pick(output_ids().size() + 1); which < output_ids().size()) {
                output = output_ids()[which];
            }
            out.emplace_back(ReadSourceLevel{pick(k_sources), output});
        }
    }
    // Everything leaves at the end, as the app's sources and sinks do.
    for (std::size_t s = 0; s < k_sources; ++s) {
        out.emplace_back(RemoveSource{s});
    }
    for (std::size_t s = 0; s < k_sinks; ++s) {
        out.emplace_back(RemoveSink{s});
    }
    return out;
}

} // namespace

TEST_CASE("Mixer: changes on the window's thread while the audio thread mixes",
          "[audio][mixer]")
{
    // Blocks run one after another, a moment apart, while the window's
    // thread changes everything, fades sources and reads meters and levels.
    // Once remove_source or remove_sink returns, the audio thread never reads
    // that source or pushes to that sink again; a level set at once reads
    // back as set, whatever blocks and plans came between; and blocks go on
    // being mixed throughout.
    audio::Mixer mixer;
    std::vector<std::unique_ptr<TestSource>> sources;
    for (std::size_t s = 0; s < k_sources; ++s) {
        sources.push_back(std::make_unique<TestSource>(0.125f * static_cast<float>(s), -0.0625f));
    }
    std::vector<std::unique_ptr<TestSink>> sinks;
    for (std::size_t s = 0; s < k_sinks; ++s) {
        sinks.push_back(std::make_unique<TestSink>());
    }
    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0};
    std::thread audio_thread([&] {
        std::vector<float> listen(k_block_samples);
        while (!stop.load()) {
            mixer.process(listen.data(), k_block_frames);
            ++blocks;
            // A sound card's callback leaves the lock between blocks for
            // longer than the block takes; with no gap at all a change can
            // wait minutes for the lock under the sanitizers.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    const auto steps = script(99, 9000);
    const auto routing = routings();
    std::array<bool, k_sources> added{};
    std::size_t levels_read = 0;
    for (const auto& step : steps) {
        std::visit(
            [&](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, SetRouting>) {
                    mixer.set_routing(routing[s.routing]);
                } else if constexpr (std::is_same_v<S, AddSource>) {
                    sources[s.source]->gone = false;
                    mixer.add_source(s.stack, sources[s.source].get(), s.type);
                    added[s.source] = true;
                } else if constexpr (std::is_same_v<S, RemoveSource>) {
                    mixer.remove_source(sources[s.source].get());
                    sources[s.source]->gone = true;
                    added[s.source] = false;
                } else if constexpr (std::is_same_v<S, MoveSource>) {
                    mixer.move_source(sources[s.source].get(), s.stack);
                } else if constexpr (std::is_same_v<S, AddSink>) {
                    sinks[s.sink]->gone = false;
                    mixer.add_sink(s.output, sinks[s.sink].get());
                } else if constexpr (std::is_same_v<S, RemoveSink>) {
                    mixer.remove_sink(sinks[s.sink].get());
                    sinks[s.sink]->gone = true;
                } else if constexpr (std::is_same_v<S, SetStrip>) {
                    mixer.set_strip(s.kind, s.id, s.mix);
                } else if constexpr (std::is_same_v<S, Look>) {
                    (void)mixer.meter(s.kind, s.id);
                    (void)mixer.strip(s.kind, s.id);
                } else if constexpr (std::is_same_v<S, FadeSource>) {
                    auto* source = sources[s.source].get();
                    mixer.fade_source(source, s.output, s.target, s.over);
                    if (added[s.source] && !s.output.empty() && s.over.count() == 0) {
                        REQUIRE(mixer.source_level(source, s.output) == s.target);
                    }
                } else if constexpr (std::is_same_v<S, SetSourceLevel>) {
                    auto* source = sources[s.source].get();
                    mixer.set_source_level(source, s.level);
                    if (added[s.source]) {
                        REQUIRE(mixer.source_level(source, "o1") == s.level);
                        REQUIRE(mixer.source_level(source, {}) == s.level);
                    }
                } else {
                    const auto level = mixer.source_level(sources[s.source].get(), s.output);
                    REQUIRE(level.has_value() == added[s.source]);
                    if (level) {
                        REQUIRE(*level >= 0.0f);
                        REQUIRE(*level <= 1.0f);
                        ++levels_read;
                    }
                }
            },
            step);
    }
    const long at_end = blocks.load();
    REQUIRE(wait_until([&] { return blocks.load() > at_end + 10; }));
    stop = true;
    audio_thread.join();
    for (const auto& source : sources) {
        REQUIRE(source->late_reads == 0);
    }
    for (const auto& sink : sinks) {
        REQUIRE(sink->late_pushes == 0);
    }
    REQUIRE(blocks.load() > 10);
    // The script reads levels of sources it has added.
    REQUIRE(levels_read > 100);
}
