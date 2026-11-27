#include "seventhslide/session/stack_runtime.hpp"

#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/model/output_library.hpp"
#include "seventhslide/model/static_media.hpp"
#include "seventhslide/model/live_source.hpp"
#include "seventhslide/model/web_page.hpp"
#include "seventhslide/scene/chrome_text.hpp"

#include <algorithm>
#include <limits>
#include <atomic>
#include <utility>

namespace seventhslide::session {
namespace {

[[nodiscard]] std::uint64_t next_build_id() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}

[[nodiscard]] std::uint64_t next_media_instance() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}

[[nodiscard]] std::uint64_t next_web_tab() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}

/// The page ``web`` shows, as the tab it is drawn in is kept by.
[[nodiscard]] std::string page_of(const scene::SceneWebMedia& web)
{
    return scene::web_source_key(web.url, web.width, web.height, web.zoom);
}

[[nodiscard]] std::vector<std::string> filenames_from_json_array(const nlohmann::json& arr)
{
    std::vector<std::string> out;
    if (!arr.is_array()) {
        return out;
    }
    out.reserve(arr.size());
    for (const auto& v : arr) {
        if (v.is_string()) {
            const auto s = v.get<std::string>();
            if (!s.empty()) {
                out.push_back(s);
            }
        }
    }
    return out;
}

} // namespace

bool StackRuntime::occupied() const noexcept
{
    return scene_mode_for_frames() != scene::SceneMode::Empty;
}

StackRuntime::ReplacedPage StackRuntime::reset_live_blobs()
{
    song_ = {};
    bible_ = {};
    freeform_ = {};
    media_ = {};
    kind_ = model::ContentKind::Empty;
    from_service_ = false;
    service_item_id_.reset();
    clear_image_playlist();
    clear_playback();
    // The page being replaced, for a page arriving to keep its tab by.
    ReplacedPage replaced;
    if (scene_extras_.web && web_tab_ != 0) {
        replaced = {page_of(*scene_extras_.web), web_tab_};
    }
    web_tab_ = 0;
    scene_extras_.web = std::nullopt;
    scene_extras_.live = std::nullopt;
    clear_bible_chrome();
    return replaced;
}

void StackRuntime::clear_image_playlist()
{
    image_playlist_ = {};
    scene_extras_.image = std::nullopt;
}

void StackRuntime::load_image_playlist_from_media()
{
    ImagePlaylist pl;
    if (media_.kind == model::ContentKind::Image) {
        const auto filename = json::str_or(media_.data, "filename");
        pl.folder_name = filename;
        if (!filename.empty()) {
            pl.images.push_back(filename);
        }
        pl.index = 0;
    } else if (media_.kind == model::ContentKind::ImageFolder) {
        if (media_.data.contains("folder_id") && media_.data["folder_id"].is_number_integer()) {
            pl.folder_id = media_.data["folder_id"].get<std::int64_t>();
        }
        pl.folder_name = json::str_or(media_.data, "folder_name");
        pl.images = filenames_from_json_array(json::array_or_empty(media_.data, "images"));
        pl.index = 0;
    }
    image_playlist_ = std::move(pl);
    sync_image_scene_extras();
}

void StackRuntime::sync_image_scene_extras()
{
    if (!image_mode_active()) {
        scene_extras_.image = std::nullopt;
        return;
    }
    scene::SceneImageMedia media;
    media.index = image_playlist_.index;
    media.count = image_playlist_.count();
    if (!image_playlist_.empty() && image_playlist_.index >= 0
        && image_playlist_.index < image_playlist_.count()) {
        const auto& fn = image_playlist_.images[static_cast<std::size_t>(image_playlist_.index)];
        if (const auto url = model::static_media_url(model::k_images_subdir, fn)) {
            media.url = *url;
        }
    }
    if (media.url.empty() && media.count == 0) {
        scene_extras_.image = std::nullopt;
        return;
    }
    scene_extras_.image = std::move(media);
}

