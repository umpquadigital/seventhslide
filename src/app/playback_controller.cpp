#include "playback_controller.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/static_assets.hpp>

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace seventhslide::app {

namespace {

/// The longest a video object waits on a new picture before the tick gives
/// up on it (mpv took under a second in every test; a held frame may never
/// change).
constexpr qint64 k_object_picture_wait_ms = 3000;

/// One of each output's fades, by output id.
[[nodiscard]] session::ByOutput<int> each_ms(const session::ByOutput<model::AudioFades>& fades,
                                             int model::AudioFades::*field)
{
    session::ByOutput<int> out;
    for (const auto& [output_id, each] : fades) {
        out[output_id] = each.*field;
    }
    return out;
}

[[nodiscard]] int longest_ms(const session::ByOutput<int>& ms)
{
    int longest = 0;
    for (const auto& [output_id, each] : ms) {
        longest = std::max(longest, each);
    }
    return longest;
}

[[nodiscard]] int ms_on(const session::ByOutput<int>& ms, std::string_view output_id)
{
    const auto it = ms.find(output_id);
    return it != ms.end() ? std::max(0, it->second) : 0;
}

}  // namespace

media::IMediaPlayer& PlaybackController::MediaDeck::player() noexcept
{
    return video ? static_cast<media::IMediaPlayer&>(*av) : *transport;
}

const media::IMediaPlayer& PlaybackController::MediaDeck::player() const noexcept
{
    return video ? static_cast<const media::IMediaPlayer&>(*av) : *transport;
}

PlaybackController::PlaybackController(session::Session& session,
                                       OpenLibrary& library,
                                       audio::Mixer& mixer,
                                       QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , mixer_(mixer)
{
    tick_ = new QTimer(this);
    tick_->setInterval(33); // ~30 fps frame pulls for preview + audience
    connect(tick_, &QTimer::timeout, this, &PlaybackController::on_tick);
    leaving_clock_.start();
    session_.set_players(
        [this](std::uint64_t media_instance, model::ContentKind kind, session::StackSide side) {
            return player_for(media_instance, kind, side);
        });
}

PlaybackController::~PlaybackController()
{
    // The session must not reach for a deck once it has gone.
    session_.set_players({});
    // The mixer's clock may be reading these players right now.
    for (auto& [instance, deck] : decks_) {
        place(deck, {});
        deck.player().unload();
    }
    for (auto& [key, player] : object_players_) {
        release_object_player(player);
    }
}

void PlaybackController::set_output_host(render::OutputWindowHost* host)
{
    windows_ = host;
    if (windows_ != nullptr) {
        windows_->set_source_frames("content", content_frames_);
    }
}

QImage PlaybackController::video_picture(std::string_view source) const
{
    // A cued video's own picture first: the outputs may still hold the last
    // one it showed there, as the take that sent it back fades out.
    for (const auto* frames : {&cued_frames_, &content_frames_}) {
        if (const QImage* shown = render::source_image(frames, source)) {
            return *shown;
        }
    }
    // At most 1920x1080, and the same image (same cacheKey) until mpv decodes
    // another, so a preview uploads each frame once.
    for (const auto& [instance, deck] : decks_) {
        if (deck.video && deck.picture->has_media()
            && scene::content_video_key(instance) == source) {
            return deck.picture->current_frame();
        }
    }
    return {};
}

namespace {

/// A deck the outputs or a well show and drive now: Live, or Cued.
template <typename Deck>
[[nodiscard]] bool shown(const Deck& deck) noexcept
{
    return deck.state == Deck::State::Live || deck.state == Deck::State::Cued;
}

}  // namespace

bool PlaybackController::any_video_shown() const noexcept
{
    return std::any_of(decks_.begin(), decks_.end(), [](const auto& entry) {
        return entry.second.video && shown(entry.second);
    });
}

bool PlaybackController::shown_videos_settled() const noexcept
{
    return std::all_of(decks_.begin(), decks_.end(), [this](const auto& entry) {
        const auto& deck = entry.second;
        if (!deck.video || !shown(deck)) {
            return true;
        }
        if (deck.picture->is_playing()) {
            return false;
        }
        // Loads are asynchronous: a still clip needs ticks until its picture
        // arrives, or its player gives up on the file.
        const auto& frames =
            deck.state == MediaDeck::State::Cued ? cued_frames_ : content_frames_;
        return render::source_image(&frames, scene::content_video_key(entry.first)) != nullptr
               || !deck.picture->has_media();
    });
}

std::size_t PlaybackController::count_decks(bool video, MediaDeck::State state) const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(decks_.begin(), decks_.end(), [&](const auto& entry) {
            return entry.second.video == video && entry.second.state == state;
        }));
}

bool PlaybackController::any_leaving() const noexcept
{
    return !content_frames_until_.empty()
           || std::any_of(decks_.begin(), decks_.end(), [](const auto& entry) {
                  return entry.second.state == MediaDeck::State::Leaving;
              });
}

