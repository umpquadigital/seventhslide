#include "seventhslide/audio/audio_file_player.hpp"

#include "audio_decoder.hpp"

#include "seventhslide/audio/frame_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace seventhslide::audio {
namespace {

/// How far ahead the decoder keeps: half a second.
constexpr std::size_t k_ahead_frames = k_sample_rate / 2;
/// How often a playing decoder with a full ring, or with all its loop marks
/// used, looks again for room: a small part of what the ring holds. A paused
/// one waits to be told.
constexpr auto k_refill_poll = std::chrono::milliseconds(20);
/// Loop restarts the ring can hold at once: a file shorter than about 8 ms
/// waits for the mixer before it repeats again.
constexpr std::size_t k_max_loop_marks = 64;
/// How long a pause, a resume, a seek or an unload fades over, as the
/// mixer's level changes ramp: 10 ms, so none clicks.
constexpr std::size_t k_fade_frames = k_block_frames;
constexpr float k_fade_step = 1.0f / static_cast<float>(k_fade_frames);
/// A player the mixer has not read for this long is not being heard (taken
/// out of the mixer, or the engine stopped): what it was playing does not
/// fade out later, and its sound fades in again. Well beyond a device
/// period and the engine's stall watchdog.
constexpr auto k_unheard = std::chrono::milliseconds(500);

} // namespace

struct AudioFilePlayer::Impl {
    // --- Guarded by mu ---
    mutable std::mutex mu;
    /// Wakes the decode thread: a seek, an unload, or ``read`` making room
    /// while a paused decoder waits for it (``room_asked``).
    std::condition_variable wake;
    /// Wakes wait_buffered: the ring filled, or the file ended or failed.
    mutable std::condition_variable buffered;
    FrameRing ring{k_ahead_frames};
    /// Bumped by every load and seek; the decoder drops what it decoded for
    /// an older one and goes to ``seek_target``.
    std::uint64_t generation = 0;
    double seek_target = 0.0;
    /// The clock: the file position, in frames, of the next frame read.
    std::int64_t position_frames = 0;
    /// Ring write counts at which the file starts over (looping).
    std::array<std::uint64_t, k_max_loop_marks> loop_marks{};
    std::size_t marks_head = 0;
    std::size_t marks_count = 0;
    /// The decoder has written the file's last frame and does not loop.
    bool decoder_ended = false;
    bool decoder_failed = false;
    bool stop = false;
    /// A paused decoder with a full ring, or all its loop marks used, waits
    /// for ``read`` to make room, and ``read`` tells it on ``wake`` once it
    /// has taken some.
    bool room_asked = false;
    /// Times a decoder with a full ring has looked again for room.
    std::uint64_t refill_looks = 0;
    std::string path;
    /// What the ring plays at, 0 to 1: it falls to 0 over a fade after a
    /// pause and rises from 0 when the sound starts again mid-file, so it
    /// never stops or starts part way through a wave.
    float gain = 0.0f;
    /// What was playing when a seek or an unload cut it short, already
    /// fading out: added to what plays next, so the two cross-fade.
    std::array<float, k_fade_frames * k_channels> tail{};
    std::size_t tail_frames = 0;
    std::size_t tail_read = 0;
    /// The level a transition sets (fade_level): where it is, where it is
    /// going, and how far it moves a frame.
    float level = 1.0f;
    float level_target = 1.0f;
    float level_step = 0.0f;
    /// When the mixer last read it.
    std::chrono::steady_clock::time_point last_read{};

    [[nodiscard]] bool heard_locked() const
    {
        return std::chrono::steady_clock::now() - last_read < k_unheard;
    }

    // --- Read without the lock ---
    std::atomic<bool> has{false};
    std::atomic<bool> playing{false};
    /// The mixer has played the last frame.
    std::atomic<bool> at_end{false};
    std::atomic<bool> loop{false};
    std::atomic<double> duration{0.0};

    std::thread thread;