void StackRuntime::load_web_from_media(const ReplacedPage& replaced)
{
    const auto page = model::web_page_from_item_data(media_.data);
    scene::SceneWebMedia web;
    web.url = page.url;
    web.width = page.width;
    web.height = page.height;
    web.zoom = page.zoom;
    // A page with no address it can show still owns the canvas (black), as
    // an image whose file is gone does; it names no renderer. The same page
    // sent again in place of itself stays in its tab, as it was.
    if (!web.url.empty()) {
        web_tab_ = replaced.tab != 0 && replaced.page == page_of(web) ? replaced.tab
                                                                       : next_web_tab();
        web.source = scene::web_tab_key(web_tab_);
    }
    scene_extras_.web = std::move(web);
}

void StackRuntime::load_live_from_media()
{
    const auto source = model::live_source_from_item_data(media_.data);
    scene::SceneLiveMedia live;
    live.name = source.name;
    // A source of a kind this build cannot receive still owns the canvas
    // (black), and names no receiver.
    live.source = model::live_source_key(source);
    scene_extras_.live = std::move(live);
}

bool StackRuntime::live_source_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Live;
}

bool StackRuntime::web_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Web;
}

void StackRuntime::clear_playback()
{
    // The player was the item's, and goes with it: the next item must not
    // read the last one's clock. It is not stopped here: the app sees the
    // item gone at its next sync and lets what it played go out as each
    // output says.
    media_player_ = nullptr;
    playback_ = {};
    scene_extras_.video = std::nullopt;
}

void StackRuntime::load_playback_from_media()
{
    MediaPlayback p;
    p.filename = json::str_or(media_.data, "filename");
    p.title = json::str_or(media_.data, "title");
    if (p.title.empty()) {
        p.title = p.filename;
    }
    p.loop = json::bool_or(media_.data, "loop", false);
    p.autoplay_intent = json::bool_or(media_.data, "autoplay", true);
    p.is_playing = p.autoplay_intent && !p.filename.empty();
    p.position_sec = 0.0;
    if (!p.filename.empty()) {
        p.instance = next_media_instance();
    }
    playback_ = std::move(p);
    sync_playback();
}

void StackRuntime::sync_playback()
{
    if (!has_transport() || playback_.empty()) {
        scene_extras_.video = std::nullopt;
        return;
    }
    if (media_player_ && media_player_->has_media()) {
        playback_.position_sec = media_player_->position_sec();
        playback_.is_playing = media_player_->is_playing();
        if (const double length = media::shown_duration_sec(media_player_); length > 0.0) {
            playback_.duration_sec = length;
        }
    }
    sync_video_scene_extras();
}

void StackRuntime::sync_video_scene_extras()
{
    if (!video_mode_active() || playback_.empty()) {
        scene_extras_.video = std::nullopt;
        return;
    }
    scene::SceneVideoMedia media;
    media.source = scene::content_video_key(playback_.instance);
    if (const auto url = model::static_media_url(model::k_videos_subdir, playback_.filename)) {
        media.url = *url;
    }
    if (media.url.empty()) {
        scene_extras_.video = std::nullopt;
        return;
    }
    media.loop = playback_.loop;
    media.autoplay = playback_.is_playing;
    media.position_sec = playback_.position_sec;
    scene_extras_.video = std::move(media);
}

bool StackRuntime::video_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Video;
}

bool StackRuntime::audio_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Audio;
}

bool StackRuntime::has_transport() const noexcept
{
    return video_mode_active() || audio_mode_active();
}

bool StackRuntime::is_playing() const noexcept
{
    if (!has_transport() || playback_.empty()) {
        return false;
    }
    if (media_player_ && media_player_->has_media()) {
        return media_player_->is_playing();
    }
    return playback_.is_playing;
}

double StackRuntime::media_position_sec() const noexcept
{
    if (!has_transport() || playback_.empty()) {
        return 0.0;
    }
    if (media_player_ && media_player_->has_media()) {
        return media::shown_position_sec(media_player_);
    }
    return playback_.position_sec;
}

