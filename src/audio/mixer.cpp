#include "seventhslide/audio/mixer.hpp"

#include "seventhslide/audio/audio_source.hpp"
#include "seventhslide/media/audio_levels.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace seventhslide::audio {
namespace {

constexpr float k_silence_db = -std::numeric_limits<float>::infinity();
constexpr std::size_t k_block_samples = k_block_frames * k_channels;

/// ``frames`` scaled by a gain moving linearly from ``from`` to ``to``.
void scale(float* frames, std::size_t count, float from, float to)
{
    if (from == to) {
        if (from == 1.0f) {
            return;
        }
        for (std::size_t i = 0; i < count * k_channels; ++i) {
            frames[i] *= from;
        }
        return;
    }
    const float step = (to - from) / static_cast<float>(count);
    for (std::size_t f = 0; f < count; ++f) {
        const float g = from + step * static_cast<float>(f + 1);
        for (int c = 0; c < k_channels; ++c) {
            frames[f * k_channels + c] *= g;
        }
    }
}

/// ``dst += src * gain``, the gain moving linearly from ``from`` to ``to``.
void add_scaled(float* dst, const float* src, std::size_t count, float from, float to)
{
    if (from == 0.0f && to == 0.0f) {
        return;
    }
    const float step = (to - from) / static_cast<float>(count);
    for (std::size_t f = 0; f < count; ++f) {
        const float g = from == to ? from : from + step * static_cast<float>(f + 1);
        for (int c = 0; c < k_channels; ++c) {
            dst[f * k_channels + c] += src[f * k_channels + c] * g;
        }
    }
}

} // namespace

Mixer::Strip::Strip()
{
    // As a strip nobody has touched: full level, unmuted, off Listen.
    apply_settings(*this, library::ChannelMix{}, true);
    level_db.fill(k_silence_db);
    hold_db.fill(k_silence_db);
}

Mixer::Mixer()
    : plan_(std::make_unique<Plan>())
    , scratch_(k_block_samples)
    , output_block_(k_block_samples)
{
}

void Mixer::Level::set(float to_target, std::chrono::milliseconds over) noexcept
{
    target = std::clamp(to_target, 0.0f, 1.0f);
    const auto frames = over.count() * k_sample_rate / 1000;
    if (frames <= 0) {
        current = target;
        step = 0.0f;
        return;
    }
    step = std::max(1e-6f, std::abs(target - current) / static_cast<float>(frames));
}

void Mixer::Level::advance(std::size_t frames) noexcept
{
    from = current;
    if (current != target) {
        const float by = step * static_cast<float>(frames);
        current = std::abs(target - current) <= by ? target
                  : current < target                ? current + by
                                                    : current - by;
    }
    to = current;
}

Mixer::Level* Mixer::Placed::on(std::string_view output_id) noexcept
{
    const auto it = std::ranges::find(outputs, output_id, [](const auto& e) -> std::string_view {
        return e.first;
    });
    return it != outputs.end() ? &it->second : nullptr;
}

const Mixer::Level* Mixer::Placed::on(std::string_view output_id) const noexcept
{
    return const_cast<Placed*>(this)->on(output_id);
}

const float* Mixer::Tail::on(std::string_view output_id) const noexcept
{
    const auto it = std::ranges::find(outputs, output_id, [](const auto& e) -> std::string_view {
        return e.first;
    });
    return it != outputs.end() ? &it->second : &rest;
}

void Mixer::apply_settings(Strip& strip, const library::ChannelMix& mix, bool snap)
{
    strip.mix = mix;
    strip.mix.gain_db = media::normalize_gain_db(mix.gain_db);
    strip.fader.target = static_cast<float>(media::db_to_gain(strip.mix.gain_db));
    strip.mute.target = mix.muted ? 0.0f : 1.0f;
    strip.listen.target = mix.listen ? 1.0f : 0.0f;
    if (snap) {
        strip.fader.current = strip.fader.target;
        strip.mute.current = strip.mute.target;
        strip.listen.current = strip.listen.target;
    }
}

