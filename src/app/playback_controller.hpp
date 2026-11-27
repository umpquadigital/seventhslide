#pragma once

// The players behind live media - each video and audio item, the theme
// background video and the video objects on Freeform slides - and the
// transport commands that drive them.
// A video or audio item plays on a MediaDeck of its own, filed by the item
// (``StackRuntime::media_instance``) and made when the session first asks for
// its player. Each sync works out from the session what every deck does: Cued
// while its item is on a Preview, Live while it is on a Program, Leaving over
// the change after it goes from one, then gone, or Cued again when
// a take sent it back to its Preview. Nothing waits for the session to say a
// player is going.
// Sound plays through the Audio Mixer: a deck's sound is a source on the strip
// of the stack playing it, or on the stack's cue strip while it is cued, which
// only Listen hears. mpv draws a video's picture only, kept in step with that
// sound (VideoWithSound). A Live video's pictures go to the outputs; a Cued
// one's only to the previews.
// Each output fades a deck's sound by its own (``audio::Mixer::fade_source``):
// a video's over that output's change, with its picture; an audio item's by
// its fades on that output (``model::AudioFades``), the transport's Play and
// Pause too when they say so (AudioTransport).
//
// SessionFacade owns one. Previews read the video frames from here. What a
// well shows of a side's video or audio - playing, position - and its
// transport are SideView's, on the runtime's own player. What plays comes
// from the session and its file from the open library's media folder;
// previewChanged and repaintRequested tell the application what to follow.

#include "audio_transport.hpp"
#include "open_library.hpp"
#include "video_with_sound.hpp"

#include <seventhslide/audio/audio_file_player.hpp>
#include <seventhslide/render/mpv_media_player.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/session/session.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QTimer;

