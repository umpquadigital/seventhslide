#include "seventhslide/scene/paint_order.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>

namespace seventhslide::scene {
namespace {

/// Where a step sorts among steps of the same z: the body, title boxes and
/// media under the objects.
[[nodiscard]] int part_rank(PaintPart part) noexcept
{
    switch (part) {
    case PaintPart::Body:
        return 0;
    case PaintPart::TitleBox:
        return 1;
    case PaintPart::Media:
        return 2;
    case PaintPart::Object:
        break;
    }
    return 3;
}

/// Where a step sorts among steps of the same z and part: an outgoing body,
/// title box or media under the incoming one; an incoming object under an
/// outgoing one.
[[nodiscard]] int side_rank(const PaintStep& step) noexcept
{
    const bool outgoing = step.side == PaintSide::Outgoing;
    if (step.part == PaintPart::Object) {
        return outgoing ? 1 : 0;
    }
    return outgoing ? 0 : 1;
}

[[nodiscard]] std::optional<int> media_z(const SceneFrame& frame) noexcept
{
    if (frame.is_blank) {
        return std::nullopt;
    }
    switch (frame.mode) {
    case SceneMode::Image:
        if (frame.image && frame.image->area.enabled) {
            return frame.image->area.z;
        }
        break;
    case SceneMode::Video:
        if (frame.video && frame.video->area.enabled) {
            return frame.video->area.z;
        }
        break;
    case SceneMode::Web:
        if (frame.web && frame.web->area.enabled) {
            return frame.web->area.z;
        }
        break;
    case SceneMode::Live:
        if (frame.live && frame.live->area.enabled) {
            return frame.live->area.z;
        }
        break;
    case SceneMode::Empty:
    case SceneMode::Song:
    case SceneMode::Bible:
    case SceneMode::Freeform:
    case SceneMode::Audio:
        break;
    }
    return std::nullopt;
}

/// ``frame``'s body, title boxes and media as ``side``'s steps; its media
/// only with ``with_media``.
void append_slots(std::vector<PaintStep>& out,
                  const SceneFrame& frame,
                  PaintSide side,
                  bool with_media)
{
    if (frame.is_blank) {
        return;
    }
    if (frame.text && !frame.text->empty()) {
        out.push_back({.part = PaintPart::Body, .side = side, .z = frame.text->z});
    }
    for (std::size_t i = 0; i < frame.overlay.boxes.size(); ++i) {
        out.push_back({.part = PaintPart::TitleBox,
                       .side = side,
                       .z = frame.overlay.boxes[i].z,
                       .index = i});
    }
    if (with_media) {
        if (const auto z = media_z(frame)) {
            out.push_back({.part = PaintPart::Media, .side = side, .z = *z});
        }
    }
}

void append_objects(std::vector<PaintStep>& out, const SceneFrame& frame, PaintSide side)
{
    if (frame.is_blank) {
        return;
    }
    for (std::size_t i = 0; i < frame.objects.size(); ++i) {
        out.push_back({.part = PaintPart::Object,
                       .side = side,
                       .z = frame.objects[i].z,
                       .index = i});
    }
}

void sort_back_to_front(std::vector<PaintStep>& steps)
{
    std::stable_sort(steps.begin(), steps.end(), [](const PaintStep& a, const PaintStep& b) {
        return std::tuple(a.z, part_rank(a.part), side_rank(a), a.index)
               < std::tuple(b.z, part_rank(b.part), side_rank(b), b.index);
    });
}

}  // namespace

bool paints_media(const SceneFrame& frame) noexcept
{
    return media_z(frame).has_value();
}

std::vector<PaintStep> paint_order(const SceneFrame& frame)
{
    std::vector<PaintStep> steps;
    append_slots(steps, frame, PaintSide::Incoming, true);
    append_objects(steps, frame, PaintSide::Incoming);
    sort_back_to_front(steps);
    return steps;
}

std::vector<PaintStep> paint_order(const SceneFrame& outgoing,
                                   const SceneFrame& incoming,
                                   const ChangeOrder& how)
{
    std::vector<PaintStep> steps;
    const bool shared_media = how.same_media && paints_media(outgoing) && paints_media(incoming);
    append_slots(steps, outgoing, PaintSide::Outgoing, !shared_media);
    append_slots(steps, incoming, PaintSide::Incoming, true);
    if (shared_media) {
        // The incoming frame's media step stands for both.
        for (auto& step : steps) {
            if (step.part == PaintPart::Media) {
                step.side = PaintSide::Both;
            }
        }
    }
    if (!how.match_objects || outgoing.is_blank || incoming.is_blank) {
        append_objects(steps, outgoing, PaintSide::Outgoing);
        append_objects(steps, incoming, PaintSide::Incoming);
        sort_back_to_front(steps);
        return steps;
    }

    // An id names one object per frame; a second with the same id is its
    // frame's alone. One the outgoing frame took off by an exit is gone.
    std::map<std::string, std::size_t, std::less<>> outgoing_ids;
    for (std::size_t i = 0; i < outgoing.objects.size(); ++i) {
        const auto& obj = outgoing.objects[i];
        if (!obj.id.empty() && !obj.exits()) {
            outgoing_ids.try_emplace(obj.id, i);
        }
    }
    std::vector<bool> shared(outgoing.objects.size(), false);
    std::set<std::string, std::less<>> incoming_ids;
    for (std::size_t i = 0; i < incoming.objects.size(); ++i) {
        const auto& obj = incoming.objects[i];
        PaintStep step{.part = PaintPart::Object, .side = PaintSide::Incoming, .z = obj.z, .index = i};
        if (!obj.id.empty() && incoming_ids.insert(obj.id).second) {
            if (const auto it = outgoing_ids.find(obj.id); it != outgoing_ids.end()) {
                step.side = PaintSide::Both;
                step.outgoing_index = it->second;
                shared[it->second] = true;
            }
        }
        steps.push_back(step);
    }
    for (std::size_t i = 0; i < outgoing.objects.size(); ++i) {
        if (!shared[i]) {
            steps.push_back({.part = PaintPart::Object,
                             .side = PaintSide::Outgoing,
                             .z = outgoing.objects[i].z,
                             .index = i});
        }
    }
    sort_back_to_front(steps);
    return steps;
}

}  // namespace seventhslide::scene
