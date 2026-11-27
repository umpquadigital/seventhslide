#pragma once

// One side of a Stack: its content and line cursor. Session owns a Stack per
// defined stack (its Program, and a Preview when it has one), composites the
// Programs per output, and applies blank / freeze on top.

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/media/media_player.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/scene/scene_builder.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/session/player_controller.hpp"
#include "seventhslide/theme/theme_resolver.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::session {

/// Operator-facing play context. Derived from ContentKind: a rundown row
/// reports Service, and an image folder reports Image. Not a second content
/// vocabulary.
enum class SessionMode {
    Song,
    Bible,
    Service,
    Freeform,
    Video,
    Audio,
    Image,
    Web,
    Live,
    Empty,
};

[[nodiscard]] constexpr SessionMode session_mode_for(model::ContentKind kind,
                                                     bool from_service) noexcept
{
    if (from_service) {
        return SessionMode::Service;
    }
    switch (kind) {
    case model::ContentKind::Song:
        return SessionMode::Song;
    case model::ContentKind::Bible:
        return SessionMode::Bible;
    case model::ContentKind::Freeform:
        return SessionMode::Freeform;
    case model::ContentKind::Video:
        return SessionMode::Video;
    case model::ContentKind::Audio:
        return SessionMode::Audio;
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
        return SessionMode::Image;
    case model::ContentKind::Web:
        return SessionMode::Web;
    case model::ContentKind::Live:
        return SessionMode::Live;
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        return SessionMode::Empty;
    }
    return SessionMode::Empty;
}

struct LiveFreeformContent {
    std::string name;
    freeform::Document document;
    /// Library item this was played from. Empty for a rundown row, which
    /// carries its own snapshot of the document and is not tied to the library.
    std::optional<std::int64_t> library_id;
};

struct LiveMediaContent {
    model::ContentKind kind = model::ContentKind::Empty;
    nlohmann::json data = nlohmann::json::object();
};

struct ImagePlaylist {
    std::optional<std::int64_t> folder_id;
    std::string folder_name;
    std::vector<std::string> images;
    int index = 0;

    [[nodiscard]] int count() const noexcept { return static_cast<int>(images.size()); }
    [[nodiscard]] bool empty() const noexcept { return images.empty(); }
};

/// A stack's video or audio file, and how far it has played.
struct MediaPlayback {
    /// Which piece of content this is: new with each video or audio item
    /// loaded, unique across every runtime, and kept while the item stays (a
    /// refresh of it, an edit of its service row). The session asks for its
    /// player by it, and the app files an audio item's player under it. 0
    /// with no file: such an item gets no player.
    std::uint64_t instance = 0;
    std::string filename;
    std::string title;
    bool loop = false;
    bool autoplay_intent = true;
    bool is_playing = false;
    double position_sec = 0.0;
    /// Its length as its player last said; 0 until one has.
    double duration_sec = 0.0;

    [[nodiscard]] bool empty() const noexcept { return filename.empty(); }
};

struct StackBuildContext {
    layout::ITextMeasurer& measurer;
    /// The outputs on (``Session::outputs_on``): the only ones built.
    const std::vector<model::OutputConfig>& outputs;
    const std::vector<std::string>& theme_priority;
    const std::string& ccli_licence_number;
    /// The open service's theme picks (none when no service is open).
    const theme::OutputThemeMap& service_themes;
};

class StackRuntime {
public:
    std::string id;

    [[nodiscard]] bool occupied() const noexcept;
    [[nodiscard]] SessionMode mode() const noexcept
    {
        return session_mode_for(kind_, from_service_);
    }
    [[nodiscard]] model::ContentKind content_kind() const noexcept { return kind_; }
    [[nodiscard]] bool has_build() const noexcept { return has_build_; }
    /// Changes whenever the built slides or their scene extras change. Unique
    /// across stacks, so it can key cached renders of built slides.
    [[nodiscard]] std::uint64_t build_id() const noexcept { return build_id_; }