float PlaybackController::audio_level(std::string_view stack_id, std::string_view output_id) const
{
    for (const auto& [instance, deck] : decks_) {
        if (!deck.video && deck.state == MediaDeck::State::Live && deck.strip == stack_id) {
            return mixer_.source_level(deck.sound.get(), output_id).value_or(-1.0f);
        }
    }
    return -1.0f;
}

int PlaybackController::fade_sound(const MediaDeck& deck,
                                   float target,
                                   const session::ByOutput<int>& ms,
                                   bool from_nothing)
{
    auto* sound = deck.sound.get();
    const auto fade = [&](std::string_view output_id, int over) {
        if (from_nothing && over > 0) {
            mixer_.fade_source(sound, output_id, 0.0f, std::chrono::milliseconds(0));
        }
        mixer_.fade_source(sound, output_id, target, std::chrono::milliseconds(over));
    };
    // Only the outputs on: the mixer carries no other's sound.
    for (const auto& oc : session_.outputs_on()) {
        fade(oc.id, ms_on(ms, oc.id));
    }
    const int longest = longest_ms(ms);
    fade({}, longest);
    return longest;
}

media::IMediaPlayer* PlaybackController::player_for(std::uint64_t media_instance,
                                                    model::ContentKind kind,
                                                    session::StackSide side)
{
    auto it = decks_.find(media_instance);
    if (it != decks_.end() && side == session::StackSide::Preview
        && (it->second.state == MediaDeck::State::Live
            || it->second.state == MediaDeck::State::Leaving)) {
        // A take sent it back, and it is still the outputs': the Preview's
        // transport must not move what they show.
        return nullptr;
    }
    if (it == decks_.end()) {
        MediaDeck deck;
        deck.video = kind == model::ContentKind::Video;
        deck.sound = std::make_unique<audio::AudioFilePlayer>();
        if (deck.video) {
            deck.picture = std::make_unique<render::MpvMediaPlayer>();
            deck.av = std::make_unique<VideoWithSound>(*deck.picture, *deck.sound);
        } else {
            deck.transport = std::make_unique<AudioTransport>(
                *deck.sound, [this, media_instance](bool play) { transport(media_instance, play); });
        }
        // Its strip is the stack playing it, which the sync says.
        it = decks_.emplace(media_instance, std::move(deck)).first;
    }
    return &it->second.player();
}

void PlaybackController::place(MediaDeck& deck, std::string_view stack_id)
{
    if (deck.strip == stack_id) {
        return;
    }
    if (deck.strip.empty()) {
        mixer_.add_source(stack_id, deck.sound.get(),
                          deck.video ? model::k_item_type_video : model::k_item_type_audio);
    } else if (stack_id.empty()) {
        mixer_.remove_source(deck.sound.get());
    } else {
        mixer_.move_source(deck.sound.get(), stack_id);
    }
    deck.strip = std::string(stack_id);
}

PlaybackController::Decks::iterator PlaybackController::end_deck(Decks::iterator it, bool gone)
{
    auto& deck = it->second;
    // Only an item that went, playing, on a strip has anything to carry on;
    // one still on a Program but given no player stops, its place kept for
    // when it has one again. A still video's picture stays as long as the
    // change its stack makes now.
    if (!gone || !deck.player().is_playing() || deck.strip.empty()) {
        return release(it, std::max<qint64>(k_keep_last_picture_ms,
                                             session_.transition_ms_on(deck.stack_id)));
    }
    const qint64 now = leaving_clock_.elapsed();
    if (deck.video) {
        // How long it plays on is the change its stack makes now, known once
        // the new content is (settle_leaving_videos).
        deck.state = MediaDeck::State::Leaving;
        deck.fresh = true;
        deck.until_ms = now;
        return std::next(it);
    }
    // An audio item goes as each output's fades said, as of the last sync:
    // its stack may hold another item by now, with fades of its own.
    const auto found = last_fades_.find(it->first);
    deck.fades = found != last_fades_.end() ? found->second : session::ByOutput<model::AudioFades>{};
    const auto out_ms = each_ms(deck.fades, &model::AudioFades::fade_out_ms);
    if (longest_ms(out_ms) <= 0
        && longest_ms(each_ms(deck.fades, &model::AudioFades::overlap_ms)) <= 0) {
        return release(it);
    }
    // Out over each output's fade out, unless the next item on the stack says
    // overlap there (sync_audio, once it knows). An output that cuts holds
    // until then.
    deck.state = MediaDeck::State::Leaving;
    deck.fresh = true;
    for (const auto& oc : session_.outputs_on()) {
        if (const int ms = ms_on(out_ms, oc.id); ms > 0) {
            mixer_.fade_source(deck.sound.get(), oc.id, 0.0f, std::chrono::milliseconds(ms));
        }
    }
    const int longest = longest_ms(out_ms);
    if (longest > 0) {
        mixer_.fade_source(deck.sound.get(), {}, 0.0f, std::chrono::milliseconds(longest));
    }
    deck.until_ms = now + longest;
    return std::next(it);
}

