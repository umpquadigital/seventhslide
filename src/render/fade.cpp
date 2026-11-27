#include <seventhslide/render/fade.hpp>

#include <algorithm>

namespace seventhslide::render {

FadeConfig fade_config_from_spec(const model::Transition& spec) noexcept
{
    FadeConfig cfg;
    cfg.enable = spec.plays();
    cfg.duration_ms = spec.duration_ms;
    return cfg;
}

FadeConfig fade_config_of(const scene::SceneFrame& frame) noexcept
{
    FadeConfig cfg = fade_config_from_spec(frame.content_transition);
    if (frame.builds_end_ms > 0) {
        cfg.duration_ms = std::max(cfg.enable ? cfg.duration_ms : 0, frame.builds_end_ms);
        cfg.enable = true;
    }
    return cfg;
}

std::string effective_transition_key(const scene::SceneFrame& frame)
{
    if (!frame.transition_key.empty()) {
        return frame.transition_key;
    }
    return std::string(scene::to_string(frame.mode));
}

bool should_crossfade(const FadeConfig& config,
                      std::string_view prev_key) noexcept
{
    if (!config.enable || config.duration_ms <= 0) {
        return false;
    }
    return !prev_key.empty();
}

std::string background_payload_key(const scene::SceneBackground& bg)
{
    const auto type = layout::to_string(bg.type);
    std::string key;
    key.reserve(type.size() + bg.color.size() + bg.image.size() + bg.video.size() + 3);
    key.append(type);
    key.push_back('|');
    key.append(bg.color);
    key.push_back('|');
    key.append(bg.image);
    key.push_back('|');
    key.append(bg.video);
    return key;
}

namespace {

// The two comparisons below name every field they compare, and leave out
// those that do not change what paints. Each first binds every field of its
// type by name, compared or not: a field added to the type, or one taken
// away, stops that binding compiling, until the comparison says whether it
// counts.

/// ``a`` and ``b`` show the same video, whatever its clock (position,
/// playing): the picture arrives separately, as the player makes it.
[[nodiscard]] bool same_video(const scene::SceneVideoMedia& a, const scene::SceneVideoMedia& b)
{
    [[maybe_unused]] const auto& [source, url, loop, autoplay, position_sec, area] = a;
    return a.source == b.source && a.url == b.url && a.loop == b.loop && a.area == b.area;
}

/// ``a`` and ``b`` differ at most in freeze, the video's playback clock and
/// the stacks below. The first two do not change what paints, so a playing
/// video or a freeze toggle is not new content; the stacks below are layers
/// of their own (the top stack's layer is the output's whole frame).
[[nodiscard]] bool paints_same(const scene::SceneFrame& a, const scene::SceneFrame& b)
{
    [[maybe_unused]] const auto& [output_id, output_name, canvas_width, canvas_height, mode,
                                  slide_index, stack_id, transition_key, is_blank, is_frozen,
                                  background, text, overlay, objects, image, video, web, live,
                                  media_stand_in, stacked_below, content_transition,
                                  builds_end_ms, background_transition] = a;
    const bool videos_same = a.video.has_value() == b.video.has_value()
                             && (!a.video || same_video(*a.video, *b.video));
    return a.output_id == b.output_id && a.output_name == b.output_name
           && a.canvas_width == b.canvas_width && a.canvas_height == b.canvas_height
           && a.mode == b.mode && a.slide_index == b.slide_index && a.stack_id == b.stack_id
           && a.transition_key == b.transition_key && a.is_blank == b.is_blank
           && a.background == b.background && a.text == b.text && a.overlay == b.overlay
           && a.objects == b.objects && a.image == b.image && videos_same && a.web == b.web
           && a.live == b.live && a.media_stand_in == b.media_stand_in
           && a.content_transition == b.content_transition
           && a.builds_end_ms == b.builds_end_ms
           && a.background_transition == b.background_transition;
}

}  // namespace

bool should_crossfade_background(const model::Transition& spec,
                                 std::string_view prev_payload,
                                 std::string_view next_payload) noexcept
{
    if (!spec.plays()) {
        return false;
    }
    if (prev_payload.empty()) {
        return false;
    }
    return prev_payload != next_payload;
}

FadePresentAction FadePresenter::present(scene::SharedSceneFrame next)
{
    const FadeConfig cfg = fade_config_of(*next);
    last_config_ = cfg;
    const std::string next_key = effective_transition_key(*next);

    if (current_ && paints_same(*current_, *next)) {
        // Keep the latest freeze and video clock.
        current_ = std::move(next);
        return FadePresentAction::Noop;
    }

    // A blank (the operator's) coming or going is a change like any other:
    // it plays the incoming frame's transition, which core makes the
    // slide's own (PlayerController::settle).
    if (should_crossfade(cfg, last_key_) && current_) {
        outgoing_ = std::move(current_);
        current_ = std::move(next);
        last_key_ = next_key;
        fading_ = true;
        progress_ = 0.0;
        return FadePresentAction::StartFade;
    }

    outgoing_.reset();
    current_ = std::move(next);
    last_key_ = next_key;
    fading_ = false;
    progress_ = 1.0;
    return FadePresentAction::Snap;
}

void FadePresenter::set_progress(double t) noexcept
{
    if (!fading_) {
        progress_ = 1.0;
        return;
    }
    progress_ = std::clamp(t, 0.0, 1.0);
    if (progress_ >= 1.0) {
        finish();
    }
}

void FadePresenter::hold_progress(double t) noexcept
{
    if (fading_) {
        progress_ = std::clamp(t, 0.0, 1.0);
    }
}

void FadePresenter::finish() noexcept
{
    outgoing_.reset();
    fading_ = false;
    progress_ = 1.0;
}

FadePresentAction BackgroundFadePresenter::present(scene::SceneBackground next,
                                                   const model::Transition& spec)
{
    last_spec_ = spec;
    const std::string next_payload = background_payload_key(next);

    if (current_ && last_payload_ == next_payload) {
        current_ = std::move(next);
        return FadePresentAction::Noop;
    }

    if (should_crossfade_background(spec, last_payload_, next_payload) && current_.has_value()) {
        outgoing_ = std::move(current_);
        current_ = std::move(next);
        last_payload_ = next_payload;
        fading_ = true;
        progress_ = 0.0;
        return FadePresentAction::StartFade;
    }

    outgoing_.reset();
    current_ = std::move(next);
    last_payload_ = next_payload;
    fading_ = false;
    progress_ = 1.0;
    return FadePresentAction::Snap;
}

void BackgroundFadePresenter::set_progress(double t) noexcept
{
    if (!fading_) {
        progress_ = 1.0;
        return;
    }
    progress_ = std::clamp(t, 0.0, 1.0);
    if (progress_ >= 1.0) {
        finish();
    }
}

void BackgroundFadePresenter::finish() noexcept
{
    outgoing_.reset();
    fading_ = false;
    progress_ = 1.0;
}

}  // namespace seventhslide::render
