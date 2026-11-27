#pragma once

// The Audio Mixer's sound: each stack's sources through its strip, each
// output's mix of the stacks it shows through its strip, out to the output's
// sinks, and the strips on Listen summed for the headphones.
//
//   stack sources -> fader -> (meter) -> mute -+-> each output it feeds:
//                                              |     each source x its level there
//                                              |     -> sum -> fader -> (meter) -> mute -> sinks
//                                              +-> Listen (if on)    output after its mute -> Listen (if on)
//
// A strip's meter reads after its fader and before its mute, as OBS's does:
// a muted strip still shows what it would send. Level, mute and Listen
// changes ramp across one block, so none clicks.
//
// Every source has a level on each output (``fade_source``), so one output
// can fade an item over two seconds while another cuts it: an item's fades
// are each output's own. A source nobody fades plays at full everywhere. The
// stack's strip, its meter and Listen, hears each source at the loudest any
// output carrying it does, so it is quiet only once every output is.
//
// Every source has a type of sound, and an output may leave types out. A stack
// strip's meter and Listen have all of its sound; an output sums the rest, so
// one type handing over to another on a stack is heard, fade-out included,
// only by the outputs carrying each.
//
// A stack with a Preview has a cue strip too (``Routing::cues``), for what is
// cued there: read, metered and heard on Listen as a stack's strip is, and
// summed into no output, whatever an output's stacks name. Its id is
// ``cue_strip_id`` of the stack, and it is a stack strip in every other way:
// its settings are set and kept by that id. No output carries it, so it
// hears a source at the level ``fade_source`` gives outputs it names none
// for.
//
// Seam: plain C++20 (no Qt).

#include "seventhslide/audio/audio_format.hpp"
#include "seventhslide/library/audio_mix_store.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::audio {

class AudioSource;

/// Where an output's mix goes: an NDI sender, a DeckLink card, a sound
/// device, a virtual microphone.
class OutputAudioSink {
public:
    virtual ~OutputAudioSink() = default;
    /// The output's mix for one block, interleaved stereo at ``k_sample_rate``.
    /// Called on the audio thread: must not block or allocate.
    virtual void push(const float* frames, std::size_t count) = 0;
};

/// Which stacks there are and which each output carries, in the Outputs
/// panel's order.
struct Routing {
    struct Output {
        std::string id;
        /// The stacks it shows (``model::output_enables_stack``).
        std::vector<std::string> stacks;
        /// The types of sound it does not carry from them (``add_source``).
        std::vector<std::string> left_out = {};

        [[nodiscard]] bool operator==(const Output&) const = default;
    };
    std::vector<std::string> stacks;
    std::vector<Output> outputs;
    /// The cue strips (``cue_strip_id``), which no output carries. An id that
    /// is also in ``stacks`` is the stack's, not a cue strip.
    std::vector<std::string> cues = {};

    [[nodiscard]] bool operator==(const Routing&) const = default;
};

/// The id of stack ``stack_id``'s cue strip: what its Preview's sound plays
/// on. No stack has it as its own id (``model::new_stack_id`` makes hex).
[[nodiscard]] std::string cue_strip_id(std::string_view stack_id);

enum class StripKind { Stack, Output };

/// A strip's meter, per side (left, right), in dBFS; -infinity for silence.
struct MeterReading {
    /// The level, falling back at OBS's rate after a peak.
    std::array<float, k_channels> level_db;
    /// The highest recent peak, held for a while (OBS's peak-hold line).
    std::array<float, k_channels> peak_hold_db;
};