void Mixer::meter_block(Strip& strip, const float* frames, std::size_t count)
{
    std::array<float, k_channels> peak{};
    for (std::size_t f = 0; f < count; ++f) {
        for (int c = 0; c < k_channels; ++c) {
            peak[c] = std::max(peak[c], std::abs(frames[f * k_channels + c]));
        }
    }
    const double block_sec = static_cast<double>(count) / k_sample_rate;
    const auto fall = static_cast<float>(k_meter_decay_db_per_sec * block_sec);
    for (int c = 0; c < k_channels; ++c) {
        const auto db = static_cast<float>(media::gain_to_db(peak[c]));
        strip.level_db[c] = std::max(db, strip.level_db[c] - fall);
        if (strip.level_db[c] < static_cast<float>(media::k_fader_floor_db)) {
            strip.level_db[c] = k_silence_db;
        }
        strip.hold_age_sec[c] += block_sec;
        if (db >= strip.hold_db[c] || strip.hold_age_sec[c] > k_peak_hold_sec) {
            strip.hold_db[c] = db;
            strip.hold_age_sec[c] = 0.0;
        }
    }
}

void Mixer::set_routing(Routing routing)
{
    std::lock_guard changes(change_mu_);
    routing_ = std::move(routing);
    replan_locked(Swap{});
}

void Mixer::set_strip(StripKind kind, std::string_view id, const library::ChannelMix& mix)
{
    std::lock_guard changes(change_mu_);
    auto& strips = strips_of(kind);
    const auto it = strips.find(id);
    if (it == strips.end()) {
        // A strip first set before it has played starts where it is set. No
        // plan names it yet, so no block reads it, and none need be made: the
        // plan that routes it finds it.
        apply_settings(strips.try_emplace(std::string(id)).first->second, mix, true);
        return;
    }
    std::lock_guard block(block_mu_);
    apply_settings(it->second, mix, false);
}

library::ChannelMix Mixer::strip(StripKind kind, std::string_view id) const
{
    std::lock_guard changes(change_mu_);
    const auto& strips = strips_of(kind);
    const auto it = strips.find(id);
    return it == strips.end() ? library::ChannelMix{} : it->second.mix;
}

Mixer::Placed* Mixer::placed_locked(const AudioSource* source) const
{
    const auto it = std::ranges::find(sources_, source, [](const auto& p) -> const AudioSource* {
        return p->source;
    });
    return it != sources_.end() ? it->get() : nullptr;
}

const Mixer::SoundPlan* Mixer::sound_of_locked(const Placed* placed) const
{
    for (const auto& stack : plan_->stacks) {
        const auto sound = std::ranges::find(stack.sounds, placed, &SoundPlan::placed);
        if (sound != stack.sounds.end()) {
            return &*sound;
        }
    }
    return nullptr;
}

void Mixer::add_source(std::string_view stack_id, AudioSource* source, std::string_view type)
{
    if (source == nullptr) {
        return;
    }
    std::lock_guard changes(change_mu_);
    auto placed = std::make_unique<Placed>();
    placed->stack_id = std::string(stack_id);
    placed->source = source;
    placed->type = std::string(type);
    sources_.push_back(std::move(placed));
    replan_locked(Swap{});
}

void Mixer::remove_source(AudioSource* source)
{
    std::lock_guard changes(change_mu_);
    const auto it = std::ranges::find(sources_, source, [](const auto& p) { return p->source; });
    if (it == sources_.end()) {
        return;
    }
    // Kept until the plan that names it is swapped out, after which no block
    // reads it.
    const std::unique_ptr<Placed> removed = std::move(*it);
    sources_.erase(it);
    Swap swap;
    swap.removed = removed.get();
    swap.sound = sound_of_locked(removed.get());
    if (swap.sound != nullptr) {
        // Its fade-out, made here so the swap only fills it in.
        auto tail = std::make_unique<Tail>();
        tail->stack_id = removed->stack_id;
        tail->type = removed->type;
        tail->outputs.reserve(removed->outputs.size());
        for (const auto& [output_id, level] : removed->outputs) {
            tail->outputs.emplace_back(output_id, 0.0f);
        }
        swap.tail = tail.get();
        tails_.push_back(std::move(tail));
    }
    replan_locked(std::move(swap));
}

void Mixer::move_source(AudioSource* source, std::string_view stack_id)
{
    std::lock_guard changes(change_mu_);
    for (auto& placed : sources_) {
        if (placed->source == source) {
            placed->stack_id = std::string(stack_id);
        }
    }
    replan_locked(Swap{});
}

