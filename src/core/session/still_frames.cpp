#include "seventhslide/session/still_frames.hpp"

#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/scene/freeform_convert.hpp"

#include <utility>

namespace seventhslide::session {

namespace {

template <class... Fn>
struct Overloaded : Fn... {
    using Fn::operator()...;
};

void load(StackRuntime& stack, const StillSource& source, const StackBuildContext& ctx)
{
    std::visit(Overloaded{
                   [&](const model::ServiceItem& item) { stack.apply_live_from_service_item(item, ctx); },
                   [&](const model::Song& song) { stack.play_song(song, ctx); },
                   [&](const model::BiblePassage& passage) { stack.play_bible(passage, ctx); },
                   [&](const LiveFreeformContent& freeform) { stack.play_freeform(freeform, ctx); },
               },
               source);
}

/// How many stills ``stack`` shows on ``output_id``: its slides, or its
/// images. 0 for content with no still picture.
[[nodiscard]] int still_count(const StackRuntime& stack, std::string_view output_id)
{
    switch (scene::scene_mode_for(stack.content_kind())) {
    case scene::SceneMode::Song:
    case scene::SceneMode::Bible:
    case scene::SceneMode::Freeform:
        for (const auto& rt : stack.last_build().outputs) {
            if (rt.output_id == output_id) {
                return rt.slide_count();
            }
        }
        return 0;
    case scene::SceneMode::Image:
        return stack.image_playlist().count();
    case scene::SceneMode::Empty:
    case scene::SceneMode::Video:
    case scene::SceneMode::Audio:
    case scene::SceneMode::Web:
    case scene::SceneMode::Live:
        return 0;
    }
    return 0;
}

}  // namespace

std::vector<OutputStills> still_frames(const StillSource& source, const StackBuildContext& ctx)
{
    StackRuntime stack;
    load(stack, source, ctx);

    const bool images = stack.image_mode_active();
    std::vector<OutputStills> out;
    out.reserve(ctx.outputs.size());
    for (const auto& oc : ctx.outputs) {
        OutputStills stills{oc.id, {}};
        const int count = stack.has_build() ? still_count(stack, oc.id) : 0;
        stills.frames.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            // An image playlist shows its current image whatever slide is asked for.
            if (images && !stack.set_image_index(i)) {
                break;
            }
            if (auto frame = stack.scene_frame_at(ctx, oc.id, images ? 0 : i)) {
                stills.frames.push_back(std::move(*frame));
            }
        }
        out.push_back(std::move(stills));
    }
    return out;
}

std::optional<scene::SceneFrame> first_still_frame(const StillSource& source,
                                                   const StackBuildContext& ctx,
                                                   std::string_view output_id)
{
    StackRuntime stack;
    load(stack, source, ctx);
    if (!stack.has_build() || still_count(stack, output_id) == 0) {
        return std::nullopt;
    }
    if (stack.image_mode_active() && !stack.set_image_index(0)) {
        return std::nullopt;
    }
    return stack.scene_frame_at(ctx, output_id, 0);
}

std::optional<freeform::Document> freeform_from_service_item(const model::ServiceItem& item,
                                                             const StackBuildContext& ctx)
{
    switch (item.kind()) {
    case model::ContentKind::Freeform: {
        const auto it = item.data.find("document");
        if (it == item.data.end()) {
            return std::nullopt;
        }
        // Named as the row is (``freeform_item_data``'s name), as it plays.
        const auto named = item.data.find("name");
        const auto name = named != item.data.end() && named->is_string() ? named->get<std::string>() : item.title();
        auto doc = freeform::load_document(*it, name.empty() ? "Untitled" : name);
        if (doc && doc->name.empty()) {
            doc->name = name.empty() ? "Untitled" : name;
        }
        return doc;
    }
    case model::ContentKind::Song:
    case model::ContentKind::Bible:
        break;
    default:
        return std::nullopt;
    }
    StackRuntime stack;
    stack.apply_live_from_service_item(item, ctx);
    if (!stack.has_build()) {
        return std::nullopt;
    }
    const auto& extras = stack.scene_extras();
    scene::FreeformConvertOptions opts;
    opts.name = item.title().empty() ? "Untitled" : item.title();
    opts.copyright_vars = extras.copyright_vars;
    opts.song_copyright = extras.song_copyright;
    opts.bible_ref_vars = extras.bible_ref_vars;
    return scene::document_from_build(stack.last_build(), ctx.outputs, ctx.measurer, opts);
}

}  // namespace seventhslide::session