    void push_mark(std::uint64_t at)
    {
        loop_marks[(marks_head + marks_count) % k_max_loop_marks] = at;
        ++marks_count;
    }
    void pop_mark()
    {
        marks_head = (marks_head + 1) % k_max_loop_marks;
        --marks_count;
    }
    [[nodiscard]] bool next_mark(std::uint64_t& at) const
    {
        if (marks_count == 0) {
            return false;
        }
        at = loop_marks[marks_head];
        return true;
    }

    /// What the ring was about to play, fading out from where the sound is
    /// now, kept as the tail before the ring is emptied. Under the lock.
    void keep_tail_locked()
    {
        if (!heard_locked()) {
            // Nobody is listening: nothing to fade out.
            tail_frames = 0;
            tail_read = 0;
            gain = 0.0f;
            return;
        }
        std::array<float, k_fade_frames * k_channels> next{};
        // An earlier tail still fading goes on under this one.
        std::size_t frames = tail_frames - tail_read;
        std::copy_n(tail.begin() + static_cast<std::ptrdiff_t>(tail_read * k_channels),
                    frames * k_channels, next.begin());
        if (gain > 0.0f) {
            std::array<float, k_fade_frames * k_channels> ahead{};
            const std::size_t n = ring.peek(ahead.data(), k_fade_frames);
            for (std::size_t f = 0; f < n; ++f) {
                const float g =
                    gain * (1.0f - static_cast<float>(f + 1) / static_cast<float>(n));
                for (int c = 0; c < k_channels; ++c) {
                    next[f * k_channels + c] += ahead[f * k_channels + c] * g;
                }
            }
            frames = std::max(frames, n);
        }
        tail = next;
        tail_frames = frames;
        tail_read = 0;
        gain = 0.0f;
    }

    /// Up to ``frames`` frames from the ring, restarting the clock at loop
    /// marks; how many there were. Under the lock.
    std::size_t take_locked(float* out, std::size_t frames)
    {
        std::size_t done = 0;
        while (done < frames) {
            std::uint64_t mark = 0;
            if (next_mark(mark) && ring.read_count() == mark) {
                position_frames = 0;
                pop_mark();
                continue;
            }
            std::size_t n = std::min(frames - done, ring.size());
            if (next_mark(mark)) {
                n = std::min<std::size_t>(n, mark - ring.read_count());
            }
            if (n == 0) {
                break;
            }
            ring.read(out + done * k_channels, n);
            done += n;
            position_frames += static_cast<std::int64_t>(n);
        }
        // A loop that starts over right here shows 0 now, not a block late.
        std::uint64_t mark = 0;
        if (next_mark(mark) && ring.read_count() == mark) {
            position_frames = 0;
            pop_mark();
        }
        return done;
    }

    /// Back to nothing loaded. Under the lock, with no decode thread. A tail
    /// still fading plays on.
    void reset_locked()
    {
        ring.clear();
        gain = 0.0f;
        marks_head = 0;
        marks_count = 0;
        position_frames = 0;
        seek_target = 0.0;
        decoder_ended = false;
        decoder_failed = false;
        stop = false;
    }

    void stop_thread()
    {
        {
            std::lock_guard lock(mu);
            stop = true;
        }
        wake.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }

    void run();
};

