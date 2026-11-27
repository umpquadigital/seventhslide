#pragma once

// Scene objects the painter tests place by hand.

#include <seventhslide/canvas/object.hpp>

namespace seventhslide::test {

/// A clock object laid out from the top left, as a theme's chrome is.
[[nodiscard]] inline canvas::Object scene_clock()
{
    canvas::Object clock;
    clock.type = canvas::ObjectType::Clock;
    clock.text_align = "left";
    clock.vertical_align = "top";
    return clock;
}

}  // namespace seventhslide::test