PlaybackController::Decks::iterator PlaybackController::release(Decks::iterator it,
                                                               qint64 keep_picture_ms)
{
    auto& deck = it->second;
    place(deck, {});
    deck.player().unload();
    if (deck.video) {
        // Its last picture stays for the outgoing side of its change; a cued
        // one was on no output.
        const auto key = scene::content_video_key(it->first);
        content_frames_until_[key] = leaving_clock_.elapsed() + keep_picture_ms;
        cued_frames_.erase(key);
    }
    return decks_.erase(it);
}

void PlaybackController::settle_leaving_videos()
{
    for (auto it = decks_.begin(); it != decks_.end();) {
        auto& deck = it->second;
        if (!deck.video || deck.state != MediaDeck::State::Leaving || !deck.fresh) {
            ++it;
            continue;
        }
        deck.fresh = false;
        // Its sound goes with its picture on each output, over the change
        // there. A cut everywhere stops it now.
        const auto ms = session_.content_change_ms_by_output(deck.stack_id);
        if (longest_ms(ms) <= 0) {
            it = release(it);
            continue;
        }
        deck.until_ms = leaving_clock_.elapsed() + fade_sound(deck, 0.0f, ms);
        ++it;
    }
}

void PlaybackController::transport(std::uint64_t media_instance, bool play)
{
    const auto it = decks_.find(media_instance);
    if (it == decks_.end() || it->second.video) {
        return;
    }
    auto& deck = it->second;
    auto& sound = *deck.sound;
    // Only what the outputs play fades: a Preview's is heard on Listen as it
    // is.
    const bool live = deck.state == MediaDeck::State::Live && !deck.strip.empty();
    session::ByOutput<int> ms;
    if (live) {
        for (const auto& [output_id, fades] : session_.audio_fades_on(deck.stack_id)) {
            ms[output_id] =
                fades.on_play_pause ? (play ? fades.fade_in_ms : fades.fade_out_ms) : 0;
        }
    }
    if (play) {
        // Still sounding: a Pause was fading it out; up again from there.
        const bool sounding = sound.is_playing();
        deck.transport->set_pausing(false);
        deck.pause_at_ms = 0;
        if (live) {
            fade_sound(deck, 1.0f, ms, !sounding);
        } else if (deck.transport_down) {
            mixer_.set_source_level(deck.sound.get(), 1.0f);
        }
        deck.transport_down = false;
        if (!sounding) {
            sound.play();
        }
        start_tick();
        return;
    }
    if (!live || !sound.is_playing() || longest_ms(ms) <= 0) {
        sound.pause();
        return;
    }
    // Down on each output over its fade out (at once on one without), then
    // paused.
    deck.pause_at_ms = leaving_clock_.elapsed() + fade_sound(deck, 0.0f, ms);
    deck.transport->set_pausing(true);
    deck.transport_down = true;
    start_tick();
}

void PlaybackController::finish_pauses()
{
    const qint64 now = leaving_clock_.elapsed();
    for (auto& [instance, deck] : decks_) {
        if (deck.video || deck.pause_at_ms == 0 || now < deck.pause_at_ms) {
            continue;
        }
        deck.sound->pause();
        deck.transport->set_pausing(false);
        deck.pause_at_ms = 0;
    }
}

void PlaybackController::tick_leaving()
{
    const qint64 now = leaving_clock_.elapsed();
    bool cued_any = false;
    for (auto it = decks_.begin(); it != decks_.end();) {
        auto& deck = it->second;
        if (deck.state != MediaDeck::State::Leaving || deck.fresh) {
            ++it;
            continue;
        }
        // Played out, or stopped on its own (an audio item at its end): gone,
        // or back in the Preview a take sent it to.
        if (now >= deck.until_ms || (!deck.video && !deck.player().is_playing())) {
            const auto* preview = deck.then_cued ? cued_runtime(it->first) : nullptr;
            if (preview == nullptr) {
                it = release(it);
                continue;
            }
            cue(deck, *preview);
            cued_any = true;
            ++it;
            continue;
        }
        if (deck.video) {
            // The outgoing side of its stack's change, moving: its pictures
            // go out with the rest (publish_content_frames).
            deck.av->keep_in_step();
        }
        ++it;
    }
    if (cued_any) {
        // Each Preview whose item is back takes its player.
        session_.attach_players();
    }
}