void AudioFilePlayer::Impl::run()
{
    std::unique_lock lock(mu);
    const std::string file = path;
    // A file that starts part way through (a load's place, or a seek made
    // before this thread began) goes there as any seek does, the first time
    // round the loop below. Read under the lock with the generation it
    // belongs to, so a seek is never missed.
    std::uint64_t done_generation = seek_target > 0.0 ? generation - 1 : generation;
    lock.unlock();

    AudioDecoder decoder;
    if (!decoder.open(file)) {
        lock.lock();
        decoder_failed = true;
        has = false;
        lock.unlock();
        buffered.notify_all();
        return;
    }
    duration = decoder.duration_sec();

    std::vector<float> chunk;
    std::size_t chunk_used = 0;  // frames of chunk already in the ring
    bool file_ended = false;
    bool decoded_any = false;
    // Frames this pass has decoded from the start, for a file that does not
    // say how long it is.
    std::int64_t pass_frames = 0;
    bool pass_from_start = true;

    lock.lock();
    for (;;) {
        if (stop) {
            return;
        }
        if (done_generation != generation) {
            // A seek: what was decoded belongs to the old place.
            done_generation = generation;
            const double target = seek_target;
            lock.unlock();
            chunk.clear();
            chunk_used = 0;
            file_ended = false;
            pass_frames = 0;
            pass_from_start = target <= 0.0;
            const bool ok = decoder.seek(target);
            lock.lock();
            if (!ok) {
                decoder_failed = true;
                decoder_ended = true;
                buffered.notify_all();
                wake.wait(lock, [&] { return stop || done_generation != generation; });
            }
            continue;
        }
        const std::size_t chunk_frames = chunk.size() / k_channels;
        if (chunk_used < chunk_frames) {
            if (ring.space() == 0) {
                buffered.notify_all();
                if (playing) {
                    wake.wait_for(lock, k_refill_poll);
                } else {
                    room_asked = true;
                    wake.wait(lock, [&] { return stop || done_generation != generation || ring.space() > 0; });
                    room_asked = false;
                }
                ++refill_looks;
                continue;
            }
            chunk_used += ring.write(chunk.data() + chunk_used * k_channels,
                                     chunk_frames - chunk_used);
            if (ring.space() == 0) {
                buffered.notify_all();
            }
            continue;
        }
        if (file_ended) {
            if (pass_from_start && duration.load() <= 0.0 && pass_frames > 0) {
                duration = static_cast<double>(pass_frames) / k_sample_rate;
            }
            if (loop && pass_frames > 0) {
                if (marks_count == k_max_loop_marks) {
                    if (playing) {
                        wake.wait_for(lock, k_refill_poll);
                    } else {
                        // Woken by any read that takes some, which may pass
                        // no mark, so it looks again and asks again.
                        room_asked = true;
                        wake.wait(lock);
                        room_asked = false;
                    }
                    continue;
                }
                push_mark(ring.written());
                lock.unlock();
                const bool ok = decoder.seek(0.0);
                lock.lock();
                if (!ok) {
                    decoder_failed = true;
                    decoder_ended = true;
                    buffered.notify_all();
                    wake.wait(lock, [&] { return stop || done_generation != generation; });
                    continue;
                }
                file_ended = false;
                pass_frames = 0;
                pass_from_start = true;
                continue;
            }
            decoder_ended = true;
            buffered.notify_all();
            wake.wait(lock, [&] { return stop || done_generation != generation; });
            continue;
        }

        lock.unlock();
        chunk.clear();
        chunk_used = 0;
        const auto status = decoder.decode(chunk);
        const auto decoded = static_cast<std::int64_t>(chunk.size() / k_channels);
        pass_frames += decoded;
        decoded_any = decoded_any || decoded > 0;
        lock.lock();
        if (status == AudioDecoder::Status::Failed && !decoded_any) {
            // Opened, but not one frame of it plays: as a file that will not open.
            decoder_failed = true;
            has = false;
            buffered.notify_all();
            return;
        }
        if (status != AudioDecoder::Status::Data) {
            // A file that breaks part way plays what it had, then ends.
            file_ended = true;
        }
    }
}

AudioFilePlayer::AudioFilePlayer()
    : impl_(std::make_unique<Impl>())
{
}

AudioFilePlayer::~AudioFilePlayer()
{
    impl_->stop_thread();
}