void Mixer::fade_source(AudioSource* source,
                        std::string_view output_id,
                        float target,
                        std::chrono::milliseconds over)
{
    std::lock_guard changes(change_mu_);
    auto* placed = placed_locked(source);
    if (placed == nullptr) {
        return;
    }
    if (output_id.empty()) {
        std::lock_guard block(block_mu_);
        placed->rest.set(target, over);
        return;
    }
    if (auto* level = placed->on(output_id)) {
        std::lock_guard block(block_mu_);
        level->set(target, over);
        return;
    }
    // An output the routing does not name yet: kept for when it does, and
    // moved on by every block from the next, so it is planned.
    auto& level = placed->outputs.emplace_back(std::string(output_id), Level{}).second;
    Swap swap;
    swap.seeds.emplace_back(&level, &placed->rest);
    swap.faded = &level;
    swap.target = target;
    swap.over = over;
    replan_locked(std::move(swap));
}

void Mixer::set_source_level(AudioSource* source, float level)
{
    std::lock_guard changes(change_mu_);
    auto* placed = placed_locked(source);
    if (placed == nullptr) {
        return;
    }
    std::lock_guard block(block_mu_);
    placed->rest.set(level, std::chrono::milliseconds(0));
    for (auto& [output_id, each] : placed->outputs) {
        each.set(level, std::chrono::milliseconds(0));
    }
}

std::optional<float> Mixer::source_level(const AudioSource* source,
                                         std::string_view output_id) const
{
    std::lock_guard changes(change_mu_);
    const auto* placed = placed_locked(source);
    if (placed == nullptr) {
        return std::nullopt;
    }
    if (!output_id.empty()) {
        const auto* level = placed->on(output_id);
        std::lock_guard block(block_mu_);
        return level != nullptr ? level->current : placed->rest.current;
    }
    const auto* sound = sound_of_locked(placed);
    std::lock_guard block(block_mu_);
    if (sound == nullptr || sound->carried.empty()) {
        return placed->rest.current;
    }
    float loudest = 0.0f;
    for (const auto* level : sound->carried) {
        loudest = std::max(loudest, level->current);
    }
    return loudest;
}

void Mixer::add_sink(std::string_view output_id, OutputAudioSink* sink)
{
    if (sink == nullptr) {
        return;
    }
    std::lock_guard changes(change_mu_);
    sinks_.emplace_back(std::string(output_id), sink);
    replan_locked(Swap{});
}

void Mixer::remove_sink(OutputAudioSink* sink)
{
    std::lock_guard changes(change_mu_);
    std::erase_if(sinks_, [sink](const auto& entry) { return entry.second == sink; });
    replan_locked(Swap{});
}

MeterReading Mixer::meter(StripKind kind, std::string_view id) const
{
    MeterReading out{};
    out.level_db.fill(k_silence_db);
    out.peak_hold_db.fill(k_silence_db);
    std::lock_guard changes(change_mu_);
    const auto routed = [&](const Strip* s) {
        if (kind == StripKind::Stack) {
            return std::ranges::any_of(plan_->stacks, [s](const auto& p) { return p.strip == s; });
        }
        return std::ranges::any_of(plan_->outputs, [s](const auto& p) { return p.strip == s; });
    };
    const auto& strips = strips_of(kind);
    const auto it = strips.find(id);
    if (it == strips.end() || !routed(&it->second)) {
        return out;
    }
    std::lock_guard block(block_mu_);
    out.level_db = it->second.level_db;
    out.peak_hold_db = it->second.hold_db;
    return out;
}

void Mixer::set_planned_hook_for_tests(std::function<void()> hook)
{
    std::lock_guard changes(change_mu_);
    planned_hook_ = std::move(hook);
}

std::string cue_strip_id(std::string_view stack_id)
{
    return std::string(stack_id) + "#cue";
}

std::size_t Mixer::plan_strip(Plan& plan, const std::string& id, std::size_t blocks)
{
    StackPlan stack{&stacks_.try_emplace(id).first->second, {}, blocks++};
    for (const auto& placed : sources_) {
        if (placed->stack_id == id) {
            stack.sounds.push_back({placed.get(), nullptr, blocks++});
        }
    }
    // A source removed is planned until its last block has played.
    for (const auto& tail : tails_) {
        if (tail->stack_id == id) {
            stack.sounds.push_back({nullptr, tail.get(), blocks++});
        }
    }
    plan.stacks.push_back(std::move(stack));
    return blocks;
}