void PlaybackController::sync_backends()
{
    // Asking for a player makes its deck.
    session_.attach_players();

    // What the session gave a player on a Program, every item still on a
    // Program, with a player or not, and what each Preview holds that has a
    // deck (a Preview is given none while its deck is still the outputs').
    std::map<std::uint64_t, const session::StackRuntime*> live;
    std::map<std::uint64_t, const session::StackRuntime*> cued;
    std::set<std::uint64_t> on_program;
    for (const auto& stack : session_.stacks()) {
        if (const auto instance = stack.program.media_instance(); instance != 0) {
            on_program.insert(instance);
            if (stack.program.media_player() != nullptr) {
                live.emplace(instance, &stack.program);
            }
        }
        if (stack.preview && decks_.contains(stack.preview->media_instance())) {
            cued.emplace(stack.preview->media_instance(), &*stack.preview);
        }
    }

    // What is on neither side any more plays out over its change, or stops;
    // what a take sent back to its Preview plays out there first.
    for (auto it = decks_.begin(); it != decks_.end();) {
        auto& deck = it->second;
        if (live.contains(it->first)) {
            ++it;
            continue;
        }
        const auto preview = cued.find(it->first);
        if (preview != cued.end()) {
            if (deck.state == MediaDeck::State::Live) {
                take_out(deck, *preview->second);
            } else if (deck.state == MediaDeck::State::Leaving) {
                deck.then_cued = true;
            }
            ++it;
            continue;
        }
        switch (deck.state) {
        case MediaDeck::State::Live:
            it = end_deck(it, !on_program.contains(it->first));
            break;
        case MediaDeck::State::Leaving:
            // Cleared from the Preview it was going back to: it only goes.
            deck.then_cued = false;
            ++it;
            break;
        case MediaDeck::State::Made:
        case MediaDeck::State::Cued:
            // On no output: it stops now.
            it = release(it, 0);
            break;
        }
    }
    // A Preview whose item was cued at once (a cut, or a still one) takes its
    // player.
    session_.attach_players();
    settle_leaving_videos();

    // What is live, loaded where it stands and playing or paused, and the
    // videos' pictures to the outputs.
    for (const auto& [instance, st] : live) {
        auto& deck = decks_.at(instance);
        if (deck.state == MediaDeck::State::Cued) {
            take_in(deck, *st);
        } else if (deck.state == MediaDeck::State::Leaving) {
            // An item that is live again before it had played out (a take
            // back and forth) keeps its deck, coming in again over its
            // stack's change.
            deck.fresh = false;
            deck.then_cued = false;
            fade_sound(deck, 1.0f, take_in_ms(deck, st->id));
        }
        deck.state = MediaDeck::State::Live;
        content_frames_until_.erase(scene::content_video_key(instance));
        if (deck.video) {
            sync_video(deck, *st);
        } else {
            sync_audio(deck, *st);
        }
    }
    // What is cued, loaded where it stands, paused unless played there.
    for (const auto& [instance, st] : cued) {
        auto& deck = decks_.at(instance);
        if (deck.state == MediaDeck::State::Leaving) {
            continue;
        }
        if (deck.state == MediaDeck::State::Made) {
            deck.state = MediaDeck::State::Cued;
        }
        if (deck.video) {
            sync_video(deck, *st);
        } else {
            sync_audio(deck, *st);
        }
    }
    publish_content_frames();
    // Nothing came after what an audio item left: it goes over each output's
    // fade out, and an output that cuts it, which held for an overlap, cuts
    // it now.
    for (auto& [instance, deck] : decks_) {
        if (deck.video || !deck.fresh) {
            continue;
        }
        deck.fresh = false;
        const auto out_ms = each_ms(deck.fades, &model::AudioFades::fade_out_ms);
        for (const auto& oc : session_.outputs_on()) {
            if (ms_on(out_ms, oc.id) <= 0) {
                mixer_.fade_source(deck.sound.get(), oc.id, 0.0f, std::chrono::milliseconds(0));
            }
        }
        if (longest_ms(out_ms) <= 0) {
            mixer_.fade_source(deck.sound.get(), {}, 0.0f, std::chrono::milliseconds(0));
        }
    }
    // What each audio item fades by, read while its stack still plays it:
    // once the stack has moved on, its fades are another item's.
    last_fades_.clear();
    for (const auto& [instance, st] : live) {
        if (!decks_.at(instance).video) {
            last_fades_[instance] = session_.audio_fades_on(st->id);
        }
    }
    if (any_audio_playing() || any_audio_opening() || any_leaving()) {
        start_tick();
    }
    sync_object_videos();
}

session::ByOutput<int> PlaybackController::take_in_ms(const MediaDeck& deck,
                                                      std::string_view stack_id) const
{
    return deck.video ? session_.content_change_ms_by_output(stack_id)
                      : each_ms(session_.audio_fades_on(stack_id), &model::AudioFades::fade_in_ms);
}

void PlaybackController::take_in(MediaDeck& deck, const session::StackRuntime& st)
{
    // From nothing on each output, up as a new item of its kind comes in
    // there; the move itself (sync_video, sync_audio) is unbroken.
    fade_sound(deck, 1.0f, take_in_ms(deck, st.id), true);
    deck.state = MediaDeck::State::Live;
}

void PlaybackController::take_out(MediaDeck& deck, const session::StackRuntime& st)
{
    // A video goes with the take's change on each output: the stack's there,
    // now that it shows what came in. Audio goes as each output's fades said
    // while it was live.
    session::ByOutput<int> ms;
    if (deck.video) {
        ms = session_.content_change_ms_by_output(st.id);
    } else if (const auto was = last_fades_.find(st.media_instance()); was != last_fades_.end()) {
        ms = each_ms(was->second, &model::AudioFades::fade_out_ms);
    }
    if (longest_ms(ms) <= 0 || deck.strip.empty() || !deck.player().is_playing()) {
        cue(deck, st);
        return;
    }
    deck.state = MediaDeck::State::Leaving;
    deck.fresh = false;
    deck.then_cued = true;
    deck.until_ms = leaving_clock_.elapsed() + fade_sound(deck, 0.0f, ms);
}

