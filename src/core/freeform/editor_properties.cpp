// Editor: the selected object's properties (by the ribbon's names, through
// object_properties) and text, and the slide's background and transitions.

#include "seventhslide/freeform/editor.hpp"

#include "seventhslide/freeform/object_properties.hpp"
#include "seventhslide/layout/background_fill.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::freeform {
EditResult Editor::set_text(std::string text)
{
    const auto* obj = selected_item();
    if (obj && obj->cue_linked) {
        return EditResult::fail("linked to cues");
    }
    return mutate_selected([&](Object& o) {
        o.text = std::move(text);
        return EditResult::success();
    });
}

EditResult Editor::set_slide_notes(std::string notes)
{
    const auto* slide = current_slide();
    if (slide == nullptr) {
        return EditResult::fail("unknown slide");
    }
    if (slide->notes == notes) {
        return EditResult::success();
    }
    return commit_here([&](Document& doc) {
        auto* target = find_slide(doc, output_id_, slide_index_);
        if (!target) {
            return EditResult::fail("unknown slide");
        }
        target->notes = std::move(notes);
        return EditResult::success();
    });
}

EditResult Editor::set_slide_background(SlideBackground background)
{
    if (!current_slide()) {
        return EditResult::fail("unknown slide");
    }
    switch (background.kind) {
    case SlideBackgroundKind::None:
        background = {};
        break;
    case SlideBackgroundKind::Color:
        background.image.clear();
        background.video.clear();
        if (background.color.empty()) {
            background.color = layout::k_default_fill_color;
        }
        break;
    case SlideBackgroundKind::Image:
        background.color.clear();
        background.video.clear();
        if (background.image.empty()) {
            return EditResult::fail("image filename required");
        }
        break;
    case SlideBackgroundKind::Video:
        background.color.clear();
        background.image.clear();
        if (background.video.empty()) {
            return EditResult::fail("video filename required");
        }
        break;
    }
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        slide->background = std::move(background);
        return EditResult::success();
    });
}

EditResult Editor::set_background_of(std::string_view slide_id,
                                     SlideBackground background,
                                     std::optional<model::Transition> transition)
{
    return commit([&](Document& doc) {
        for (auto& deck : doc.decks) {
            for (auto& slide : deck.slides) {
                if (slide.id == slide_id) {
                    slide.background = std::move(background);
                    slide.background_transition = std::move(transition);
                    return EditResult::success();
                }
            }
        }
        return EditResult::fail("unknown slide");
    });
}

namespace {

/// The slide's animations in its order, as pointers into it.
[[nodiscard]] std::vector<std::pair<Object*, canvas::Animation*>> animations_in_order(Slide& slide)
{
    std::vector<std::pair<Object*, canvas::Animation*>> all;
    for (auto& obj : slide.objects) {
        for (auto& animation : obj.animations) {
            all.emplace_back(&obj, &animation);
        }
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const auto& a, const auto& b) { return a.second->order < b.second->order; });
    return all;
}

/// 1, 2, 3 ... in the order they are in.
void renumber(std::vector<std::pair<Object*, canvas::Animation*>>& all)
{
    int order = 0;
    for (auto& [obj, animation] : all) {
        animation->order = ++order;
    }
}

}  // namespace

std::vector<Editor::SlideAnimation> Editor::slide_animations() const
{
    std::vector<SlideAnimation> out;
    const auto* slide = current_slide();
    if (slide == nullptr) {
        return out;
    }
    for (const auto& obj : slide->objects) {
        for (const auto& animation : obj.animations) {
            out.push_back({obj.id, animation});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.animation.order < b.animation.order;
    });
    return out;
}

EditResult Editor::add_animation(canvas::AnimationEffect effect)
{
    const auto ids = selected_ids();
    if (ids.empty()) {
        return EditResult::fail("nothing selected");
    }
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        auto all = animations_in_order(*slide);
        renumber(all);
        int order = static_cast<int>(all.size());
        bool first = true;
        for (const auto& id : ids) {
            for (auto& obj : slide->objects) {
                if (obj.id != id) {
                    continue;
                }
                canvas::Animation animation;
                animation.order = ++order;
                animation.effect = effect;
                animation.trigger = first ? canvas::AnimationTrigger::OnClick
                                          : canvas::AnimationTrigger::WithPrevious;
                first = false;
                obj.animations.push_back(animation);
            }
        }
        return first ? EditResult::fail("nothing selected") : EditResult::success();
    });
}

EditResult Editor::set_animation(std::string_view object_id, int order, canvas::Animation value)
{
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        for (auto& [obj, animation] : animations_in_order(*slide)) {
            if (obj->id == object_id && animation->order == order) {
                value.order = order;
                value.duration_ms = std::clamp(value.duration_ms, 0, 60'000);
                value.delay_ms = std::clamp(value.delay_ms, 0, 60'000);
                if (!canvas::animation_has_direction(value.effect)) {
                    value.direction = model::TransitionDirection::Left;
                }
                *animation = value;
                return EditResult::success();
            }
        }
        return EditResult::fail("no such animation");
    });
}

EditResult Editor::remove_animation(std::string_view object_id, int order)
{
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        for (auto& obj : slide->objects) {
            if (obj.id != object_id) {
                continue;
            }
            const auto before = obj.animations.size();
            std::erase_if(obj.animations, [order](const canvas::Animation& a) { return a.order == order; });
            if (obj.animations.size() != before) {
                auto all = animations_in_order(*slide);
                renumber(all);
                return EditResult::success();
            }
        }
        return EditResult::fail("no such animation");
    });
}

EditResult Editor::move_animation(int from, int to)
{
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        auto all = animations_in_order(*slide);
        const int n = static_cast<int>(all.size());
        if (from < 0 || from >= n || to < 0 || to >= n) {
            return EditResult::fail("no such animation");
        }
        auto moved = all[static_cast<std::size_t>(from)];
        all.erase(all.begin() + from);
        all.insert(all.begin() + to, moved);
        renumber(all);
        return EditResult::success();
    });
}

EditResult Editor::set_slide_transition(std::optional<model::Transition> spec, bool all_slides)
{
    if (all_slides) {
        return commit([&](Document& doc) {
            auto* deck = find_deck(doc, output_id_);
            if (!deck) {
                return EditResult::fail("unknown deck");
            }
            for (auto& slide : deck->slides) {
                slide.transition = spec;
            }
            return EditResult::success();
        });
    }
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        slide->transition = std::move(spec);
        return EditResult::success();
    });
}

EditResult Editor::set_slide_background_transition(std::optional<model::Transition> spec)
{
    return commit_here([&](Document& doc) {
        auto* slide = find_slide(doc, output_id_, slide_index_);
        if (!slide) {
            return EditResult::fail("unknown slide");
        }
        slide->background_transition = std::move(spec);
        return EditResult::success();
    });
}

EditResult Editor::set_object_transition(model::Transition spec)
{
    return mutate_selected([&](Object& obj) {
        obj.transition = spec;
        return EditResult::success();
    });
}

} // namespace seventhslide::freeform