std::unique_ptr<Mixer::Plan> Mixer::make_plan_locked(std::vector<std::unique_ptr<Tail>>& dropped)
{
    auto plan = std::make_unique<Plan>();

    // A tail that has played, or whose stack has left the routing, goes. One
    // still pending may be played by a block before the swap, and plays
    // silence until the next plan drops it.
    const auto planned = [this](const std::string& id) {
        return std::ranges::find(routing_.stacks, id) != routing_.stacks.end()
               || std::ranges::find(routing_.cues, id) != routing_.cues.end();
    };
    const auto gone = std::stable_partition(tails_.begin(), tails_.end(), [&](const auto& tail) {
        return tail->pending.load(std::memory_order_relaxed) && planned(tail->stack_id);
    });
    std::move(gone, tails_.end(), std::back_inserter(dropped));
    tails_.erase(gone, tails_.end());

    for (const auto& placed : sources_) {
        plan->levels.push_back(&placed->rest);
        for (auto& [output_id, level] : placed->outputs) {
            plan->levels.push_back(&level);
        }
    }

    std::size_t blocks = 0;
    for (const auto& id : routing_.stacks) {
        blocks = plan_strip(*plan, id, blocks);
    }
    // After every stack, so no output reaches one: an output finds a stack's
    // plan by its place in ``routing_.stacks``.
    for (const auto& id : routing_.cues) {
        if (std::ranges::find(routing_.stacks, id) == routing_.stacks.end()) {
            blocks = plan_strip(*plan, id, blocks);
        }
    }
    std::unordered_set<const Strip*> routed;
    for (const auto& stack : plan->stacks) {
        routed.insert(stack.strip);
    }
    for (const auto& output : routing_.outputs) {
        OutputPlan out{&outputs_.try_emplace(output.id).first->second, {}, {}};
        for (const auto& stack_id : output.stacks) {
            const auto at = std::ranges::find(routing_.stacks, stack_id);
            if (at == routing_.stacks.end()) {
                continue;
            }
            auto& stack = plan->stacks[static_cast<std::size_t>(at - routing_.stacks.begin())];
            for (auto& sound : stack.sounds) {
                const auto& type = sound.placed != nullptr ? sound.placed->type : sound.tail->type;
                if (std::ranges::find(output.left_out, type) != output.left_out.end()) {
                    continue;
                }
                if (sound.placed != nullptr) {
                    const Level* level = sound.placed->on(output.id);
                    // replan_locked gives every source a level on every
                    // output routed.
                    assert(level != nullptr);
                    out.carried.push_back({sound.block, level, nullptr});
                    sound.carried.push_back(level);
                } else {
                    out.carried.push_back({sound.block, nullptr, sound.tail->on(output.id)});
                }
            }
        }
        for (const auto& [output_id, sink] : sinks_) {
            if (output_id == output.id) {
                out.sinks.push_back(sink);
            }
        }
        routed.insert(out.strip);
        plan->outputs.push_back(std::move(out));
    }
    const auto leaving = [&](Strip* strip) {
        if (!routed.contains(strip)) {
            plan->quiet.push_back(strip);
        }
    };
    for (const auto& stack : plan_->stacks) {
        leaving(stack.strip);
    }
    for (const auto& out : plan_->outputs) {
        leaving(out.strip);
    }
    plan->blocks.assign(blocks * k_block_samples, 0.0f);
    return plan;
}

void Mixer::replan_locked(Swap swap) noexcept
{
    // Every source has a level for every output routed, so the plan can point
    // at it. One an output comes to carry starts where its source's rest
    // level is when the plan goes in.
    for (auto& placed : sources_) {
        for (const auto& output : routing_.outputs) {
            if (placed->on(output.id) == nullptr) {
                auto& level = placed->outputs.emplace_back(output.id, Level{}).second;
                swap.seeds.emplace_back(&level, &placed->rest);
            }
        }
    }
    // Freed at the end, after the lock is let go: the plan swapped out, and
    // the tails it alone named.
    std::vector<std::unique_ptr<Tail>> dropped;
    std::unique_ptr<Plan> next = make_plan_locked(dropped);
    if (planned_hook_) {
        planned_hook_();
    }
    {
        std::lock_guard block(block_mu_);
        if (swap.tail != nullptr) {
            // What the source plays next, fading out across the next block,
            // at the level each output carries it at now. Taking the lock
            // waited out a block in progress; after the swap no plan names
            // the source.
            auto& tail = *swap.tail;
            const auto& removed = *swap.removed;
            removed.source->read(scratch_.data(), k_block_frames);
            add_scaled(tail.frames.data(), scratch_.data(), k_block_frames, 1.0f, 0.0f);
            tail.rest = removed.rest.current;
            tail.stack_level = swap.sound->carried.empty() ? tail.rest : 0.0f;
            for (const auto* level : swap.sound->carried) {
                tail.stack_level = std::max(tail.stack_level, level->current);
            }
            // remove_source made one gain for each of its levels, in order.
            assert(tail.outputs.size() == removed.outputs.size());
            for (std::size_t i = 0; i < tail.outputs.size(); ++i) {
                tail.outputs[i].second = removed.outputs[i].second.current;
            }
        }
        for (const auto& [level, rest] : swap.seeds) {
            *level = *rest;
        }
        if (swap.faded != nullptr) {
            swap.faded->set(swap.target, swap.over);
        }
        plan_.swap(next);
        // A strip that leaves the routing comes back with a quiet meter.
        for (auto* strip : plan_->quiet) {
            strip->level_db.fill(k_silence_db);
            strip->hold_db.fill(k_silence_db);
        }
    }
}