void PlaybackController::cue(MediaDeck& deck, const session::StackRuntime& st)
{
    const bool was_shown = deck.state == MediaDeck::State::Live
                           || deck.state == MediaDeck::State::Leaving;
    deck.state = MediaDeck::State::Cued;
    deck.then_cued = false;
    deck.fresh = false;
    if (deck.transport) {
        // A Pause fading out on the outputs is over: it is cued as it is.
        deck.transport->set_pausing(false);
        deck.pause_at_ms = 0;
        deck.transport_down = false;
    }
    if (was_shown) {
        // Off the stack's strip as any sound that stops goes, its next block
        // fading out there (what that reads ahead, the seek below takes
        // back), and on to the cue strip by the sync below.
        place(deck, {});
    }
    if (was_shown && deck.video) {
        // The outputs' outgoing side may still paint its last picture.
        content_frames_until_[scene::content_video_key(st.media_instance())] =
            leaving_clock_.elapsed()
            + std::max<qint64>(k_keep_last_picture_ms, session_.transition_ms_on(st.id));
    }
    if (was_shown && deck.player().has_media()) {
        // Back where it was taken off (or where the Preview moved it since).
        deck.player().seek(st.playback().position_sec);
    }
    if (deck.video) {
        sync_video(deck, st);
    } else {
        sync_audio(deck, st);
    }
    // Paused (or playing, as the Preview says) on the cue strip: heard there
    // as it is.
    mixer_.set_source_level(deck.sound.get(), 1.0f);
}

const session::StackRuntime* PlaybackController::cued_runtime(std::uint64_t media_instance) const
{
    for (const auto& stack : session_.stacks()) {
        if (stack.preview && stack.preview->media_instance() == media_instance) {
            return &*stack.preview;
        }
    }
    return nullptr;
}

bool PlaybackController::object_playing(std::string_view key) const
{
    const auto it = object_players_.find(key);
    return it != object_players_.end() && it->second.playing;
}

void PlaybackController::release_object_player(ObjectPlayer& player)
{
    if (!player.sound_stack.empty()) {
        mixer_.remove_source(player.sound.get());
        player.sound_stack.clear();
    }
    player.av->unload();
}

void PlaybackController::sync_object_videos()
{
    const auto wanted = session_.object_videos();
    const qint64 now = leaving_clock_.elapsed();
    // Those no slide shows: stopped, their last picture kept for the change.
    for (auto it = object_players_.begin(); it != object_players_.end();) {
        const bool shown = std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) {
            return w.video.key == it->first;
        });
        if (shown) {
            ++it;
            continue;
        }
        // As long as the change its stack makes now.
        const int change_ms = session_.transition_ms_on(it->second.stack_id);
        release_object_player(it->second);
        object_frames_until_[it->first] = now + std::max(0, change_ms);
        it = object_players_.erase(it);
    }
    for (const auto& [stack_id, video] : wanted) {
        auto it = object_players_.find(video.key);
        // Another showing starts it over; so does Loop, which a loaded player
        // cannot take.
        if (it != object_players_.end()
            && (it->second.run != video.run || it->second.loop != video.loop)) {
            release_object_player(it->second);
            object_players_.erase(it);
            it = object_players_.end();
        }
        if (it == object_players_.end()) {
            const auto path = library_.resolve_media_path(model::k_videos_subdir, video.filename);
            if (path.empty()) {
                qWarning("SeventhSlide: video file not found in the media folder: %s",
                         video.filename.c_str());
                continue;
            }
            ObjectPlayer player;
            player.picture = std::make_unique<render::MpvMediaPlayer>();
            player.sound = std::make_unique<audio::AudioFilePlayer>();
            player.av = std::make_unique<VideoWithSound>(*player.picture, *player.sound);
            player.path = path.string();
            player.loop = video.loop;
            player.run = video.run;
            if (!player.av->load(player.path, video.loop)) {
                qWarning("SeventhSlide: mpv failed to load video: %s", player.path.c_str());
                continue;
            }
            player.picture_due_ms = now + k_object_picture_wait_ms;
            object_frames_until_.erase(video.key);
            it = object_players_.emplace(video.key, std::move(player)).first;
        }
        auto& player = it->second;
        player.stack_id = stack_id;
        // Its sound on its stack's strip, as a Media item's.
        if (player.sound_stack != stack_id) {
            if (player.sound_stack.empty()) {
                mixer_.add_source(stack_id, player.sound.get(), model::k_item_type_video);
            } else {
                mixer_.move_source(player.sound.get(), stack_id);
            }
            player.sound_stack = stack_id;
        }
        if (video.mute != player.muted) {
            player.muted = video.mute;
            player.sound->fade_level(video.mute ? 0.0f : 1.0f, std::chrono::milliseconds(0));
        }
        if (video.plays != player.playing) {
            player.playing = video.plays;
            if (video.plays) {
                player.av->play();
            } else {
                // Stepped back past its click: at its start again, for the
                // click to come.
                player.av->pause();
                player.av->seek(0.0);
                player.picture_due_ms = now + k_object_picture_wait_ms;
            }
        }
    }
    if (object_videos_active() || !object_frames_until_.empty()) {
        start_tick();
    }
}