double StackRuntime::media_duration_sec() const noexcept
{
    if (!has_transport() || playback_.empty()) {
        return 0.0;
    }
    if (media_player_ && media_player_->has_media()) {
        return media::shown_duration_sec(media_player_);
    }
    return playback_.duration_sec;
}

void StackRuntime::set_media_playing(bool playing)
{
    if (!has_transport() || playback_.empty()) {
        return;
    }
    playback_.is_playing = playing;
    sync_video_scene_extras();
}

bool StackRuntime::media_play()
{
    if (!has_transport() || playback_.empty()) {
        return false;
    }
    playback_.is_playing = true;
    if (media_player_ && media_player_->has_media()) {
        // Played to its end, it starts over, as a player's Play does.
        if (media_player_->ended()) {
            playback_.position_sec = 0.0;
            media_player_->restart();
        } else {
            media_player_->play();
        }
    }
    sync_playback();
    return true;
}

bool StackRuntime::media_pause()
{
    if (!has_transport() || playback_.empty()) {
        return false;
    }
    playback_.is_playing = false;
    if (media_player_) {
        media_player_->pause();
    }
    sync_playback();
    return true;
}

bool StackRuntime::media_seek(double position_sec)
{
    if (!has_transport() || playback_.empty()) {
        return false;
    }
    playback_.position_sec = position_sec < 0.0 ? 0.0 : position_sec;
    if (media_player_ && media_player_->has_media()) {
        media_player_->seek(playback_.position_sec);
    }
    sync_playback();
    return true;
}

bool StackRuntime::media_restart()
{
    if (!has_transport() || playback_.empty()) {
        return false;
    }
    playback_.position_sec = 0.0;
    playback_.is_playing = true;
    if (media_player_ && media_player_->has_media()) {
        media_player_->restart();
    }
    sync_playback();
    return true;
}

void StackRuntime::play_song(model::Song song, const StackBuildContext& ctx)
{
    reset_live_blobs();
    kind_ = model::ContentKind::Song;
    song_ = std::move(song);
    if (!song_.theme_map.is_object()) {
        song_.theme_map = nlohmann::json::object();
    }
    rebuild(ctx);
}

void StackRuntime::play_bible(model::BiblePassage bible, const StackBuildContext& ctx)
{
    reset_live_blobs();
    kind_ = model::ContentKind::Bible;
    bible_ = std::move(bible);
    if (!bible_.theme_map.is_object()) {
        bible_.theme_map = nlohmann::json::object();
    }
    sync_bible_chrome();
    rebuild(ctx);
}

void StackRuntime::play_freeform(LiveFreeformContent content, const StackBuildContext& ctx)
{
    reset_live_blobs();
    kind_ = model::ContentKind::Freeform;
    freeform_ = std::move(content);
    rebuild(ctx);
}

bool StackRuntime::refresh_freeform(LiveFreeformContent content, const StackBuildContext& ctx)
{
    if (kind_ != model::ContentKind::Freeform || from_service_ || !content.library_id
        || freeform_.library_id != content.library_id) {
        return false;
    }
    freeform_ = std::move(content);
    refresh(ctx);
    return true;
}

void StackRuntime::play_media(LiveMediaContent media, const StackBuildContext& ctx)
{
    const auto replaced = reset_live_blobs();
    media_ = std::move(media);
    engage_loaded_media(replaced);
    rebuild(ctx);
}

void StackRuntime::apply_live_from_service_item(const model::ServiceItem& item,
                                                const StackBuildContext& ctx)
{
    const auto replaced = reset_live_blobs();
    from_service_ = true;
    kind_ = item.kind();
    service_item_id_ = item.item_id;
    load_service_item_content(item);
    engage_loaded_media(replaced);
    rebuild(ctx);
}