void Mixer::process(float* listen, std::size_t frames)
{
    frames = std::min(frames, k_block_frames);
    const std::size_t samples = frames * k_channels;
    std::fill_n(listen, samples, 0.0f);

    std::lock_guard lock(block_mu_);
    auto& plan = *plan_;
    float* const blocks = plan.blocks.data();
    const auto block_at = [blocks](std::size_t block) { return blocks + block * k_block_samples; };
    // Every source's levels move on with the clock, heard or not.
    for (auto* level : plan.levels) {
        level->advance(frames);
    }
    for (auto& stack : plan.stacks) {
        auto& strip = *stack.strip;
        float* sum = block_at(stack.block);
        std::fill_n(sum, samples, 0.0f);
        for (auto& sound : stack.sounds) {
            float* block = block_at(sound.block);
            float from = 1.0f;
            float to = 1.0f;
            if (sound.placed != nullptr) {
                sound.placed->source->read(block, frames);
                // The strip hears it at the loudest output carrying it.
                if (sound.carried.empty()) {
                    from = sound.placed->rest.from;
                    to = sound.placed->rest.to;
                } else {
                    from = 0.0f;
                    to = 0.0f;
                    for (const auto* level : sound.carried) {
                        from = std::max(from, level->from);
                        to = std::max(to, level->to);
                    }
                }
            } else if (sound.tail->pending.load(std::memory_order_relaxed)) {
                // Its one block, once; silence after, until a plan drops it.
                std::copy_n(sound.tail->frames.data(), samples, block);
                sound.tail->pending.store(false, std::memory_order_relaxed);
                from = to = sound.tail->stack_level;
            } else {
                std::fill_n(block, samples, 0.0f);
            }
            scale(block, frames, strip.fader.current, strip.fader.target);
            add_scaled(sum, block, frames, from, to);
        }
        strip.fader.current = strip.fader.target;
        meter_block(strip, sum, frames);
        for (const auto& sound : stack.sounds) {
            scale(block_at(sound.block), frames, strip.mute.current, strip.mute.target);
        }
        scale(sum, frames, strip.mute.current, strip.mute.target);
        strip.mute.current = strip.mute.target;
        add_scaled(listen, sum, frames, strip.listen.current, strip.listen.target);
        strip.listen.current = strip.listen.target;
    }

    for (auto& output : plan.outputs) {
        auto& strip = *output.strip;
        float* block = output_block_.data();
        std::fill_n(block, samples, 0.0f);
        for (const auto& carried : output.carried) {
            const float from = carried.level != nullptr ? carried.level->from : *carried.gain;
            const float to = carried.level != nullptr ? carried.level->to : *carried.gain;
            add_scaled(block, block_at(carried.block), frames, from, to);
        }
        scale(block, frames, strip.fader.current, strip.fader.target);
        strip.fader.current = strip.fader.target;
        meter_block(strip, block, frames);
        scale(block, frames, strip.mute.current, strip.mute.target);
        strip.mute.current = strip.mute.target;
        for (auto* sink : output.sinks) {
            sink->push(block, frames);
        }
        add_scaled(listen, block, frames, strip.listen.current, strip.listen.target);
        strip.listen.current = strip.listen.target;
    }

    for (std::size_t i = 0; i < samples; ++i) {
        listen[i] = std::clamp(listen[i], -1.0f, 1.0f);
    }
}

} // namespace seventhslide::audio
