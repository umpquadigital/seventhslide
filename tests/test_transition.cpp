#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/transition.hpp>

#include <nlohmann/json.hpp>

using seventhslide::model::Transition;
using seventhslide::model::TransitionType;
using seventhslide::model::optional_transition_from_json;
using seventhslide::model::to_string;
using seventhslide::model::transition_from_fade;
using seventhslide::model::transition_type_from_string;

TEST_CASE("Transition from a stored fade", "[model][transition]")
{
    // A background theme stores ``bg_enable_fade`` / ``bg_fade_duration``.
    const auto off = transition_from_fade(false, 500);
    REQUIRE(off.type == TransitionType::Cut);
    REQUIRE_FALSE(off.plays());
    REQUIRE(off.duration_ms == 500);

    const auto on = transition_from_fade(true, 250);
    REQUIRE(on.type == TransitionType::Fade);
    REQUIRE(on.duration_ms == 250);

    // Zero duration is a cut even when enabled; a negative one is clamped.
    REQUIRE_FALSE(transition_from_fade(true, 0).plays());
    REQUIRE(transition_from_fade(true, -5).duration_ms == 0);
}

TEST_CASE("Transition json + inherit", "[model][transition]")
{
    REQUIRE(to_string(TransitionType::Fade) == "fade");
    REQUIRE(to_string(TransitionType::FadeThroughBlack) == "fade_through_black");
    REQUIRE(transition_type_from_string("fade") == TransitionType::Fade);
    REQUIRE(transition_type_from_string("fade_through_black") == TransitionType::FadeThroughBlack);
    REQUIRE(transition_type_from_string("sparkle") == TransitionType::Cut);
    REQUIRE(transition_type_from_string("cut") == TransitionType::Cut);

    Transition spec{TransitionType::Fade, 300};
    nlohmann::json j = spec;
    REQUIRE(j["type"] == "fade");
    REQUIRE(j["duration_ms"] == 300);
    // The default easing is not written, so what was stored stays as it was.
    REQUIRE_FALSE(j.contains("easing"));

    const auto back = j.get<Transition>();
    REQUIRE(back == spec);

    REQUIRE_FALSE(optional_transition_from_json(nlohmann::json::object()));
    const auto parsed = optional_transition_from_json({{"type", "fade"}, {"duration_ms", 120}});
    REQUIRE(parsed);
    REQUIRE(parsed->plays());
    REQUIRE(parsed->duration_ms == 120);
}

TEST_CASE("A transition's easing and blend", "[model][transition]")
{
    using seventhslide::model::TransitionEasing;
    using seventhslide::model::blend_at;
    using seventhslide::model::eased;

    Transition linear{TransitionType::FadeThroughBlack, 400, TransitionEasing::Linear};
    nlohmann::json j = linear;
    REQUIRE(j["easing"] == "linear");
    REQUIRE(j.get<Transition>() == linear);

    REQUIRE(eased(TransitionEasing::Linear, 0.25) == 0.25);
    REQUIRE(eased(TransitionEasing::EaseInOut, 0.25) == 0.125);
    REQUIRE(eased(TransitionEasing::EaseInOut, 0.5) == 0.5);
    REQUIRE(eased(TransitionEasing::EaseInOut, 1.5) == 1.0);

    // A fade crosses the two; through black, one is gone before the other comes.
    REQUIRE(blend_at(TransitionType::Fade, 0.25).outgoing == 0.75);
    REQUIRE(blend_at(TransitionType::Fade, 0.25).incoming == 0.25);
    REQUIRE(blend_at(TransitionType::FadeThroughBlack, 0.25).outgoing == 0.5);
    REQUIRE(blend_at(TransitionType::FadeThroughBlack, 0.25).incoming == 0.0);
    REQUIRE(blend_at(TransitionType::FadeThroughBlack, 0.5).outgoing == 0.0);
    REQUIRE(blend_at(TransitionType::FadeThroughBlack, 0.5).incoming == 0.0);
    REQUIRE(blend_at(TransitionType::FadeThroughBlack, 0.75).incoming == 0.5);
}

TEST_CASE("A moving transition has a direction", "[model][transition]")
{
    using seventhslide::model::TransitionDirection;
    REQUIRE(transition_type_from_string("push") == TransitionType::Push);
    REQUIRE(transition_type_from_string("reveal") == TransitionType::Reveal);
    REQUIRE(seventhslide::model::transition_moves(TransitionType::Split));
    REQUIRE_FALSE(seventhslide::model::transition_moves(TransitionType::FadeThroughBlack));
    Transition push{TransitionType::Push, 600};
    push.direction = TransitionDirection::Up;
    nlohmann::json j = push;
    REQUIRE(j["direction"] == "up");
    REQUIRE(j.get<Transition>() == push);
    // The default side is not written.
    push.direction = TransitionDirection::Right;
    j = push;
    REQUIRE_FALSE(j.contains("direction"));
    // Both pictures stay whole; the motion is where each is.
    REQUIRE(seventhslide::model::blend_at(TransitionType::Push, 0.3).outgoing == 1.0);
    REQUIRE(seventhslide::model::blend_at(TransitionType::Zoom, 0.3).incoming == 0.3);
}