bool StackRuntime::refresh_from_service_item(const model::ServiceItem& item,
                                             const StackBuildContext& ctx)
{
    if (!item.item_id || service_item_id() != item.item_id) {
        return false;
    }
    if (item.kind() != kind_) {
        apply_live_from_service_item(item, ctx);
        return true;
    }
    // Media keeps its loaded playlist / video / audio; only the data blob (theme map,
    // stack pin, ...) is swapped, so playback is not interrupted.
    load_service_item_content(item);
    refresh(ctx);
    return false;
}

void StackRuntime::engage_loaded_media(const ReplacedPage& replaced)
{
    switch (media_.kind) {
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
        kind_ = media_.kind;
        load_playback_from_media();
        break;
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
        kind_ = media_.kind;
        load_image_playlist_from_media();
        break;
    case model::ContentKind::Web:
        kind_ = media_.kind;
        load_web_from_media(replaced);
        break;
    case model::ContentKind::Live:
        kind_ = media_.kind;
        load_live_from_media();
        break;
    case model::ContentKind::Song:
    case model::ContentKind::Bible:
    case model::ContentKind::Freeform:
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        break;
    }
}

void StackRuntime::load_service_item_content(const model::ServiceItem& item)
{
    const auto k = item.kind();
    switch (k) {
    case model::ContentKind::Song:
        song_ = item.song();
        return;
    case model::ContentKind::Bible:
        bible_ = model::BiblePassage::from_json(item.data);
        sync_bible_chrome();
        return;
    case model::ContentKind::Freeform: {
        freeform_ = {};
        freeform_.name = json::str_or(item.data, "name");
        if (freeform_.name.empty()) {
            freeform_.name = item.title();
        }
        const auto doc = item.data.is_object() ? item.data.find("document") : item.data.end();
        // An unreadable snapshot plays nothing; the rundown row is left as stored.
        auto loaded = freeform::load_document(
            doc != item.data.end() ? *doc : nlohmann::json::object(), freeform_.name);
        freeform_.document = loaded ? std::move(*loaded) : freeform::Document{};
        return;
    }
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
    case model::ContentKind::Web:
    case model::ContentKind::Live:
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        media_.kind = k;
        media_.data = item.data.is_object() ? item.data : nlohmann::json::object();
        return;
    }
}

void StackRuntime::clear(const StackBuildContext& ctx)
{
    reset_live_blobs();
    rebuild(ctx);
}

void StackRuntime::rebuild(const StackBuildContext& ctx)
{
    build(ctx, false);
}

void StackRuntime::refresh(const StackBuildContext& ctx)
{
    build(ctx, true);
}

void StackRuntime::set_scene_extras(scene::SceneExtras extras)
{
    scene_extras_ = std::move(extras);
    build_id_ = next_build_id();
}

void StackRuntime::build(const StackBuildContext& ctx, bool keep_position)
{
    if (!keep_position) {
        ++content_run_;
    }
    const int cursor = player_.line_cursor();
    const auto steps = player_.steps();
    last_build_ = build::build_slides(make_build_input(ctx), ctx.outputs, ctx.measurer);
    has_build_ = true;
    build_id_ = next_build_id();
    player_.apply_build_result(last_build_);
    if (keep_position) {
        // The same place, as built: nothing plays again for a rebuild.
        player_.restore(cursor, steps);
    }
    sync_copyright_base(ctx);
}

theme::ThemeSelection StackRuntime::theme_selection(const StackBuildContext& ctx) const
{
    theme::ThemeSelection sel;
    sel.theme_priority = ctx.theme_priority;
    sel.service_themes = ctx.service_themes;

    sel.kind = kind_;
    sel.from_service = from_service_;
    // A song's picks are its theme chips', which the build reads off the song
    // it lays out, one run of the play order at a time. A rundown item's own
    // picks are the content tier; library content has none.
    if (kind_ == model::ContentKind::Song || !from_service_) {
        return sel;
    }
    const auto allowed = theme::categories_for(kind_);
    if (kind_ == model::ContentKind::Bible) {
        sel.content_themes = theme::OutputThemeMap::from_json(bible_.theme_map, allowed);
    } else if (const auto* map = json::find(media_.data, "theme_map")) {
        sel.content_themes = theme::OutputThemeMap::from_json(*map, allowed);
    }
    return sel;
}