/**
 * The mixer. Settings, routing, sources and sinks change from the app's
 * thread; ``process`` runs on the audio thread, and never allocates, frees or
 * waits on a plan being made. It may wait out the short work the app's side
 * does under ``block_mu_`` (below), which a plain mutex does not shield from
 * the app's thread being preempted while it holds it.
 *
 * What ``process`` walks is a plan: which strips there are in what order,
 * each strip's sounds and the block each is mixed in, what each output
 * carries at which level, and which sinks it feeds. A plan is made on the
 * app's thread, and once swapped in only its scratch blocks change. Two locks
 * keep the two threads apart:
 *
 * - ``change_mu_`` is the app's side. Every public call but ``process``
 *   holds it throughout, so changes come one at a time, and the plan in, the
 *   routing, the lists of sources, sinks and fade-outs and the strips' maps
 *   change only under it. A routing, source or sink change (and a fade for
 *   an output the routing does not name yet) makes a new plan under it
 *   alone: every allocation and free a change makes happens here, while
 *   blocks go on being mixed.
 * - ``block_mu_`` is the audio thread's. ``process`` holds it for one
 *   block's arithmetic. The app's side takes it, after ``change_mu_``, only
 *   for short work that neither allocates nor frees: a strip's new settings
 *   (``set_strip``), a source's levels set or read (``fade_source``,
 *   ``set_source_level``, ``source_level``), a meter copied (``meter``), and
 *   a new plan swapped in. The swap reads a removed source's last block into
 *   its fade-out with its levels as they are now, starts each level the plan
 *   brings in where its source's rest level is, swaps the plan pointer, and
 *   quiets the meters of the strips the plan in before routed and this one
 *   does not: loops over the levels and strips that change, and one block
 *   read.
 *
 * Everything ``process`` changes stays where it is while a plan points at
 * it, so a plan made beside running blocks may point at it, and nothing has
 * to be carried from one plan to the next at a swap. Strips are in maps that
 * are never erased from. A source is a node of its own, and its level on
 * each output sits in a deque that only grows; a source removed is freed
 * only once the plan naming it has been swapped out. A fade-out is a node of
 * its own too, made before the swap (so a removal allocates nothing under
 * the block's lock) and freed only once no plan names it. Each block moves
 * every level on, in place, through the pointers its plan holds; a fade
 * changes a level's target in place under ``block_mu_``, so it reaches the
 * next block whichever plan is in, and none is lost to a swap. Only levels
 * and fade-outs a plan brings into being are filled at its swap, since they
 * must start from where the blocks before it left their source.
 *
 * A fade-out's ``pending`` flag is the one thing a plan being made reads
 * that a block may change. A plan keeps the fade-outs still pending when it
 * is made and drops the rest; one a block plays before the swap stays in it
 * until the next plan, playing silence, as one played since the last change
 * always has. Old plans, sources and fade-outs are freed on the app's
 * thread, after it lets go of ``block_mu_``.
 */
class Mixer {
public:
    /// Meter ballistics, as OBS draws its meters: the level falls back at
    /// 23.53 dB/s ("fast" decay) and a peak is held for 20 s.
    static constexpr double k_meter_decay_db_per_sec = 23.53;
    static constexpr double k_peak_hold_sec = 20.0;

    Mixer();

    /// The stacks and outputs there are, and what each output carries.
    /// Strips keep their settings by id across changes.
    void set_routing(Routing routing);

    /// A strip's level, mute and Listen. Kept by id even for a strip the
    /// routing does not name yet.
    void set_strip(StripKind kind, std::string_view id, const library::ChannelMix& mix);
    [[nodiscard]] library::ChannelMix strip(StripKind kind, std::string_view id) const;

    /// ``source`` plays on stack ``stack_id`` (or the cue strip of that id)
    /// until removed. A stack may have several (an audio item and a video's
    /// sound). ``type`` is its type of sound, which an output's
    /// ``Routing::Output::left_out`` may name.
    void add_source(std::string_view stack_id, AudioSource* source, std::string_view type = {});
    /// Once this returns the audio thread will not read ``source`` again, so
    /// the caller may destroy it. It reads one last block of ``source`` here,
    /// which its stack plays fading out, to the outputs that carried it, so
    /// the sound does not stop with a click.
    void remove_source(AudioSource* source);
    /// ``source`` plays on stack ``stack_id`` (or a cue strip) from the next
    /// block on, without a break (a video's sound following the video to
    /// another stack, or from a stack's cue strip to the stack at a take). Its
    /// levels go with it.
    void move_source(AudioSource* source, std::string_view stack_id);

    /// Move ``source``'s level on output ``output_id`` to ``target`` (0 to 1)
    /// in a straight line over ``over``; at once for none. An empty
    /// ``output_id`` moves the level it plays at on every output it has no
    /// level of its own for (one that comes to carry it later starts from
    /// there), which is also what a strip no output carries it from hears.
    /// A source starts at 1 everywhere; nothing for a source not added.
    void fade_source(AudioSource* source,
                     std::string_view output_id,
                     float target,
                     std::chrono::milliseconds over);
    /// Every level ``source`` has, at ``level`` at once.
    void set_source_level(AudioSource* source, float level);
    /// ``source``'s level on output ``output_id`` now; with an empty
    /// ``output_id``, what its stack's strip hears it at (the loudest output
    /// carrying it). None for a source not added.
    [[nodiscard]] std::optional<float> source_level(const AudioSource* source,
                                                    std::string_view output_id) const;

