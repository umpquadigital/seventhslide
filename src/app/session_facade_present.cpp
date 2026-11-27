// SessionFacade: following what is live and putting it on the outputs.
//
// One slice of SessionFacade (declared in session_facade.hpp): what follows a
// change of live content (after_mutation), the remote's views of the session
// (run_in_view), the clock, and the present path itself -
// queue_present_frames, present_frames. What goes live is LiveController's.

#include "session_facade.hpp"

#include "media_controller.hpp"
#include "mixer_controller.hpp"
#include "output_list_model.hpp"
#include "playback_controller.hpp"
#include "service_item_model.hpp"
#include "stack_controller.hpp"
#include "side_view.hpp"
#include "live_sources.hpp"
#include "web_sources.hpp"

#include <seventhslide/canvas/object.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QTimer>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::app {

qint64 SessionFacade::openServiceId() const noexcept
{
    if (const auto id = session_.service_id()) {
        return static_cast<qint64>(*id);
    }
    return -1;
}

void SessionFacade::bump_preview_revision()
{
    ++preview_revision_;
    emit previewRevisionChanged();
}

void SessionFacade::run_in_view(std::string& view_stack, const std::function<void()>& fn)
{
    if (view_scope_) {
        fn();
        return;
    }
    ViewScope scope;
    scope.view_stack = &view_stack;
    scope.console = session_.focused_stack_id();
    scope.console_held = session_.focused_stack().holds_content();
    if (!view_stack.empty()) {
        session_.set_focused_stack_id(view_stack);
    }
    scope.entered = session_.focused_stack_id();
    view_scope_ = std::move(scope);
    try {
        fn();
    } catch (...) {
        leave_view();
        throw;
    }
    leave_view();
}

void SessionFacade::leave_view()
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): only run_in_view calls it, in the scope it set.
    ViewScope scope = std::move(*view_scope_);
    view_scope_.reset();
    const auto landed = session_.focused_stack_id();
    // Content the command sent live elsewhere takes the view's tab with it.
    if (landed != scope.entered) {
        *scope.view_stack = landed;
    }
    // The console keeps its tab, unless the command left it with nothing
    // live or cued: then it moves as it would after clearing it itself.
    std::string console = scope.console;
    const auto* was = session_.find_stack(console);
    if (scope.console_held && was != nullptr && !was->holds_content()) {
        if (const auto* front = session_.frontmost_holding()) {
            console = front->id();
        }
    }
    session_.set_focused_stack_id(console);
    const bool swapped = scope.entered != scope.console || landed != scope.console
                         || console != scope.console;
    if (scope.pending) {
        after_mutation(scope.rebuild);
    } else if (swapped) {
        for (auto* side : sides()) {
            side->reload_lines();
        }
        service_items_->notify_selection_changed();
        stack_ctl_->refresh_state();
        emit focusedStackChanged();
        emit sessionChanged();
    }
    // Media signals sent while the view's tab was in focus described that tab.
    if (swapped) {
        emit mediaStateChanged();
    }
}

void SessionFacade::after_mutation(bool rebuild_models)
{
    if (view_scope_) {
        // A command for another view is still running against its own tab.
        // Announce once the console has its focus back, so nothing showing
        // the console reads the other view's stack.
        view_scope_->pending = true;
        view_scope_->rebuild = view_scope_->rebuild || rebuild_models;
        return;
    }
    // Load/play video and audio before preview URLs refresh so the first
    // thumbnail request does not capture the hold-frame / empty-frame black plate.
    mixer_->sync();
    playback_->sync_backends();
    web_sources_->sync();
    live_sources_->sync();
    bump_preview_revision();
    if (rebuild_models) {
        service_items_->reload();
        for (auto* side : sides()) {
            side->reload_lines();
        }
        // An output added, removed, renamed or switched on or off: refresh
        // the host's names and the open / screen state the output strip shows.
        const bool outputs_changed = outputs_model_->reload();
        if (outputs_on_model_->reload() || outputs_changed) {
            sync_output_host_state();
        }
    } else {
        outputs_model_->notify_rows_changed();
        outputs_on_model_->notify_rows_changed();
        for (auto* side : sides()) {
            side->notify_lines_moved();
        }
    }
    // An output's canvas can change without the rows doing so.
    output_previews_->refresh_slide_aspect();
    stack_ctl_->refresh_state();
    emit focusedStackChanged();
    emit sessionChanged();
    queue_present_frames();
}

void SessionFacade::queue_present_frames()
{
    if (present_queued_) {
        return;
    }
    present_queued_ = true;
    QMetaObject::invokeMethod(this, [this] {
        present_queued_ = false;
        present_frames();
    }, Qt::QueuedConnection);
}

void SessionFacade::on_clock_tick()
{
    // Only the time moved, and only on the outputs showing a clock: recompose
    // those. No preview revision bump either: that reloads the live video
    // preview, which has no clock in it.
    for (const auto& [output_id, frame] : shown_frames_) {
        if (!render::scene_frame_has_clock(*frame)) {
            continue;
        }
        if (auto fresh = session_.scene_frame(output_id)) {
            presented_frames_.push_back(scene::share_frame(std::move(*fresh)));
        }
    }
    publish_frames(false);
}