bool AudioFilePlayer::do_load(std::string_view path, bool loop, double start_sec)
{
    unload();
    if (path.empty()) {
        return false;
    }
    const std::string file(path);
    // A file that cannot be read fails now rather than a moment later.
    if (!std::ifstream(file, std::ios::binary).is_open()) {
        return false;
    }
    if (!std::isfinite(start_sec) || start_sec < 0.0) {
        start_sec = 0.0;
    }
    {
        std::lock_guard lock(impl_->mu);
        impl_->reset_locked();
        impl_->path = file;
        // The decoder starts here; the clock stands here until it plays.
        impl_->seek_target = start_sec;
        impl_->position_frames = std::llround(start_sec * k_sample_rate);
        ++impl_->generation;
    }
    impl_->loop = loop;
    impl_->duration = 0.0;
    impl_->at_end = false;
    impl_->playing = false;
    impl_->has = true;
    impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
    return true;
}

void AudioFilePlayer::play()
{
    if (impl_->has) {
        impl_->playing = true;
    }
}

void AudioFilePlayer::pause()
{
    impl_->playing = false;
}

void AudioFilePlayer::seek(double position_sec)
{
    if (!impl_->has) {
        return;
    }
    if (!std::isfinite(position_sec) || position_sec < 0.0) {
        position_sec = 0.0;
    }
    const double length = impl_->duration;
    if (length > 0.0) {
        position_sec = std::min(position_sec, length);
    }
    {
        std::lock_guard lock(impl_->mu);
        // What was playing fades out under the new place fading in.
        impl_->keep_tail_locked();
        impl_->ring.clear();
        impl_->marks_head = 0;
        impl_->marks_count = 0;
        impl_->position_frames = std::llround(position_sec * k_sample_rate);
        impl_->seek_target = position_sec;
        impl_->decoder_ended = false;
        ++impl_->generation;
        // A file that played to its end shows paused, and a seek leaves it so.
        if (impl_->at_end) {
            impl_->playing = false;
        }
        impl_->at_end = false;
    }
    impl_->wake.notify_all();
}

void AudioFilePlayer::restart()
{
    if (!impl_->has) {
        return;
    }
    seek(0.0);
    impl_->playing = true;
}

void AudioFilePlayer::unload()
{
    impl_->stop_thread();
    std::lock_guard lock(impl_->mu);
    // A file replaced while it plays fades out under the next.
    impl_->keep_tail_locked();
    impl_->reset_locked();
    impl_->path.clear();
    impl_->has = false;
    impl_->playing = false;
    impl_->at_end = false;
    impl_->duration = 0.0;
}

bool AudioFilePlayer::has_media() const
{
    return impl_->has;
}

bool AudioFilePlayer::is_playing() const
{
    return impl_->has && impl_->playing && !impl_->at_end;
}

bool AudioFilePlayer::ended() const
{
    return impl_->has && impl_->at_end;
}

double AudioFilePlayer::position_sec() const
{
    std::lock_guard lock(impl_->mu);
    return static_cast<double>(impl_->position_frames) / k_sample_rate;
}

double AudioFilePlayer::duration_sec() const
{
    return impl_->has ? impl_->duration.load() : 0.0;
}

bool AudioFilePlayer::loop() const
{
    return impl_->loop;
}