namespace seventhslide::audio {
class Mixer;
}

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class PlaybackController : public QObject {
    Q_OBJECT

public:
    /// Audio players are added to ``mixer`` as their stack's sources; it must
    /// outlive this.
    PlaybackController(session::Session& session, OpenLibrary& library, audio::Mixer& mixer,
                       QObject* parent = nullptr);
    ~PlaybackController() override;

    /// The windows the frames go to; null detaches (tests, headless). They
    /// are handed the videos' latest pictures at once.
    void set_output_host(render::OutputWindowHost* host);

    /// Every video item's latest picture and clock, by its source
    /// (``scene::content_video_key``): what the outputs are given as the
    /// ``"content"`` provider's frames, and the previews paint from. A video
    /// that has gone keeps its last picture until its change is over.
    [[nodiscard]] const render::SourceFrames& content_frames() const noexcept
    {
        return content_frames_;
    }
    /// The latest picture and clock of every video cued in a Preview, by its
    /// source: what a Preview's well and slides paint, and never the outputs.
    [[nodiscard]] const render::SourceFrames& cued_frames() const noexcept
    {
        return cued_frames_;
    }
    /// The latest picture of the video item whose source is ``source``
    /// (``scene::content_video_key``), live or cued: the published one, or one
    /// pulled from its player while none has been. Null with no such video.
    [[nodiscard]] QImage video_picture(std::string_view source) const;
    [[nodiscard]] render::MpvMediaPlayer* background_player() const noexcept
    {
        return bg_video_player_.get();
    }
    [[nodiscard]] const QImage& background_frame() const noexcept
    {
        return background_video_frame_;
    }

    /// A well's video or audio was sought (``SideView::sought``), either side:
    /// draw the picture held there again, and move the clock. Nothing went
    /// live, so nothing is synced.
    void follow_seek();
    /// The focused stack's media state was announced, by whatever route (a
    /// change of live content, a change of focus, a tick): what was read then
    /// is what the next tick compares with, so a length learned after it is
    /// announced even when it matches one an earlier item had.
    void media_state_announced();

    /// After live or cued content changed: hand the session's players out
    /// again, and bring every deck to what the session now says: load and
    /// start or pause what is live or cued, let what has gone play out over
    /// its change or stop, take what a take moved to its new side. A
    /// theme's background video is ``sync_background_video``'s, from the
    /// frames the outputs are given.
    void sync_backends();
    /// Play the background video the first of ``frames`` shows (a theme's),
    /// or stop the one playing when none does. It plays beside a content video
    /// for an output with something else on top: a video's own frame has a plain
    /// background. Takes the frames composed for the outputs, so nothing is
    /// composed only to find it.
    void sync_background_video(const std::vector<scene::SharedSceneFrame>& frames);
    /// The video objects' latest pictures, by ``scene::video_object_key``.
    [[nodiscard]] const render::SourceFrames& object_frames() const noexcept
    {
        return object_frames_;
    }
    /// Video objects with a player right now (tests).
    [[nodiscard]] std::size_t object_player_count() const noexcept
    {
        return object_players_.size();
    }
    /// Whether the video object with source ``key`` is playing (tests).
    [[nodiscard]] bool object_playing(std::string_view key) const;
    /// Audio items live with a player right now (tests).
    [[nodiscard]] std::size_t audio_player_count() const noexcept
    {
        return count_decks(false, MediaDeck::State::Live);
    }
    /// Audio items fading out after their stack moved on; for tests.
    [[nodiscard]] std::size_t leaving_count() const noexcept
    {
        return count_decks(false, MediaDeck::State::Leaving);
    }
    /// Videos still playing on through the transition that replaced them;
    /// for tests.
    [[nodiscard]] std::size_t leaving_video_count() const noexcept
    {
        return count_decks(true, MediaDeck::State::Leaving);
    }
    /// Video and audio items cued in a Preview with a player; for tests.
    [[nodiscard]] std::size_t cued_count() const noexcept
    {
        return count_decks(false, MediaDeck::State::Cued)
               + count_decks(true, MediaDeck::State::Cued);
    }
    /// The level stack ``stack_id``'s live audio plays at now, 0 to 1, on
    /// output ``output_id``, or on the stack's strip (the loudest output) for
    /// none; -1 with none live. For tests.
    [[nodiscard]] float audio_level(std::string_view stack_id,
                                    std::string_view output_id = {}) const;

signals:
    /// The focused stack's media, on either side, started or stopped
    /// playing, or learned its length. Not the clock: that is
    /// mediaPositionChanged.
    void mediaStateChanged();
    /// The focused stack's clock moved: every tick while media plays, and a
    /// seek.
    void mediaPositionChanged();
    void videoFrameChanged();
    /// A new video frame was published: previews reload.
    void previewChanged();
    /// Repaint the outputs now (a scrub moved the held frame).
    void repaintRequested();

private:
    /// What plays one video or audio item: a video's picture and its sound,
    /// joined (VideoWithSound), or a sound alone. Filed by the item's
    /// ``media_instance``; the session holds ``player()`` while its item is
    /// on either side of a stack.
    struct MediaDeck {
        enum class State {
            /// Just made for the session: the next sync says which side its
            /// item is on.
            Made,
            /// Its item is on a Preview: paused unless the operator plays it
            /// there, its sound on its stack's cue strip, a video's picture to
            /// the previews only.
            Cued,
            /// Its item is on a Program and the session holds its player: its
            /// sound on its stack's strip, a video's picture on the outputs.
            Live,
            /// Its item has just left the Program it played on: it plays on
            /// over the change, fading on its strip, then goes, or
            /// is Cued again (``then_cued``).
            Leaving,
        };
        bool video = false;
        std::unique_ptr<render::MpvMediaPlayer> picture;  ///< a video's only
        std::unique_ptr<audio::AudioFilePlayer> sound;
        std::unique_ptr<VideoWithSound> av;               ///< a video's only
        /// What the session drives of an audio item's: its transport's Play
        /// and Pause come through ``transport``.
        std::unique_ptr<AudioTransport> transport;        ///< audio's only
        /// A Pause fading out: when it pauses (``leaving_clock_``); 0 for none.
        qint64 pause_at_ms = 0;
        /// A Pause faded it out: its levels are down until Play brings them
        /// up again.
        bool transport_down = false;
        State state = State::Made;
        std::string loaded_path;
        /// The stack it plays on, or played on last.
        std::string stack_id;
        /// The stack whose strip ``sound`` is on; empty while off the mixer.
        std::string strip;
        /// Leaving: when it stops.
        qint64 until_ms = 0;
        /// Leaving since this sync: how long it goes on is settled once the
        /// stack's new content is known (an audio item's overlap, the change
        /// a video goes out with).
        bool fresh = false;
        /// Leaving: a take sent its item back to its stack's Preview, where it
        /// is Cued once it has played out.
        bool then_cued = false;
        /// Leaving audio: the fades it went with, on each output.
        session::ByOutput<model::AudioFades> fades;

        /// What the session drives: the joined pair, or the audio transport.
        [[nodiscard]] media::IMediaPlayer& player() noexcept;
        [[nodiscard]] const media::IMediaPlayer& player() const noexcept;
    };
    using Decks = std::map<std::uint64_t, MediaDeck>;

    /// The player for one item on ``side`` of its stack (``Session::PlayerFor``):
    /// its deck's, the deck made the first time it is asked for. None for a
    /// Preview while the deck is still the outputs' (Live, or playing out
    /// after a take): what the Preview is told meanwhile waits in its runtime,
    /// and ``cue`` takes it up.
    media::IMediaPlayer* player_for(std::uint64_t media_instance,
                                    model::ContentKind kind,
                                    session::StackSide side);
    /// Put ``deck``'s sound on stack ``stack_id``'s strip (empty: off the
    /// mixer).
    void place(MediaDeck& deck, std::string_view stack_id);
    /// ``it``'s item is no longer live. Gone from every Program while playing,
    /// it plays out over the change when it fades (``Leaving``); otherwise,
    /// or still on a Program but given no player, it stops now. Returns the
    /// next deck.
    Decks::iterator end_deck(Decks::iterator it, bool gone);
    /// Stop ``it`` and take it off the mixer. A video's last picture stays
    /// ``keep_picture_ms`` for the change it went out with. Returns the next
    /// deck.
    Decks::iterator release(Decks::iterator it, qint64 keep_picture_ms = k_keep_last_picture_ms);
    /// How long a video stopped keeps its last picture at least: the outgoing
    /// side of the change it went out with, which may run a little past the
    /// deck.
    static constexpr qint64 k_keep_last_picture_ms = 1000;
    /// Give each video that has just started Leaving its length: the change
    /// its stack makes to what it shows now. A cut stops it.
    void settle_leaving_videos();
    /// How long ``deck``, taken onto stack ``stack_id``'s Program, comes up
    /// over on each output: the stack's change there for a video, the
    /// output's fade in for audio, as either would come in sent there.
    [[nodiscard]] session::ByOutput<int> take_in_ms(const MediaDeck& deck,
                                                    std::string_view stack_id) const;
    /// Move ``deck``'s sound to ``target`` on every output, each over its own
    /// time in ``ms`` (at once on one ``ms`` does not name), from nothing
    /// first where it takes time when ``from_nothing``; and where no output
    /// carries it from (a cue strip) over the longest. Returns the longest.
    int fade_sound(const MediaDeck& deck,
                   float target,
                   const session::ByOutput<int>& ms,
                   bool from_nothing = false);
    /// A take moved ``deck``'s item to its stack's Program: its sound to the
    /// stack's strip, coming up from nothing (``take_in_ms``). Nothing loads
    /// or seeks: it plays on as it was.
    void take_in(MediaDeck& deck, const session::StackRuntime& st);
    /// A take sent ``deck``'s item back to its stack's Preview: playing, it
    /// plays on, fading on each output over the take's change there (a video)
    /// or the output's fade out (audio), Leaving, then is Cued; still, or cut
    /// on every output, it is Cued now.
    void take_out(MediaDeck& deck, const session::StackRuntime& st);
    /// ``deck`` is Cued on ``st``, its Preview: on the cue strip at full level,
    /// where ``st`` says it stands, playing or paused as it says. ``st`` is
    /// handed the player at the next ``Session::attach_players``.
    void cue(MediaDeck& deck, const session::StackRuntime& st);
    /// The Preview runtime holding ``media_instance``; null when none does.
    [[nodiscard]] const session::StackRuntime* cued_runtime(std::uint64_t media_instance) const;
    /// Load what ``st`` plays into its deck where it stands, and play or
    /// pause it, as a content video or an audio item, on the strip its state
    /// says. A Live one comes in as its stack's change or theme says; a Cued
    /// one is at full level at once.
    void sync_video(MediaDeck& deck, const session::StackRuntime& st);
    void sync_audio(MediaDeck& deck, const session::StackRuntime& st);
    /// Move what is Leaving on, and stop what has played out.
    void tick_leaving();
    /// The transport's Play (``play``) or Pause on audio item
    /// ``media_instance``: on the outputs, it fades on each that its fades
    /// say so for (``on_play_pause``), over their fade in or out, and is at
    /// once on the rest; in a Preview, at once.
    void transport(std::uint64_t media_instance, bool play);
    /// Pause what a Pause has faded out.
    void finish_pauses();
    [[nodiscard]] std::size_t count_decks(bool video, MediaDeck::State state) const noexcept;
    /// A video or sound is Leaving, or a video gone still shows its last
    /// picture: the tick runs until they are done.
    [[nodiscard]] bool any_leaving() const noexcept;
    /// A video is Live or Cued: it has pictures to pull.
    [[nodiscard]] bool any_video_shown() const noexcept;
    /// Every Live or Cued video is still and shows its picture (or its player
    /// gave up): nothing to pull until something changes.
    [[nodiscard]] bool shown_videos_settled() const noexcept;

    /// A video object's player (4.7's 6): its picture and its sound, as the
    /// content video's, one for each object the slides show.
    struct ObjectPlayer {
        std::unique_ptr<render::MpvMediaPlayer> picture;
        std::unique_ptr<audio::AudioFilePlayer> sound;
        std::unique_ptr<VideoWithSound> av;
        std::string path;
        std::string stack_id;
        /// The stack whose strip its sound is on. A muted one stays there at
        /// no level: the mixer reading it is what runs the sound's clock,
        /// which the picture follows.
        std::string sound_stack;
        bool muted = false;
        bool loop = false;
        /// ``SceneObjectVideo::run``: another starts the clip over.
        std::uint64_t run = 0;
        bool playing = false;
        /// Tick until a new picture comes (``leaving_clock_``), or this
        /// passes; 0 when it has come.
        qint64 picture_due_ms = 0;
    };
    /// Give each video object the slides show a player, started or held on
    /// its first picture as the slide says; stop the ones no slide shows.
    void sync_object_videos();
    void release_object_player(ObjectPlayer& player);
    /// Read the video objects' pictures; true when one moved. Pictures of
    /// objects gone stay until their slide's change is over.
    bool publish_object_frames();
    /// A video object is playing, or waiting on its first picture.
    [[nodiscard]] bool object_videos_active() const;

    void ensure_bg_video_player();
    [[nodiscard]] bool any_audio_playing() const;
    /// A stack's audio file is still opening: its length may arrive on any
    /// tick, so the clock ticks until it does, playing or not.
    [[nodiscard]] bool any_audio_opening() const;
    /// Read every video deck's picture and clock into ``content_frames_``
    /// (or ``cued_frames_`` while it is Cued), let go of gone videos'
    /// pictures whose moment has passed, and hand the outputs theirs. True
    /// when a picture moved.
    bool publish_content_frames();
    void start_tick();
    void stop_tick();
    void on_tick();
    /// The clock moved: announce it, and the play state and length if they
    /// changed since a tick last looked.
    void announce_clock();

    session::Session& session_;
    OpenLibrary& library_;
    audio::Mixer& mixer_;
    render::OutputWindowHost* windows_ = nullptr;
    /// Every item's deck, by ``media_instance``.
    Decks decks_;
    render::SourceFrames content_frames_;
    render::SourceFrames cued_frames_;
    /// The pictures of videos let go, kept until then (``leaving_clock_``)
    /// for the outgoing side of their change.
    std::map<std::string, qint64, std::less<>> content_frames_until_;
    std::unique_ptr<render::MpvMediaPlayer> bg_video_player_;
    QImage background_video_frame_;
    /// Pulls video frames and moves the clocks while anything plays.
    QTimer* tick_ = nullptr;
    /// The focused stack's media as the media state announces it: its
    /// Program's and its Preview's.
    struct ClockState {
        bool playing = false;
        double duration_sec = 0.0;
        bool cued_playing = false;
        double cued_duration_sec = 0.0;
        bool operator==(const ClockState&) const = default;
    };
    [[nodiscard]] ClockState clock_state() const;
    /// What the media state last announced (media_state_announced).
    ClockState last_clock_;
    std::string bg_video_loaded_path_;
    QElapsedTimer leaving_clock_;
    /// Each live audio item's fades on each output, by its instance, as of
    /// the last sync.
    std::map<std::uint64_t, session::ByOutput<model::AudioFades>> last_fades_;
    /// By ``scene::video_object_key``.
    std::map<std::string, ObjectPlayer, std::less<>> object_players_;
    render::SourceFrames object_frames_;
    /// The pictures of objects no slide shows any more, kept until then
    /// (``leaving_clock_``), for the outgoing side of their change.
    std::map<std::string, qint64, std::less<>> object_frames_until_;
};

} // namespace seventhslide::app
