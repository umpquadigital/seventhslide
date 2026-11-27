#pragma once

// Stand-in players for session tests: what the app hands out
// (``Session::PlayerFor``), counting what the session asks of them.

#include <seventhslide/media/media_player.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/session/session.hpp>

#include <cstdint>
#include <map>
#include <string_view>

namespace seventhslide::test {

/// Records loads, plays, pauses and unloads so a test can tell which stack
/// drove the player.
class CountingPlayer final : public media::IMediaPlayer {
public:
    void play() override
    {
        playing_ = true;
        ++plays;
    }
    void pause() override
    {
        playing_ = false;
        ++pauses;
    }
    void seek(double) override {}
    void restart() override
    {
        playing_ = true;
        ended_ = false;
        position = 0.0;
        ++restarts;
    }
    void unload() override
    {
        has_media_ = false;
        playing_ = false;
        ++unloads;
    }
    [[nodiscard]] bool has_media() const override { return has_media_; }
    [[nodiscard]] bool is_playing() const override { return has_media_ && playing_ && !ended_; }
    [[nodiscard]] bool ended() const override { return has_media_ && ended_; }
    /// Play to the end, as a file that does not loop stops there.
    void end() { ended_ = true; }
    [[nodiscard]] double position_sec() const override { return position; }
    [[nodiscard]] double duration_sec() const override { return 0.0; }
    [[nodiscard]] bool loop() const override { return false; }

    int loads = 0;
    int plays = 0;
    int pauses = 0;
    int unloads = 0;
    int restarts = 0;
    double position = 0.0;

private:
    bool do_load(std::string_view, bool, double start_sec) override
    {
        has_media_ = true;
        position = start_sec;
        ++loads;
        return true;
    }

    bool has_media_ = false;
    bool playing_ = false;
    bool ended_ = false;
};

/// The players the app hands out (``Session::PlayerFor``): a CountingPlayer
/// of its own for each item, as the app's decks are filed by the item; or,
/// with ``video`` set, that one for every video (a test of one).
struct Players {
    CountingPlayer* video = nullptr;
    std::map<std::uint64_t, CountingPlayer> audio;
    std::map<std::uint64_t, CountingPlayer> videos;

    [[nodiscard]] session::Session::PlayerFor source()
    {
        return [this](std::uint64_t instance, model::ContentKind kind,
                      session::StackSide) -> media::IMediaPlayer* {
            if (kind == model::ContentKind::Video) {
                return video != nullptr ? video : &videos.try_emplace(instance).first->second;
            }
            return &audio.try_emplace(instance).first->second;
        };
    }
    /// The video player ``st``'s item was given (with no ``video`` set).
    [[nodiscard]] CountingPlayer& video_of(const session::StackRuntime& st)
    {
        return videos.at(st.media_instance());
    }
    /// The audio player ``st``'s item was given.
    [[nodiscard]] CountingPlayer& audio_of(const session::StackRuntime& st)
    {
        return audio.at(st.media_instance());
    }
};

}  // namespace seventhslide::test