build::SlideBuildInput StackRuntime::make_build_input(const StackBuildContext& ctx) const
{
    build::SlideBuildInput in;
    in.selection = theme_selection(ctx);
    in.kind = build::build_kind_for(kind_);
    switch (in.kind) {
    case build::BuildKind::Song:
        in.song = song_;
        in.ccli_licence_number = ctx.ccli_licence_number;
        break;
    case build::BuildKind::Bible:
        in.bible.chapter = bible_.chapter;
        in.bible.verses = bible_.verses;
        break;
    case build::BuildKind::Freeform:
        in.freeform.document = freeform_.document;
        break;
    case build::BuildKind::Empty:
        break;
    }
    return in;
}

void StackRuntime::clear_bible_chrome()
{
    scene_extras_.bible_ref_vars.clear();
}

void StackRuntime::clear_copyright_base()
{
    scene_extras_.copyright_vars.clear();
    scene_extras_.song_copyright = false;
}

void StackRuntime::sync_copyright_base(const StackBuildContext& ctx)
{
    if (scene_mode_for_frames() != scene::SceneMode::Song) {
        clear_copyright_base();
        return;
    }
    // The song's tokens are published whatever the copyright toggle says: they
    // are the whole built-in set (``song-title``, ``key``, ``songbook``...), and a
    // title design is entitled to use them without opting into a copyright
    // notice. The toggle only decides whether the elements that declare
    // themselves part of the notice paint - see ``theme::ShowWhen``.
    scene_extras_.song_copyright = song_.show_copyright;
    scene_extras_.copyright_vars =
        scene::copyright_token_values(song_, ctx.ccli_licence_number);
}

void StackRuntime::sync_bible_chrome()
{
    if (!bible_mode_active()) {
        scene_extras_.bible_ref_vars.clear();
        return;
    }
    scene_extras_.bible_ref_vars = scene::bible_ref_token_values(bible_);
}

bool StackRuntime::bible_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Bible;
}

bool StackRuntime::freeform_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Freeform;
}

bool StackRuntime::image_mode_active() const noexcept
{
    return scene_mode_for_frames() == scene::SceneMode::Image;
}

bool StackRuntime::set_image_index(int index)
{
    if (!image_mode_active() || image_playlist_.empty()) {
        return false;
    }
    if (index < 0 || index >= image_playlist_.count()) {
        return false;
    }
    image_playlist_.index = index;
    sync_image_scene_extras();
    return true;
}

bool StackRuntime::step_image(int delta)
{
    if (!image_mode_active() || image_playlist_.empty()) {
        return false;
    }
    return set_image_index(image_playlist_.index + delta);
}

bool StackRuntime::next_slide()
{
    if (image_mode_active()) {
        return step_image(+1);
    }
    return player_.next_slide();
}

bool StackRuntime::prev_slide()
{
    if (image_mode_active()) {
        return step_image(-1);
    }
    return player_.prev_slide();
}

bool StackRuntime::jump_to_line(int line_index)
{
    if (image_mode_active()) {
        return set_image_index(line_index);
    }
    return player_.jump_to_line(line_index);
}

bool StackRuntime::jump_to_step(int line_index, int step)
{
    if (image_mode_active()) {
        return false;
    }
    return player_.jump_to_step(line_index, step);
}

scene::SceneMode StackRuntime::scene_mode_for_frames() const noexcept
{
    return scene::scene_mode_for(kind_);
}

std::string_view StackRuntime::transition_key_for_frames() const noexcept
{
    if (from_service_ && kind_ != model::ContentKind::Empty) {
        return model::to_string(kind_);
    }
    return scene::to_string(scene_mode_for_frames());
}

scene::SceneExtras StackRuntime::frame_extras() const
{
    auto extras = scene_extras_;
    if (extras.transition_key.empty()) {
        extras.transition_key = transition_key_for_frames();
    }
    return extras;
}