bool PlaybackController::object_videos_active() const
{
    const qint64 now = leaving_clock_.elapsed();
    return std::any_of(object_players_.begin(), object_players_.end(), [now](const auto& entry) {
        const auto& player = entry.second;
        if (!player.picture->has_media()) {
            return false;
        }
        return (player.playing && !player.av->ended()) || now < player.picture_due_ms;
    });
}

bool PlaybackController::publish_object_frames()
{
    bool moved = false;
    for (auto& [key, player] : object_players_) {
        if (player.playing) {
            player.av->keep_in_step();
        }
        QImage frame = player.picture->current_frame();
        if (frame.isNull()) {
            continue;
        }
        auto& shown = object_frames_[key].image;
        if (shown.cacheKey() != frame.cacheKey()) {
            shown = std::move(frame);
            player.picture_due_ms = 0;
            moved = true;
        }
    }
    const qint64 now = leaving_clock_.elapsed();
    for (auto it = object_frames_until_.begin(); it != object_frames_until_.end();) {
        if (now < it->second) {
            ++it;
            continue;
        }
        moved = object_frames_.erase(it->first) > 0 || moved;
        it = object_frames_until_.erase(it);
    }
    if (moved && windows_ != nullptr) {
        windows_->set_source_frames("video", object_frames_);
    }
    return moved;
}

void PlaybackController::sync_audio(MediaDeck& deck, const session::StackRuntime& st)
{
    const bool cued = deck.state == MediaDeck::State::Cued;
    deck.stack_id = st.id;
    // Its sound on the strip of the stack playing it, or its cue strip.
    place(deck, cued ? audio::cue_strip_id(st.id) : st.id);
    const auto& playback = st.playback();
    const auto path = library_.resolve_media_path(model::k_audio_subdir, playback.filename);
    if (path.empty()) {
        qWarning("SeventhSlide: audio file not found in the media folder: %s",
                 playback.filename.c_str());
        if (deck.sound->has_media()) {
            deck.sound->unload();
        }
        deck.loaded_path.clear();
        return;
    }
    const std::string load_path = path.string();
    if (cued && (!deck.sound->has_media() || deck.loaded_path != load_path)) {
        // Heard only on Listen, as it is.
        if (!deck.sound->load(load_path, playback.loop, playback.position_sec)) {
            qWarning("SeventhSlide: failed to load audio: %s", load_path.c_str());
            deck.loaded_path.clear();
            return;
        }
        deck.loaded_path = load_path;
        mixer_.set_source_level(deck.sound.get(), 1.0f);
    } else if (!deck.sound->has_media() || deck.loaded_path != load_path) {
        // How each output's fades bring this item in: over what was just
        // let go on this stack, it overlaps it there, or cuts it where the
        // overlap is none.
        const auto fades = session_.audio_fades_on(st.id);
        const auto overlap = each_ms(fades, &model::AudioFades::overlap_ms);
        bool overlapping = false;
        for (auto& [instance, going] : decks_) {
            if (going.video || going.state != MediaDeck::State::Leaving || !going.fresh
                || going.stack_id != st.id) {
                continue;
            }
            going.fresh = false;
            going.until_ms = leaving_clock_.elapsed() + fade_sound(going, 0.0f, overlap);
            overlapping = true;
        }
        // Where the item stands: the start, or where it was.
        if (!deck.sound->load(load_path, playback.loop, playback.position_sec)) {
            qWarning("SeventhSlide: failed to load audio: %s", load_path.c_str());
            deck.loaded_path.clear();
            return;
        }
        deck.loaded_path = load_path;
        // Up over each output's fade in, or its overlap where it overlaps
        // the one going and that is longer.
        auto in_ms = each_ms(fades, &model::AudioFades::fade_in_ms);
        if (overlapping) {
            for (auto& [output_id, ms] : in_ms) {
                ms = std::max(ms, ms_on(overlap, output_id));
            }
        }
        fade_sound(deck, 1.0f, in_ms, true);
    }
    if (playback.is_playing) {
        deck.sound->play();
    } else if (deck.pause_at_ms == 0) {
        // One fading out to a Pause pauses when its fade is over
        // (finish_pauses).
        deck.sound->pause();
    }
}

bool PlaybackController::any_audio_playing() const
{
    return std::any_of(decks_.begin(), decks_.end(), [](const auto& entry) {
        const auto& deck = entry.second;
        return !deck.video && shown(deck) && deck.sound->is_playing();
    });
}

bool PlaybackController::any_audio_opening() const
{
    return std::any_of(decks_.begin(), decks_.end(), [](const auto& entry) {
        const auto& deck = entry.second;
        if (deck.video || !shown(deck)) {
            return false;
        }
        // Once the decoder has filled what it keeps ahead (or ended, or
        // failed) it has opened the file: a length not known by then is known
        // only at the file's end.
        const auto& player = *deck.sound;
        return player.has_media() && player.duration_sec() <= 0.0
               && !player.wait_buffered(std::chrono::milliseconds(0));
    });
}