void SessionFacade::present_frames()
{
    // Compose once for this pass; every destination of an output, its preview
    // included, draws from the one presenter these go to, and the theme's
    // background video is found in the same frames.
    for (auto& frame : session_.scene_frames()) {
        presented_frames_.push_back(scene::share_frame(std::move(frame)));
    }
    playback_->sync_background_video(presented_frames_);
    publish_frames(true);
    prefetch_neighbour_images();
    follow_live_arrivals();
}

std::set<std::string> SessionFacade::live_media() const
{
    std::set<std::string> urls;
    for (const auto& [output_id, frame] : shown_frames_) {
        scene::note_frame_media(*frame, urls);
    }
    const auto note = [&](std::string_view subdir, const std::string& name) {
        if (name.starts_with("/static/")) {
            urls.insert(name);
        } else if (auto url = model::static_media_url(subdir, name)) {
            urls.insert(std::move(*url));
        }
    };
    for (const auto* stack : session_.occupied_stacks()) {
        const auto& st = stack->program;
        for (const auto& image : st.image_playlist().images) {
            note(model::k_images_subdir, image);
        }
        if (!st.playback().empty()) {
            note(st.audio_mode_active() ? model::k_audio_subdir : model::k_videos_subdir,
                 st.playback().filename);
        }
        if (!st.freeform_mode_active()) {
            continue;
        }
        // The slides to come, not only the one showing.
        for (const auto& deck : st.freeform().document.decks) {
            for (const auto& slide : deck.slides) {
                if (slide.background.kind == freeform::SlideBackgroundKind::Image) {
                    note(model::k_images_subdir, slide.background.image);
                }
                for (const auto& object : slide.objects) {
                    if (auto url = canvas::object_media_url(object)) {
                        urls.insert(std::move(*url));
                    }
                    if (auto url = canvas::fill_picture_url(object.fill)) {
                        urls.insert(std::move(*url));
                    }
                }
            }
        }
    }
    constexpr std::string_view k_static = "/static/";
    std::set<std::string> out;
    for (const auto& url : urls) {
        out.insert(url.substr(k_static.size()));
    }
    return out;
}

void SessionFacade::follow_live_arrivals()
{
    // Most of the time every file is here: nothing to look through.
    QVariantList arrivals;
    if (media_->has_arrivals()) {
        for (const auto& path : live_media()) {
            auto arrival = media_->arrival_at(path);
            if (!arrival.isEmpty()) {
                arrival.insert(QStringLiteral("file"),
                               QString::fromStdString(std::string(model::media_basename(path))));
                arrivals.push_back(arrival);
            }
        }
    }
    if (arrivals != live_arrivals_) {
        live_arrivals_ = std::move(arrivals);
        emit liveArrivalsChanged();
    }
}

void SessionFacade::publish_frames(bool every_output)
{
    if (every_output) {
        shown_frames_.clear();
    }
    for (const auto& frame : presented_frames_) {
        shown_frames_.insert_or_assign(frame->output_id, frame);
    }
    if (host_ != nullptr) {
        // Before the emit, so whatever it prompts sees the destinations
        // already showing this pass.
        host_->present_all(presented_frames_);
    }
    emit framesChanged();
    presented_frames_.clear();

    // The clock ticks while an output shows one.
    const bool clock_shown =
        std::any_of(shown_frames_.begin(), shown_frames_.end(), [](const auto& entry) {
            return render::scene_frame_has_clock(*entry.second);
        });
    if (clock_shown && !clock_tick_->isActive()) {
        clock_tick_->start();
    } else if (!clock_shown && clock_tick_->isActive()) {
        clock_tick_->stop();
    }
}

void SessionFacade::prefetch_neighbour_images()
{
    if (!session_.focused_stack().program.image_mode_active()) {
        return;
    }
    const auto& pl = session_.focused_stack().program.image_playlist();
    if (pl.count() <= 1) {
        return;
    }
    std::vector<std::string> urls;
    urls.reserve(2);
    auto push_url = [&](int idx) {
        if (idx < 0 || idx >= pl.count()) {
            return;
        }
        if (const auto url = model::static_media_url(model::k_images_subdir, pl.images[static_cast<std::size_t>(idx)])) {
            urls.push_back(*url);
        }
    };
    push_url(pl.index - 1);
    push_url(pl.index + 1);
    if (urls.empty()) {
        return;
    }
    // Canvas sizes come from the outputs themselves; composing a second set of
    // frames just to read them back was the most expensive way to ask.
    int edge = 1920;
    for (const auto& oc : session_.outputs()) {
        edge = std::max(edge, std::max(oc.canvas_width, oc.canvas_height));
    }
    // Defer so the current slide paints first.
    QTimer::singleShot(0, this, [this, urls = std::move(urls), edge]() {
        if (host_) {
            host_->prefetch_static_urls(urls, edge);
        }
    });
}

} // namespace seventhslide::app