std::vector<scene::SceneFrame> StackRuntime::scene_frames(const StackBuildContext& ctx,
                                                          std::string_view only_output_id) const
{
    if (!has_build_) {
        return {};
    }
    return build_scene_frames(last_build_, ctx.outputs, player_, frame_extras(),
                                     scene_mode_for_frames(), &ctx.measurer, only_output_id);
}

std::optional<scene::SceneFrame> StackRuntime::scene_frame_at(const StackBuildContext& ctx,
                                                              std::string_view output_id,
                                                              int slide_index) const
{
    if (!has_build_) {
        return std::nullopt;
    }
    const auto rt = std::find_if(last_build_.outputs.begin(), last_build_.outputs.end(),
                                 [output_id](const build::OutputRuntime& r) {
                                     return r.output_id == output_id;
                                 });
    if (rt == last_build_.outputs.end()) {
        return std::nullopt;
    }
    const auto* oc = model::find_output_by_id(ctx.outputs, output_id);
    const geom::CanvasSize canvas =
        oc != nullptr ? model::output_canvas(*oc) : geom::k_reference_canvas;
    return scene::build_scene_frame({
        .runtime = *rt,
        .mode = scene_mode_for_frames(),
        .canvas_width = canvas.width,
        .canvas_height = canvas.height,
        .slide_index = slide_index,
        .extras = frame_extras(),
        .measurer = &ctx.measurer,
        // A slide's picture is the slide fully built.
        .step = std::numeric_limits<int>::max(),
    });
}

std::vector<scene::SceneObjectVideo> StackRuntime::object_videos() const
{
    std::vector<scene::SceneObjectVideo> out;
    if (!has_build_) {
        return out;
    }
    for (const auto& rt : last_build_.outputs) {
        const auto* nav = player_.find_output(rt.output_id);
        if (nav == nullptr) {
            continue;
        }
        for (auto& video : scene::object_videos_at(rt, nav->index, nav->step)) {
            const auto listed = std::find_if(out.begin(), out.end(), [&](const auto& v) {
                return v.key == video.key;
            });
            if (listed != out.end()) {
                listed->merge(video);
                continue;
            }
            video.run = (content_run_ << 32) | static_cast<std::uint32_t>(nav->index);
            out.push_back(std::move(video));
        }
    }
    return out;
}

std::vector<scene::SceneFrame> build_scene_frames(
    const build::SlideBuildResult& built,
    const std::vector<model::OutputConfig>& outputs,
    const PlayerController& player,
    const scene::SceneExtras& extras,
    std::optional<scene::SceneMode> mode_override,
    layout::ITextMeasurer* measurer,
    std::string_view only_output_id)
{
    const scene::SceneMode mode = mode_override.value_or(scene::scene_mode_from_build_kind(built.kind));
    std::vector<scene::SceneFrame> frames;
    frames.reserve(built.outputs.size());

    for (const auto& rt : built.outputs) {
        if (!only_output_id.empty() && rt.output_id != only_output_id) {
            continue;
        }
        int canvas_w = geom::k_reference_canvas.width;
        int canvas_h = geom::k_reference_canvas.height;
        if (const auto* oc = model::find_output_by_id(outputs, rt.output_id)) {
            canvas_w = oc->canvas_width;
            canvas_h = oc->canvas_height;
        }
        const auto* nav = player.find_output(rt.output_id);
        frames.push_back(scene::build_scene_frame({
            .runtime = rt,
            .mode = mode,
            .canvas_width = canvas_w,
            .canvas_height = canvas_h,
            .slide_index = nav != nullptr ? nav->index : rt.index,
            .extras = extras,
            .measurer = measurer,
            .line_index = player.line_cursor(),
            .step = nav != nullptr ? nav->step : 0,
            .animate_step = nav == nullptr || nav->animate,
            .within_slide = nav != nullptr && nav->within,
            .transition = nav != nullptr ? nav->transition : std::nullopt,
        }));
    }
    return frames;
}

} // namespace seventhslide::session