    /// ``sink`` receives output ``output_id``'s mix until removed.
    void add_sink(std::string_view output_id, OutputAudioSink* sink);
    /// Once this returns the audio thread will not push to ``sink`` again.
    void remove_sink(OutputAudioSink* sink);

    /// A strip's meter now; silence for a strip the routing does not name.
    [[nodiscard]] MeterReading meter(StripKind kind, std::string_view id) const;

    /// For tests: ``hook`` runs on the changing thread each time a change has
    /// made a plan, before it takes the block's lock to swap it in, so a test
    /// may hold a block or play one there (``process``), as the audio thread
    /// may meanwhile. It runs under ``change_mu_``, so it may call only
    /// ``process``: any other call waits for itself. It must not throw. Empty
    /// to stop.
    void set_planned_hook_for_tests(std::function<void()> hook);

    /// Audio thread: mix the next ``frames`` frames (at most
    /// ``k_block_frames``), push each output's mix to its sinks, and write
    /// the Listen mix to ``listen`` (interleaved stereo, clipped to +-1).
    void process(float* listen, std::size_t frames);

private:
    /// A gain that moves from where it is to a new target across one block.
    struct Ramp {
        float current = 1.0f;
        float target = 1.0f;
    };
    /// A level a source plays at somewhere, moving toward its target by
    /// ``step`` a frame (``fade_source``). Under ``block_mu_``.
    struct Level {
        float current = 1.0f;
        float target = 1.0f;
        float step = 0.0f;
        /// Where it was and is across the block being mixed.
        float from = 1.0f;
        float to = 1.0f;

        void set(float to_target, std::chrono::milliseconds over) noexcept;
        void advance(std::size_t frames) noexcept;
    };
    /// ``mix`` is the app's side's; the ramps' currents and the meter are a
    /// block's, and the targets are set under ``block_mu_``.
    struct Strip {
        library::ChannelMix mix;
        Ramp fader;
        Ramp mute;
        Ramp listen;
        std::array<float, k_channels> level_db;
        std::array<float, k_channels> hold_db;
        std::array<double, k_channels> hold_age_sec{};
        Strip();
    };
    /// Never erased from, so a plan's pointer to a strip stays good.
    using Strips = std::map<std::string, Strip, std::less<>>;

    static void apply_settings(Strip& strip, const library::ChannelMix& mix, bool snap);
    static void meter_block(Strip& strip, const float* frames, std::size_t count);
    Strips& strips_of(StripKind kind) { return kind == StripKind::Stack ? stacks_ : outputs_; }
    [[nodiscard]] const Strips& strips_of(StripKind kind) const
    {
        return kind == StripKind::Stack ? stacks_ : outputs_;
    }

    /// A source on a stack. Its levels are a block's (``block_mu_``); the
    /// rest is the app's side's, and a block reads only ``source``.
    struct Placed {
        std::string stack_id;
        AudioSource* source;
        std::string type;
        /// Its level on an output it has none of its own for.
        Level rest;
        /// Its level on each output, by id: one for every output routed
        /// (``replan_locked``), and any ``fade_source`` named since. Only
        /// ever added to, so each level keeps its address.
        std::deque<std::pair<std::string, Level>> outputs;

        [[nodiscard]] Level* on(std::string_view output_id) noexcept;
        [[nodiscard]] const Level* on(std::string_view output_id) const noexcept;
    };
    /// What a source removed would have played next (one block), fading out
    /// across the next block on its stack, at the level each output carried
    /// it at as it went. Made before the swap that fills it; a block reads
    /// its sound and gains and clears ``pending``.
    struct Tail {
        std::string stack_id;
        std::string type;
        std::array<float, k_block_frames * k_channels> frames{};
        /// Its stack's strip's level for it, its level on outputs not named,
        /// and on each output, in the order of its source's levels.
        float stack_level = 1.0f;
        float rest = 1.0f;
        std::vector<std::pair<std::string, float>> outputs;
        /// Until a block plays it. Atomic so a plan being made may look
        /// without ``block_mu_``.
        std::atomic<bool> pending{true};