void AudioFilePlayer::read(float* out, std::size_t frames)
{
    std::size_t done = 0;
    std::lock_guard lock(impl_->mu);
    auto& gain = impl_->gain;
    if (!impl_->heard_locked()) {
        // Back in the mixer after a while out of it: what it played then is
        // long gone, and the sound fades in as if from a pause.
        gain = 0.0f;
        impl_->tail_frames = 0;
        impl_->tail_read = 0;
    }
    impl_->last_read = std::chrono::steady_clock::now();
    const bool wanted = impl_->has && impl_->playing && !impl_->at_end;
    if (impl_->has && (wanted || gain > 0.0f)) {
        // The file's own start plays as it is; anywhere else the sound
        // fades in.
        if (wanted && gain == 0.0f && impl_->position_frames == 0) {
            gain = 1.0f;
        }
        // Paused: play on only until the fade is out.
        const auto fade_left = static_cast<std::size_t>(std::ceil(gain * k_fade_frames - 1e-3f));
        const std::size_t limit = wanted ? frames : std::min(frames, fade_left);
        done = impl_->take_locked(out, limit);
        if (impl_->room_asked && done > 0) {
            impl_->room_asked = false;
            impl_->wake.notify_all();
        }
        const float target = wanted ? 1.0f : 0.0f;
        if (gain != 1.0f || target != 1.0f) {
            for (std::size_t f = 0; f < done; ++f) {
                if (gain != target) {
                    gain += target > gain ? k_fade_step : -k_fade_step;
                    // Steps of 1/480 do not add up to exactly 0 or 1.
                    if (std::abs(gain - target) < 0.5f * k_fade_step) {
                        gain = target;
                    }
                }
                for (int c = 0; c < k_channels; ++c) {
                    out[f * k_channels + c] *= gain;
                }
            }
        }
        if (!wanted && limit == fade_left) {
            gain = 0.0f;
        }
        if (done < limit) {
            // The ring ran dry with the decoder behind: what this block had
            // fades out, so the gap does not start with a click, and the
            // sound fades in again once it is back. The file's end plays as
            // it is.
            if (!impl_->decoder_ended) {
                const std::size_t n = std::min(done, k_fade_frames);
                for (std::size_t i = 0; i < n; ++i) {
                    const float g = static_cast<float>(n - 1 - i) / static_cast<float>(n);
                    for (int c = 0; c < k_channels; ++c) {
                        out[(done - n + i) * k_channels + c] *= g;
                    }
                }
            }
            gain = 0.0f;
        }
        if (impl_->ring.empty() && impl_->decoder_ended) {
            impl_->at_end = true;
        }
    }
    std::fill(out + done * k_channels, out + frames * k_channels, 0.0f);
    // What a seek or an unload cut short, fading out.
    auto& tail_read = impl_->tail_read;
    for (std::size_t f = 0; f < frames && tail_read < impl_->tail_frames; ++f, ++tail_read) {
        for (int c = 0; c < k_channels; ++c) {
            out[f * k_channels + c] += impl_->tail[tail_read * k_channels + c];
        }
    }
    // A transition's level, over everything.
    auto& level = impl_->level;
    const float target = impl_->level_target;
    if (level == 1.0f && target == 1.0f) {
        return;
    }
    for (std::size_t f = 0; f < frames; ++f) {
        if (level != target) {
            const float step = impl_->level_step;
            level = target > level ? std::min(target, level + step) : std::max(target, level - step);
            // Steps of a fraction do not add up to the target exactly.
            if (std::abs(target - level) < 0.5f * step) {
                level = target;
            }
        }
        for (int c = 0; c < k_channels; ++c) {
            out[f * k_channels + c] *= level;
        }
    }
}

void AudioFilePlayer::fade_level(float target, std::chrono::milliseconds over)
{
    std::lock_guard lock(impl_->mu);
    impl_->level_target = std::clamp(target, 0.0f, 1.0f);
    const auto frames = over.count() * k_sample_rate / 1000;
    if (frames <= 0) {
        impl_->level = impl_->level_target;
        impl_->level_step = 0.0f;
        return;
    }
    impl_->level_step = std::max(1e-6f, std::abs(impl_->level_target - impl_->level)
                                            / static_cast<float>(frames));
}

float AudioFilePlayer::level() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->level;
}

float AudioFilePlayer::level_target() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->level_target;
}

std::uint64_t AudioFilePlayer::refill_looks() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->refill_looks;
}

bool AudioFilePlayer::wait_buffered(std::chrono::milliseconds timeout) const
{
    std::unique_lock lock(impl_->mu);
    return impl_->buffered.wait_for(lock, timeout, [&] {
        return !impl_->has || impl_->decoder_failed || impl_->decoder_ended
            || impl_->ring.space() == 0;
    });
}

} // namespace seventhslide::audio