void PlaybackController::ensure_bg_video_player()
{
    if (bg_video_player_) {
        return;
    }
    bg_video_player_ = std::make_unique<render::MpvMediaPlayer>();
}

void PlaybackController::follow_seek()
{
    // Audio paints nothing, so its seek moves only the clock. A cued video
    // is drawn in its well alone, but the outputs repaint as cheaply as a
    // tick does.
    const auto& stack = session_.focused_stack();
    const auto shows_video = [](const session::StackRuntime* st) {
        return st != nullptr && st->video_mode_active() && st->media_player() != nullptr;
    };
    if (shows_video(&stack.program) || shows_video(stack.side(session::StackSide::Preview))) {
        publish_content_frames();
        emit previewChanged();
        emit repaintRequested();
    }
    // A seek can change what plays (mpv may play on from its end), and the
    // tick stops itself again when nothing needs it.
    announce_clock();
    start_tick();
}

void PlaybackController::sync_video(MediaDeck& deck, const session::StackRuntime& st)
{
    const bool cued = deck.state == MediaDeck::State::Cued;
    deck.stack_id = st.id;
    // Its sound on the strip of the stack showing it, or its cue strip.
    place(deck, cued ? audio::cue_strip_id(st.id) : st.id);
    const auto& v = st.playback();
    const std::filesystem::path path = library_.resolve_media_path(model::k_videos_subdir, v.filename);
    if (path.empty()) {
        qWarning("SeventhSlide: video file not found in the media folder: %s", v.filename.c_str());
        if (deck.picture->has_media() || deck.sound->has_media()) {
            deck.av->unload();
        }
        deck.loaded_path.clear();
        content_frames_.erase(scene::content_video_key(st.media_instance()));
        cued_frames_.erase(scene::content_video_key(st.media_instance()));
        return;
    }
    const std::string load_path = path.string();
    if (!deck.picture->has_media() || deck.loaded_path != load_path) {
        content_frames_.erase(scene::content_video_key(st.media_instance()));
        cued_frames_.erase(scene::content_video_key(st.media_instance()));
        // Where the item stands: the start, or where it was.
        if (!deck.av->load(load_path, v.loop, v.position_sec)) {
            qWarning("SeventhSlide: mpv failed to load video: %s", load_path.c_str());
            deck.loaded_path.clear();
            return;
        }
        deck.loaded_path = load_path;
        qInfo("SeventhSlide: loaded video %s", load_path.c_str());
        // Its sound comes in with its picture on each output, over its
        // entrance there; a cued one's is heard on Listen, as it is.
        if (cued) {
            mixer_.set_source_level(deck.sound.get(), 1.0f);
        } else {
            fade_sound(deck, 1.0f, session_.content_change_ms_by_output(st.id), true);
        }
    }
    if (v.is_playing) {
        deck.av->play();
    } else {
        deck.av->pause();
    }
    // Playing, or still waiting on the picture it holds (loads are
    // asynchronous): the tick pulls its pictures, and stops itself once
    // every video is settled.
    start_tick();
}

bool PlaybackController::publish_content_frames()
{
    // Reads every stack's clock back, audio included.
    session_.sync_playback();
    bool moved = false;
    for (const auto& [instance, deck] : decks_) {
        if (!deck.video || deck.state == MediaDeck::State::Made) {
            continue;
        }
        // A cued video's to the previews alone. What a take sent back keeps
        // its last picture on the outputs until the take's change is over.
        const auto key = scene::content_video_key(instance);
        const bool cued = deck.state == MediaDeck::State::Cued;
        if (!cued) {
            cued_frames_.erase(key);
        }
        auto& shown = (cued ? cued_frames_ : content_frames_)[key];
        QImage picture = deck.picture->current_frame();
        if (!picture.isNull() && picture.cacheKey() != shown.image.cacheKey()) {
            shown.image = std::move(picture);
            moved = true;
        }
        // The clock as of that picture: what a countdown with it counts down.
        const auto& player = deck.player();
        if (player.has_media()) {
            const double position = player.position_sec();
            const double duration = player.duration_sec();
            shown.clock = render::SourceClock{std::isfinite(position) ? position : 0.0,
                                              std::isfinite(duration) ? duration : 0.0};
        }
    }
    // A video let go keeps its last picture until its change is over.
    const qint64 now = leaving_clock_.elapsed();
    for (auto it = content_frames_until_.begin(); it != content_frames_until_.end();) {
        if (now < it->second) {
            ++it;
            continue;
        }
        moved = content_frames_.erase(it->first) > 0 || moved;
        it = content_frames_until_.erase(it);
    }
    // Published every time: a destination that started since the last tick
    // still needs the held pictures, and a presenter ignores a repeat itself.
    if (windows_ != nullptr) {
        windows_->set_source_frames("content", content_frames_);
    }
    return moved;
}