        /// Where its gain on output ``output_id`` is kept.
        [[nodiscard]] const float* on(std::string_view output_id) const noexcept;
    };

    // What process() walks. Blocks are numbered within ``Plan::blocks``.
    /// One source's sound on a stack, or one tail's: a block of its own.
    struct SoundPlan {
        Placed* placed;  ///< a source's
        Tail* tail;      ///< a tail's
        std::size_t block;
        /// The source's level on each output carrying it: the stack's strip
        /// hears it at the loudest (its ``rest`` level when none do).
        std::vector<const Level*> carried = {};
    };
    struct StackPlan {
        Strip* strip;
        std::vector<SoundPlan> sounds;
        std::size_t block;  // the sum of its sounds
    };
    /// A sound an output carries, at its level there.
    struct Carried {
        std::size_t block;
        const Level* level;  ///< a source's level there
        const float* gain;   ///< a tail's (``level`` null)
    };
    struct OutputPlan {
        Strip* strip;
        std::vector<Carried> carried;
        std::vector<OutputAudioSink*> sinks;
    };
    struct Plan {
        /// The stacks' (``routing_.stacks``, in order, which is how an output
        /// finds one), then the cue strips'.
        std::vector<StackPlan> stacks;
        std::vector<OutputPlan> outputs;
        /// Every level of every source, carried or not, which each block
        /// moves on.
        std::vector<Level*> levels;
        /// A block for each stack and each of their sounds.
        std::vector<float> blocks;
        /// The strips the plan before it named and it does not, whose meters
        /// go quiet when it is swapped in. One no plan named is quiet
        /// already: nothing meters it.
        std::vector<Strip*> quiet;
    };

    /// What a swap fills in besides the plan (see the class's comment).
    struct Swap {
        /// Levels the plan brings in, each to start where its source's rest
        /// level is.
        std::vector<std::pair<Level*, const Level*>> seeds;
        /// A level ``fade_source`` made, set once it has started.
        Level* faded = nullptr;
        float target = 0.0f;
        std::chrono::milliseconds over{0};
        /// A source just taken out of ``sources_``, the tail it fades out
        /// into (null if no stack plays it), and its sound in the plan in.
        Placed* removed = nullptr;
        Tail* tail = nullptr;
        const SoundPlan* sound = nullptr;
    };

    /// Under ``change_mu_``: give every source a level on every output
    /// routed, make a plan from routing_, strips, sources, tails and sinks,
    /// and swap it in with ``swap``'s work done under ``block_mu_``. A plan
    /// that cannot be made ends the program: once a removal has begun, a
    /// source the plan in still names would otherwise be freed under it.
    void replan_locked(Swap swap) noexcept;
    /// The plan as it stands. Tails that have played or whose stack is not
    /// planned leave ``tails_`` for ``dropped``.
    [[nodiscard]] std::unique_ptr<Plan> make_plan_locked(std::vector<std::unique_ptr<Tail>>& dropped);
    /// Plan ``id``'s strip and the sounds on it into ``plan``; returns how
    /// many blocks are numbered now.
    std::size_t plan_strip(Plan& plan, const std::string& id, std::size_t blocks);
    /// ``placed``'s sound in the plan in, or null if no stack plays it.
    [[nodiscard]] const SoundPlan* sound_of_locked(const Placed* placed) const;
    [[nodiscard]] Placed* placed_locked(const AudioSource* source) const;

    /// The app's side (see the class's comment). Taken before ``block_mu_``.
    mutable std::mutex change_mu_;
    /// A block's: the plan in, what it points at that a block changes, and
    /// ``scratch_`` and ``output_block_``.
    mutable std::mutex block_mu_;
    Routing routing_;
    Strips stacks_;
    Strips outputs_;
    std::vector<std::unique_ptr<Placed>> sources_;
    std::vector<std::pair<std::string, OutputAudioSink*>> sinks_;
    std::vector<std::unique_ptr<Tail>> tails_;
    /// Changed under both locks, so either is enough to read it.
    std::unique_ptr<Plan> plan_;
    std::vector<float> scratch_;
    std::vector<float> output_block_;
    /// Under ``change_mu_`` (``set_planned_hook_for_tests``).
    std::function<void()> planned_hook_;
};

} // namespace seventhslide::audio
