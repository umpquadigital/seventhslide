#include <catch2/catch_test_macros.hpp>

#include <seventhslide/scene/scene_frame.hpp>

using seventhslide::scene::SceneMode;
using seventhslide::scene::to_string;

TEST_CASE("scene mode names", "[scene][scene_frame]")
{
    // Window titles and default transition keys are built from these.
    REQUIRE(to_string(SceneMode::Song) == "song");
    REQUIRE(to_string(SceneMode::Bible) == "bible");
    REQUIRE(to_string(SceneMode::Freeform) == "freeform");
    REQUIRE(to_string(SceneMode::Image) == "image");
    REQUIRE(to_string(SceneMode::Video) == "video");
    REQUIRE(to_string(SceneMode::Empty) == "empty");
}