    [[nodiscard]] const PlayerController& player() const noexcept { return player_; }
    [[nodiscard]] PlayerController& player() noexcept { return player_; }
    [[nodiscard]] const build::SlideBuildResult& last_build() const noexcept { return last_build_; }
    [[nodiscard]] const scene::SceneExtras& scene_extras() const noexcept { return scene_extras_; }
    void set_scene_extras(scene::SceneExtras extras);

    [[nodiscard]] const model::Song& song() const noexcept { return song_; }
    [[nodiscard]] const model::BiblePassage& bible() const noexcept { return bible_; }
    [[nodiscard]] const LiveFreeformContent& freeform() const noexcept { return freeform_; }
    [[nodiscard]] const LiveMediaContent& media() const noexcept { return media_; }
    [[nodiscard]] const ImagePlaylist& image_playlist() const noexcept { return image_playlist_; }
    /// The video or audio file live here; empty for anything else.
    [[nodiscard]] const MediaPlayback& playback() const noexcept { return playback_; }
    /// What this runtime's video or audio is played by is filed under
    /// (``MediaPlayback::instance``); 0 with none.
    [[nodiscard]] std::uint64_t media_instance() const noexcept { return playback_.instance; }

    /// The player this runtime's video or audio plays on, handed out by
    /// Session; null when it has none (no file, or not video or audio).
    void set_media_player(media::IMediaPlayer* player) noexcept { media_player_ = player; }
    [[nodiscard]] media::IMediaPlayer* media_player() const noexcept { return media_player_; }

    void play_song(model::Song song, const StackBuildContext& ctx);
    void play_bible(model::BiblePassage bible, const StackBuildContext& ctx);
    void play_freeform(LiveFreeformContent content, const StackBuildContext& ctx);
    /// Swap in a newer version of the library Freeform this stack is showing,
    /// keeping the stack's place. No-op (false) unless the stack is showing
    /// library item ``content.library_id``.
    bool refresh_freeform(LiveFreeformContent content, const StackBuildContext& ctx);
    void play_media(LiveMediaContent media, const StackBuildContext& ctx);
    void apply_live_from_service_item(const model::ServiceItem& item,
                                      const StackBuildContext& ctx);
    /// Re-read the content of the rundown item this stack is already showing
    /// (after an edit) without restarting it: the line cursor, image index and
    /// video or audio playback carry over. A row made another kind of item
    /// loads anew instead, as it would be sent: true then. No-op unless
    /// ``service_item_id()`` matches.
    bool refresh_from_service_item(const model::ServiceItem& item,
                                   const StackBuildContext& ctx);
    /// Rundown item this stack is showing, or nullopt for library / ad-hoc content.
    [[nodiscard]] std::optional<std::int64_t> service_item_id() const noexcept
    {
        return from_service_ ? service_item_id_ : std::nullopt;
    }
    void clear(const StackBuildContext& ctx);
    /// Build the current content from scratch; the line cursor starts at 0.
    void rebuild(const StackBuildContext& ctx);
    /// Build the current content again after outputs, themes or settings
    /// changed, keeping the line cursor (clamped to the new line count).
    void refresh(const StackBuildContext& ctx);