void PlaybackController::sync_background_video(const std::vector<scene::SharedSceneFrame>& frames)
{
    ensure_bg_video_player();
    if (!bg_video_player_) {
        return;
    }
    const auto stop_background_video = [this] {
        if (bg_video_player_->has_media()) {
            bg_video_player_->unload();
        }
        bg_video_loaded_path_.clear();
        background_video_frame_ = {};
        if (windows_ != nullptr) {
            windows_->set_background_video_frame({});
        }
        // The tick is left be: audio playing or opening still needs it, and
        // with nothing to drive it stops itself on its next pass.
    };

    std::string url;
    for (const auto& frame : frames) {
        if (frame && frame->background.type == layout::BackgroundType::Video
            && !frame->background.video.empty()) {
            url = frame->background.video;
            break;
        }
    }
    if (url.empty()) {
        stop_background_video();
        return;
    }

    std::filesystem::path path;
    const auto root = library_.media_root();
    if (!root.empty()) {
        if (const auto resolved = render::resolve_static_asset(url, root)) {
            path = *resolved;
        }
    }
    const std::string load_path = path.empty() ? std::string() : path.string();
    if (load_path.empty()) {
        stop_background_video();
        return;
    }
    if (!bg_video_player_->has_media() || bg_video_loaded_path_ != load_path) {
        if (!bg_video_player_->load(load_path, true)) {
            bg_video_loaded_path_.clear();
            return;
        }
        bg_video_loaded_path_ = load_path;
    }
    bg_video_player_->play();
    start_tick();
}

void PlaybackController::start_tick()
{
    if (tick_ && !tick_->isActive()) {
        tick_->start();
    }
}

void PlaybackController::stop_tick()
{
    if (tick_ && tick_->isActive()) {
        tick_->stop();
    }
}

void PlaybackController::on_tick()
{
    finish_pauses();
    const bool videos = any_video_shown();
    const bool audio = any_audio_playing();
    const bool opening = any_audio_opening();
    const bool background = bg_video_player_ && bg_video_player_->has_media();
    const bool objects = object_videos_active() || !object_frames_until_.empty();
    tick_leaving();
    const bool leaving = any_leaving();
    const bool active = videos || audio || opening || background || objects || leaving;
    bool frame_changed = publish_object_frames();
    for (auto& [instance, deck] : decks_) {
        if (deck.video && shown(deck)) {
            deck.av->keep_in_step();
        }
    }
    if (videos || leaving) {
        frame_changed = publish_content_frames() || frame_changed;
    } else if (audio) {
        session_.sync_playback();
    }
    for (auto& [instance, deck] : decks_) {
        if (!deck.video && !deck.loaded_path.empty() && !deck.sound->has_media()) {
            qWarning("SeventhSlide: could not decode audio: %s", deck.loaded_path.c_str());
            deck.loaded_path.clear();
        }
    }
    if (background) {
        const auto before = background_video_frame_.cacheKey();
        QImage fr = bg_video_player_->current_frame();
        if (!fr.isNull()) {
            background_video_frame_ = std::move(fr);
        }
        if (background_video_frame_.cacheKey() != before) {
            frame_changed = true;
        }
        if (windows_ != nullptr) {
            windows_->set_background_video_frame(background_video_frame_);
        }
    }
    if (!active) {
        // What last ran (audio reaching its end) shows where it stopped.
        session_.sync_playback();
        announce_clock();
        stop_tick();
        return;
    }
    // Windows and sinks already have the new frame images above; the scene
    // frames themselves only moved in playback position, which is not painted.
    for (auto& [instance, deck] : decks_) {
        if (deck.video && shown(deck) && !deck.picture->has_media()
            && !deck.loaded_path.empty()) {
            qWarning("SeventhSlide: mpv could not open video: %s", deck.loaded_path.c_str());
            deck.loaded_path.clear();
        }
    }
    // Only the frame images moved, and only when a clip actually advanced:
    // a repeat frame would reload every operator preview for the same pixels.
    if (frame_changed) {
        emit previewChanged();
        emit videoFrameChanged();
    }
    // The clock moves on every tick, repeat frame or not.
    announce_clock();
    // Still clips need ticks only until their pictures arrive, or until the
    // player gives up on a file; audio only while it plays or opens, and a
    // background video always.
    if (videos && shown_videos_settled() && !audio && !opening && !background && !objects
        && !leaving) {
        stop_tick();
    }
}

PlaybackController::ClockState PlaybackController::clock_state() const
{
    const auto& focused = session_.focused_stack();
    ClockState state{focused.program.is_playing(), focused.program.media_duration_sec()};
    if (focused.preview) {
        state.cued_playing = focused.preview->is_playing();
        state.cued_duration_sec = focused.preview->media_duration_sec();
    }
    return state;
}

void PlaybackController::media_state_announced()
{
    last_clock_ = clock_state();
}

void PlaybackController::announce_clock()
{
    emit mediaPositionChanged();
    // Not only what a tick last announced: a change of live content announces
    // the media state too, and a new item's length may not be known yet then
    // (its file still opening). Replaying an item reads 0 then, and its
    // length, once known, is the same the tick last announced.
    const ClockState now = clock_state();
    if (now != last_clock_) {
        last_clock_ = now;
        emit mediaStateChanged();
    }
}

} // namespace seventhslide::app
