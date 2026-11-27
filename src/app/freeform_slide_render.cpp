#include "freeform_slide_render.hpp"

#include <seventhslide/scene/scene_builder.hpp>

#include <limits>

namespace seventhslide::app {

build::SlideBuildResult build_freeform_slides(const freeform::Document& doc,
                                              const std::vector<model::OutputConfig>& outputs,
                                              const std::vector<std::string>& theme_priority,
                                              PreviewResources& previews)
{
    build::SlideBuildInput in;
    in.kind = build::BuildKind::Freeform;
    in.selection.kind = model::ContentKind::Freeform;
    in.selection.theme_priority = theme_priority;
    in.freeform.document = doc;
    return build::build_slides(in, outputs, previews.measurer());
}

render::StillPaint freeform_slide_still(const build::OutputRuntime& rt,
                                        geom::CanvasSize canvas,
                                        int slide_index,
                                        QSize size,
                                        PreviewResources& previews)
{
    render::StillPaint still;
    still.frame = scene::share_frame(scene::build_scene_frame({
        .runtime = rt,
        .mode = scene::SceneMode::Freeform,
        .canvas_width = canvas.width,
        .canvas_height = canvas.height,
        .slide_index = slide_index,
        .measurer = &previews.measurer(),
        // A slide's picture is the slide fully built.
        .step = std::numeric_limits<int>::max(),
    }));
    still.size = size;
    still.cache = previews.image_cache();
    still.faces = &previews.measurer().faces();
    return still;
}

}  // namespace seventhslide::app
