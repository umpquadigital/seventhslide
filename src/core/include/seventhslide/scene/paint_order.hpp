#pragma once

// The order a frame's content paints in, back to front.
//
// A frame's content is its body text, its title boxes, its objects and its
// media (a picture, video, page or live source). Each paints at its own place
// in the stack: an object at its z, and the body, a title box and the media at
// the z of the theme element they are laid out in. So a theme paints as it is
// designed, whatever is filled in at showtime.
//
// On equal z the body, the title boxes and the media paint under the objects.
// Nothing the editor saves ties: it renumbers z on every reorder and gives a
// new element the next one. The rule only settles a hand-edited file.
//
// Seam: plain C++20.

#include "seventhslide/scene/scene_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace seventhslide::scene {

/// What one step of a frame's content paints.
enum class PaintPart : std::uint8_t {
    /// ``SceneFrame::text``: the lyrics or the passage.
    Body,
    /// One of ``SceneFrame::overlay``'s boxes: a title slide's.
    TitleBox,
    /// The picture, video, page or live source, in the theme's area.
    Media,
    /// One of ``SceneFrame::objects``.
    Object,
};

/// Which frame of a change a step is from. ``Both``: one object the two
/// frames share by id, or media both show (``ChangeOrder::same_media``).
enum class PaintSide : std::uint8_t {
    Outgoing,
    Incoming,
    Both,
};

struct PaintStep {
    PaintPart part = PaintPart::Body;
    PaintSide side = PaintSide::Incoming;
    int z = 0;
    /// A title box: its index in its frame's ``overlay.boxes``. An object:
    /// its index in its frame's ``objects``, the incoming frame's for one on
    /// both sides.
    std::size_t index = 0;
    /// An object on both sides: its index in the outgoing frame's ``objects``.
    std::size_t outgoing_index = 0;

    [[nodiscard]] bool operator==(const PaintStep&) const noexcept = default;
};

/// Whether ``frame`` paints media: its mode's picture, video, page or live
/// source, in an area its theme gives it. A blank frame paints none.
[[nodiscard]] bool paints_media(const SceneFrame& frame) noexcept;

/// ``frame``'s content, back to front, every step ``PaintSide::Incoming``.
/// A blank frame has none.
[[nodiscard]] std::vector<PaintStep> paint_order(const SceneFrame& frame);

/// How the two frames of a change share their content.
struct ChangeOrder {
    /// Objects are one step where both frames have them by id: each changes
    /// by its own transition. Otherwise every object is its own frame's.
    bool match_objects = false;
    /// The two frames show the same media, which plays on through the change
    /// as one step at the incoming frame's z.
    bool same_media = false;
};

/**
 * The content of a change from ``outgoing`` to ``incoming``, back to front.
 *
 * A step on both sides takes the incoming frame's z. On equal z, after the
 * rule above: of two bodies, title boxes or media, the outgoing frame's paints
 * first; of two objects, the incoming frame's does. An object the outgoing
 * frame took off by an exit is not shared: it is gone, and one with its id
 * comes in new.
 */
[[nodiscard]] std::vector<PaintStep> paint_order(const SceneFrame& outgoing,
                                                 const SceneFrame& incoming,
                                                 const ChangeOrder& how);

}  // namespace seventhslide::scene
