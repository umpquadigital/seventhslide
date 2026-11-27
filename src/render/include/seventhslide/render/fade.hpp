#pragma once

// Content and background transitions of one stack's picture.
// Content changes by the incoming frame's transition whatever changed: a slide,
// or the item, content kind and all, or a blank coming or going.
// Backgrounds change only when the fill payload does.
// Seam: plain C++20 (no Qt).

#include "seventhslide/model/transition.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::render {

struct FadeConfig {
    bool enable = false;
    int duration_ms = 500;

    [[nodiscard]] bool operator==(const FadeConfig&) const noexcept = default;
};

[[nodiscard]] FadeConfig fade_config_from_spec(const model::Transition& spec) noexcept;
/// How ``frame`` comes in: its transition, and as long as its builds take
/// when they outlast it. Builds alone still make it a change.
[[nodiscard]] FadeConfig fade_config_of(const scene::SceneFrame& frame) noexcept;

/// ``transition_key`` if non-empty, else ``to_string(mode)``: a change of key is
/// what makes a transition play.
[[nodiscard]] std::string effective_transition_key(const scene::SceneFrame& frame);

/**
 * True when the next present should transition rather than cut.
 *
 * False on first paint (empty ``prev_key``), a cut, or a non-positive
 * duration. A change of key (another item, another content kind) plays the
 * incoming transition like any other change: nothing snaps for it.
 */
[[nodiscard]] bool should_crossfade(const FadeConfig& config,
                                    std::string_view prev_key) noexcept;

/// True when the background fill payload changed and its transition plays.
[[nodiscard]] bool should_crossfade_background(const model::Transition& spec,
                                               std::string_view prev_payload,
                                               std::string_view next_payload) noexcept;

/// Everything ``bg`` paints, as one string: two backgrounds with the same key
/// paint the same, so moving between them is not a background change.
[[nodiscard]] std::string background_payload_key(const scene::SceneBackground& bg);

enum class FadePresentAction {
    Noop,       // identical to current settled frame
    Snap,       // show next immediately
    StartFade,  // crossfade outgoing -> next
};

/**
 * Double-buffer state for one stack's content fade on an output. A frame
 * handed in may carry the stacks below it (the top stack's is the output's
 * whole frame); they are layers of their own, and do not count here.
 *
 * ``progress`` 0 = fully outgoing, 1 = fully current.
 */
class FadePresenter {
public:
    [[nodiscard]] FadePresentAction present(scene::SharedSceneFrame next);

    void set_progress(double t) noexcept;
    /// ``t`` without finishing at 1: the transition is in, its builds play on.
    void hold_progress(double t) noexcept;
    void finish() noexcept;

    [[nodiscard]] bool is_fading() const noexcept { return fading_; }
    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] const scene::SharedSceneFrame& current() const noexcept { return current_; }
    [[nodiscard]] const scene::SharedSceneFrame& outgoing() const noexcept { return outgoing_; }
    [[nodiscard]] const std::string& last_key() const noexcept { return last_key_; }
    [[nodiscard]] FadeConfig last_config() const noexcept { return last_config_; }

private:
    scene::SharedSceneFrame current_;
    scene::SharedSceneFrame outgoing_;
    std::string last_key_;
    FadeConfig last_config_;
    bool fading_ = false;
    double progress_ = 1.0;
};

/// Independent background dual-buffer. Fades only on payload change.
class BackgroundFadePresenter {
public:
    [[nodiscard]] FadePresentAction present(scene::SceneBackground next,
                                            const model::Transition& spec);

    void set_progress(double t) noexcept;
    void finish() noexcept;

    [[nodiscard]] bool is_fading() const noexcept { return fading_; }
    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] const std::optional<scene::SceneBackground>& current() const noexcept
    {
        return current_;
    }
    [[nodiscard]] const std::optional<scene::SceneBackground>& outgoing() const noexcept
    {
        return outgoing_;
    }
    [[nodiscard]] int last_duration_ms() const noexcept { return last_spec_.duration_ms; }
    /// The transition the background is changing by.
    [[nodiscard]] const model::Transition& last_spec() const noexcept { return last_spec_; }

private:
    std::optional<scene::SceneBackground> current_;
    std::optional<scene::SceneBackground> outgoing_;
    std::string last_payload_;
    model::Transition last_spec_;
    bool fading_ = false;
    double progress_ = 1.0;
};

}  // namespace seventhslide::render
