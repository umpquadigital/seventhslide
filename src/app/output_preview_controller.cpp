// The operator's previews of the outputs (see the header).

#include "output_preview_controller.hpp"

#include "open_library.hpp"
#include "playback_controller.hpp"
#include "requested_size.hpp"
#include "side_view.hpp"
#include "live_sources.hpp"
#include "web_sources.hpp"

#include <seventhslide/model/output_library.hpp>

#include <QUrl>

#include <algorithm>
#include <optional>
#include <utility>

namespace seventhslide::app {

OutputPreviewController::OutputPreviewController(const session::Session& session,
                                                 const PlaybackController& playback,
                                                 const OpenLibrary& library,
                                                 PreviewResources& previews,
                                                 QObject* parent)
    : QObject(parent)
    , session_(session)
    , playback_(playback)
    , library_(library)
    , previews_(previews)
{
}

void OutputPreviewController::refresh_slide_aspect()
{
    const auto* primary = session_.primary_output();
    const qreal aspect = primary != nullptr && primary->canvas_width > 0 && primary->canvas_height > 0
                             ? static_cast<qreal>(primary->canvas_width) / primary->canvas_height
                             : 16.0 / 9.0;
    if (!qFuzzyCompare(aspect, slide_aspect_)) {
        slide_aspect_ = aspect;
        emit slideAspectChanged();
    }
}

QSize OutputPreviewController::preview_size(const QString& output_id, int width) const
{
    const auto* oc = model::find_output_by_id(session_.outputs(), output_id.toStdString());
    if (oc == nullptr || oc->canvas_width <= 0 || oc->canvas_height <= 0) {
        return {width, width * 9 / 16};
    }
    return {width, std::max(1, width * oc->canvas_height / oc->canvas_width)};
}

QString OutputPreviewController::slideThumbUrl(SideView* view,
                                              int slide_index,
                                              int /*revision*/) const
{
    const auto* primary = session_.primary_output();
    if (view == nullptr || slide_index < 0 || primary == nullptr) {
        return {};
    }
    const bool preview = view->side() == session::StackSide::Preview;
    const QString id = QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(primary->id)));
    // Keyed by the build rather than the per-action preview revision: a built
    // slide renders the same until the build changes, so navigation, blank and
    // freeze keep serving the cached thumbnail. A slide drawn before one of
    // its pictures arrived is drawn again once it has.
    // In one pass: a second arg() would take a "%2" the encoded id holds for
    // a place marker.
    // A build is one side's, unique across every runtime: the side is in the
    // address for the provider to draw from, not for the cache.
    return QStringLiteral("image://outputpreview/%1?slide=%2%3&r=%4&p=%5")
        .arg(id, QString::number(slide_index),
             preview ? QStringLiteral("&side=preview") : QString(),
             QString::number(view->build_id()),
             QString::number(previews_.pictures_revision()));
}

QImage OutputPreviewController::render_output_preview(const QString& output_id,
                                                      const QSize& size,
                                                      int slide_index,
                                                      session::StackSide side) const
{
    return finish_preview(prepare_output_preview(output_id, size, slide_index, side));
}

PreviewJob OutputPreviewController::prepare_output_preview(const QString& output_id,
                                                           const QSize& size,
                                                           int slide_index,
                                                           session::StackSide side) const
{
    const auto* oc = model::find_output_by_id(session_.outputs(), output_id.toStdString());
    if (oc == nullptr) {
        return {};
    }
    // A side's slide is nothing until that side has built; what the output
    // shows, nothing until anything live has.
    std::optional<scene::SceneFrame> frame;
    if (slide_index >= 0) {
        frame = session_.scene_frame_at({session_.focused_stack_id(), side}, oc->id, slide_index);
    } else if (session_.has_build()) {
        frame = session_.scene_frame(oc->id);
    }
    if (!frame) {
        return {};
    }
    render::StillPaint still;
    still.frame = scene::share_frame(std::move(*frame));
    still.size = size_or(size, preview_size(QString::fromStdString(oc->id), 320));
    still.cache = previews_.image_cache();
    still.faces = &previews_.measurer().faces();
    still.background_video_frame = playback_.background_frame();
    // Every source it may show: each video's picture and clock (a Preview's
    // slide, the cued ones', first: the outputs may still hold the last
    // picture a cued one showed there), pages, live sources, video objects.
    still.source_frames = playback_.cued_frames();
    const auto& content = playback_.content_frames();
    still.source_frames.insert(content.begin(), content.end());
    if (web_sources_ != nullptr) {
        const auto& pages = web_sources_->frames();
        still.source_frames.insert(pages.begin(), pages.end());
    }
    if (live_sources_ != nullptr) {
        const auto& live = live_sources_->frames();
        still.source_frames.insert(live.begin(), live.end());
    }
    const auto& objects = playback_.object_frames();
    still.source_frames.insert(objects.begin(), objects.end());
    // A video not published yet (its first tick to come) is pulled from its
    // player, so a still made at once shows it.
    for (const auto& source : render::frame_sources_of(*still.frame)) {
        if (render::source_image(&still.source_frames, source) == nullptr) {
            if (QImage picture = playback_.video_picture(source); !picture.isNull()) {
                still.source_frames[source].image = std::move(picture);
            }
        }
    }
    const QSize pull(still.size.width() * 2, still.size.height() * 2);
    if (playback_.background_player() != nullptr
        && playback_.background_player()->has_media()) {
        if (still.background_video_frame.isNull()) {
            still.background_video_frame =
                playback_.background_player()->current_frame(pull.width(), pull.height());
        }
    }
    return {.still = std::move(still)};
}

QImage OutputPreviewController::video_frame(const SideView* view) const
{
    const auto* st = view != nullptr ? view->runtime() : nullptr;
    if (st == nullptr || !st->video_mode_active() || st->media_instance() == 0) {
        return {};
    }
    return playback_.video_picture(scene::content_video_key(st->media_instance()));
}

QImage OutputPreviewController::live_web_frame(const QString& source) const
{
    if (web_sources_ == nullptr || source.isEmpty()) {
        return {};
    }
    const auto& frames = web_sources_->frames();
    const auto it = frames.find(source.toStdString());
    return it != frames.end() ? it->second.image : QImage();
}

QImage OutputPreviewController::live_source_frame(const QString& source) const
{
    if (live_sources_ == nullptr || source.isEmpty()) {
        return {};
    }
    const auto& frames = live_sources_->frames();
    const auto it = frames.find(source.toStdString());
    return it != frames.end() ? it->second.image : QImage();
}

void OutputPreviewController::set_output_host(render::OutputWindowHost* host)
{
    if (host_ == host) {
        return;
    }
    host_ = host;
    emit outputHostChanged();
}

}  // namespace seventhslide::app