    [[nodiscard]] bool image_mode_active() const noexcept;
    /// A video is live: there is a picture to preview.
    [[nodiscard]] bool video_mode_active() const noexcept;
    /// Audio is live: sound only.
    [[nodiscard]] bool audio_mode_active() const noexcept;
    /// Video or audio is live, so the transport below drives it.
    [[nodiscard]] bool has_transport() const noexcept;
    /// The live file is playing. Asks the player when it has the file, so a
    /// clip that reached its end reads as stopped.
    [[nodiscard]] bool is_playing() const noexcept;
    /// Where the live file stands, and its length, in seconds: the player's
    /// when it has the file, else what this runtime was last told or read
    /// (a Preview whose player still plays out after a take). 0 when unknown.
    [[nodiscard]] double media_position_sec() const noexcept;
    [[nodiscard]] double media_duration_sec() const noexcept;
    /// A web page is live: its frames come from the app's renderer.
    [[nodiscard]] bool web_mode_active() const noexcept;
    /// What this runtime's web page is drawn by (``scene::web_tab_key``, its
    /// ``SceneWebMedia::source``): new with each page that arrives, unique
    /// across every runtime, so two stacks or two sides showing one page are
    /// two tabs. A page sent again in place of the same page keeps it (it is
    /// not loaded again), a refresh keeps it, and a swap moves it with the
    /// runtime. 0 with no page, or a page with no address it can show.
    [[nodiscard]] std::uint64_t web_tab() const noexcept { return web_tab_; }
    /// A live source (a camera) is live: its frames come from its receiver.
    [[nodiscard]] bool live_source_active() const noexcept;
    [[nodiscard]] bool bible_mode_active() const noexcept;
    [[nodiscard]] bool freeform_mode_active() const noexcept;

    [[nodiscard]] bool set_image_index(int index);
    [[nodiscard]] bool step_image(int delta);
    [[nodiscard]] bool next_slide();
    [[nodiscard]] bool prev_slide();
    [[nodiscard]] bool jump_to_line(int line_index);
    /// ``line_index`` at build step ``step``, shown as built.
    [[nodiscard]] bool jump_to_step(int line_index, int step);

    /// Whether its video or audio is meant to be playing, told to no player:
    /// the session's call as content lands in a Preview or changes side. The
    /// app's next sync plays or holds the player to match. Nothing without
    /// video or audio.
    void set_media_playing(bool playing);

    // --- Transport for the side's video or audio. False when there is none. ---
    [[nodiscard]] bool media_play();
    [[nodiscard]] bool media_pause();
    [[nodiscard]] bool media_seek(double position_sec);
    [[nodiscard]] bool media_restart();
    /// Read the player's position and state back into ``playback()``, and a
    /// video's into the scene extras the outputs paint from.
    void sync_playback();

    void sync_copyright_base(const StackBuildContext& ctx);
    void sync_bible_chrome();

    [[nodiscard]] theme::ThemeSelection theme_selection(const StackBuildContext& ctx) const;
    /// What this stack's content looks like on every output at the current
    /// line, or only on the output with id ``only_output_id`` when given.
    /// Empty before the first build.
    [[nodiscard]] std::vector<scene::SceneFrame> scene_frames(
        const StackBuildContext& ctx,
        std::string_view only_output_id = {}) const;
    /// The video objects its slides show now, on every output (4.7's 6), the
    /// same clip shown twice listed once. Empty before the first build.
    [[nodiscard]] std::vector<scene::SceneObjectVideo> object_videos() const;
    /// The same for one output at ``slide_index`` rather than the current line,
    /// fully built (a slide's picture), whatever step the live one is on.
    [[nodiscard]] std::optional<scene::SceneFrame> scene_frame_at(
        const StackBuildContext& ctx,
        std::string_view output_id,
        int slide_index) const;

private:
    [[nodiscard]] build::SlideBuildInput make_build_input(const StackBuildContext& ctx) const;
    [[nodiscard]] scene::SceneMode scene_mode_for_frames() const noexcept;
    [[nodiscard]] std::string_view transition_key_for_frames() const noexcept;
    /// The page content replaced showed, and its tab: what a page arriving
    /// keeps its tab by. Empty when what was replaced was not a page.
    struct ReplacedPage {
        std::string page;  ///< ``scene::web_source_key``
        std::uint64_t tab = 0;
    };
    /// Video, audio, images and a web page load from ``media_`` and become the live
    /// kind. Any other loaded media leaves ``kind_`` as the caller set it.
    /// ``replaced`` is what ``reset_live_blobs`` let go.
    void engage_loaded_media(const ReplacedPage& replaced);
    [[nodiscard]] scene::SceneExtras frame_extras() const;
    void load_image_playlist_from_media();
    void clear_image_playlist();
    void sync_image_scene_extras();
    void load_playback_from_media();
    void clear_playback();
    void sync_video_scene_extras();
    void load_web_from_media(const ReplacedPage& replaced);
    void load_live_from_media();
    void clear_bible_chrome();
    void clear_copyright_base();
    /// Let go of the content shown. Returns the page it showed, if any.
    ReplacedPage reset_live_blobs();
    /// Copy a rundown item's content into the live blobs (no build).
    void load_service_item_content(const model::ServiceItem& item);
    void build(const StackBuildContext& ctx, bool keep_position);

    model::ContentKind kind_ = model::ContentKind::Empty;
    bool from_service_ = false;
    std::optional<std::int64_t> service_item_id_;

    /// The song live on this stack: a library song (``id`` set) or a rundown
    /// row's snapshot of one.
    model::Song song_;
    model::BiblePassage bible_;
    LiveFreeformContent freeform_;
    LiveMediaContent media_;
    ImagePlaylist image_playlist_;
    MediaPlayback playback_;
    media::IMediaPlayer* media_player_ = nullptr;
    std::uint64_t web_tab_ = 0;

    PlayerController player_;
    build::SlideBuildResult last_build_;
    bool has_build_ = false;
    std::uint64_t build_id_ = 0;
    /// Bumped by each build of new content (not a refresh of what is live).
    std::uint64_t content_run_ = 0;
    scene::SceneExtras scene_extras_;
};

/// A stack's two sides: what it shows live, and what is cued beside it.
enum class StackSide {
    Program,
    Preview,
};

/// One side of one stack, by the stack's id.
struct Slot {
    std::string stack_id;
    StackSide side = StackSide::Program;
};

/**
 * One stack: what it shows live (``program``) and, when Preview is on for it,
 * what is cued beside it (``preview``). Both sides carry the stack's id; a
 * runtime does not know which side it is on.
 *
 * Code that walks the stacks says which side it means: ``program`` for what
 * the outputs show, plays and sounds, and both (``Session::for_every_side``)
 * for what follows content wherever it is (a rebuild, an edit, a clear).
 */
struct Stack {
    StackRuntime program;
    std::optional<StackRuntime> preview;

    [[nodiscard]] const std::string& id() const noexcept { return program.id; }
    /// Something is live on it, or cued: a stack the operator is working.
    [[nodiscard]] bool holds_content() const noexcept
    {
        return program.occupied() || (preview && preview->occupied());
    }
    /// ``side``'s runtime; null for a Preview the stack does not have.
    [[nodiscard]] const StackRuntime* side(StackSide side) const noexcept
    {
        if (side == StackSide::Program) {
            return &program;
        }
        return preview ? &*preview : nullptr;
    }
    [[nodiscard]] StackRuntime* side(StackSide side) noexcept
    {
        return const_cast<StackRuntime*>(std::as_const(*this).side(side));
    }
};

/**
 * The content frame of every output in ``built`` (or only the output with id
 * ``only_output_id``, when given) at the player's current slide. Blank and
 * freeze are applied by Session over these.
 * Canvas comes from matching ``outputs`` by id (fallback: the reference canvas).
 * ``mode_override`` replaces Empty when media is live.
 */
[[nodiscard]] std::vector<scene::SceneFrame> build_scene_frames(
    const build::SlideBuildResult& built,
    const std::vector<model::OutputConfig>& outputs,
    const PlayerController& player,
    const scene::SceneExtras& extras = {},
    std::optional<scene::SceneMode> mode_override = std::nullopt,
    layout::ITextMeasurer* measurer = nullptr,
    std::string_view only_output_id = {});

} // namespace seventhslide::session
