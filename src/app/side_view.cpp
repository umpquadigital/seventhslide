// One side of the focused stack, as a well shows and works it (see the header).

#include "side_view.hpp"

#include "entry_error.hpp"
#include "live_sources.hpp"
#include "web_sources.hpp"

#include <seventhslide/model/web_page.hpp>

#include <QUrl>

#include <exception>

namespace seventhslide::app {

SideView::SideView(session::Session& session, session::StackSide side, QObject* parent)
    : QObject(parent)
    , session_(session)
    , side_(side)
    , lines_(std::make_unique<LineMapModel>(session, side))
{
    // Whatever else about the media changed, the clock read may have too.
    connect(this, &SideView::mediaStateChanged, this, &SideView::mediaPositionChanged);
}

SideView::~SideView() = default;

const session::StackRuntime* SideView::runtime() const
{
    return session_.runtime({session_.focused_stack_id(), side_});
}

session::StackRuntime* SideView::runtime_mut()
{
    return session_.runtime({session_.focused_stack_id(), side_});
}

template <typename Move>
bool SideView::act(const char* entry, Move move)
{
    return guarded_change(
        *this, entry,
        [&] {
            auto* st = runtime_mut();
            return st != nullptr && move(*st);
        },
        [this] { emit liveChanged(false); });
}

bool SideView::occupied() const
{
    const auto* st = runtime();
    return st != nullptr && st->occupied();
}

qint64 SideView::serviceItemId() const
{
    const auto* st = runtime();
    const auto id = st != nullptr ? st->service_item_id() : std::nullopt;
    return id ? static_cast<qint64>(*id) : -1;
}

int SideView::lineCursor() const
{
    const auto* st = runtime();
    if (st == nullptr) {
        return 0;
    }
    return st->image_mode_active() ? st->image_playlist().index : st->player().line_cursor();
}

int SideView::totalLines() const
{
    const auto* st = runtime();
    if (st == nullptr) {
        return 0;
    }
    return st->image_mode_active() ? st->image_playlist().count() : st->player().total_lines();
}

int SideView::currentStep() const
{
    const auto* st = runtime();
    return st == nullptr || st->image_mode_active() ? 0 : st->player().step();
}

int SideView::step_count() const
{
    const auto* st = runtime();
    if (st == nullptr || st->image_mode_active()) {
        return 1;
    }
    return st->player().steps_at(st->player().line_cursor());
}

bool SideView::imageMode() const
{
    const auto* st = runtime();
    return st != nullptr && st->image_mode_active();
}

bool SideView::videoMode() const
{
    const auto* st = runtime();
    return st != nullptr && st->video_mode_active();
}

bool SideView::mediaTransport() const
{
    const auto* st = runtime();
    return st != nullptr && st->has_transport();
}

bool SideView::mediaPlaying() const
{
    const auto* st = runtime();
    return st != nullptr && st->is_playing();
}

double SideView::mediaPositionSec() const
{
    const auto* st = runtime();
    return st != nullptr ? st->media_position_sec() : 0.0;
}

double SideView::mediaDurationSec() const
{
    const auto* st = runtime();
    return st != nullptr ? st->media_duration_sec() : 0.0;
}

QString SideView::currentImageFilename() const
{
    const auto* st = runtime();
    if (st == nullptr || !st->image_mode_active()) {
        return {};
    }
    const auto& pl = st->image_playlist();
    if (pl.empty() || pl.index < 0 || pl.index >= pl.count()) {
        return {};
    }
    return QString::fromStdString(pl.images[static_cast<std::size_t>(pl.index)]);
}

bool SideView::freeformGallery() const
{
    const auto* st = runtime();
    return st != nullptr && st->freeform_mode_active()
           && !st->freeform().document.line_control;
}

quint64 SideView::build_id() const
{
    const auto* st = runtime();
    return st != nullptr ? st->build_id() : 0;
}

bool SideView::nextSlide()
{
    return act(__func__, [](session::StackRuntime& st) { return st.next_slide(); });
}

bool SideView::prevSlide()
{
    return act(__func__, [](session::StackRuntime& st) { return st.prev_slide(); });
}

bool SideView::jumpToLine(int line_index)
{
    return act(__func__,
               [line_index](session::StackRuntime& st) { return st.jump_to_line(line_index); });
}

bool SideView::jumpToStep(int line_index, int step)
{
    return act(__func__, [line_index, step](session::StackRuntime& st) {
        return st.jump_to_step(line_index, step);
    });
}

bool SideView::mediaPlay()
{
    // The runtime drives its player itself, when it has one; the
    // application syncs the players and the frame tick after.
    return act(__func__, [](session::StackRuntime& st) { return st.media_play(); });
}

bool SideView::mediaPause()
{
    return act(__func__, [](session::StackRuntime& st) { return st.media_pause(); });
}

bool SideView::mediaSeek(double position_sec)
try {
    auto* st = runtime_mut();
    if (st == nullptr || !st->media_seek(position_sec)) {
        return false;
    }
    // A scrub: nothing goes live, but the picture held there is drawn again.
    emit sought();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

bool SideView::mediaRestart()
{
    return act(__func__, [](session::StackRuntime& st) { return st.media_restart(); });
}

void SideView::reload_lines()
{
    lines_->reload();
}

void SideView::notify_lines_moved()
{
    lines_->notify_rows_changed();
}

// --- the web page ---

void SideView::set_web_sources(WebSources* sources)
{
    if (web_sources_ != nullptr) {
        disconnect(web_sources_, nullptr, this, nullptr);
    }
    web_sources_ = sources;
    if (web_sources_ != nullptr) {
        connect(web_sources_, &WebSources::stateChanged, this, &SideView::webStateChanged);
    }
    emit webStateChanged();
}

bool SideView::webMode() const
{
    const auto* st = runtime();
    return st != nullptr && st->web_mode_active();
}

QString SideView::webSource() const
{
    const auto* st = runtime();
    if (st == nullptr || !st->web_mode_active()) {
        return {};
    }
    const auto& web = st->scene_extras().web;
    return web ? QString::fromStdString(web->source) : QString{};
}

QString SideView::webProblem() const
{
    if (!webMode()) {
        return {};
    }
    const auto source = webSource().toStdString();
    if (source.empty()) {
        return tr("This page has no address it can show.");
    }
    if (web_sources_ == nullptr || !web_sources_->can_draw()) {
        return tr("Web pages cannot be shown in this build of SeventhSlide.");
    }
    const auto* renderer = web_renderer();
    return renderer != nullptr ? renderer->problem() : QString();
}

WebRenderer* SideView::web_renderer() const
{
    return web_sources_ != nullptr ? web_sources_->renderer(webSource().toStdString()) : nullptr;
}

bool SideView::webLoading() const
{
    const auto* renderer = web_renderer();
    return renderer != nullptr && renderer->problem().isEmpty()
           && !web_sources_->frames().contains(webSource().toStdString());
}

QString SideView::webUrl() const
{
    if (!webMode()) {
        return {};
    }
    if (const auto* renderer = web_renderer(); renderer != nullptr && !renderer->url().isEmpty()) {
        // Spaces as %20, as typed addresses have them: the address shown can
        // be edited and gone to again.
        return renderer->url().toString(QUrl::EncodeSpaces);
    }
    // Not drawn here: the address the item was added with.
    const auto& web = runtime()->scene_extras().web;
    return web ? QString::fromStdString(web->url) : QString();
}

bool SideView::webCanGoBack() const
{
    const auto* renderer = web_renderer();
    return renderer != nullptr && renderer->can_go_back();
}

bool SideView::webGoBack()
{
    auto* renderer = web_renderer();
    if (renderer == nullptr || !renderer->can_go_back()) {
        return false;
    }
    renderer->go_back();
    return true;
}

QString SideView::webGoTo(const QString& text)
{
    if (!webMode()) {
        return tr("No web page is live.");
    }
    const auto url = model::web_url_from_text(text.toStdString());
    if (!url) {
        return tr("Enter a web address, such as example.com or https://example.com/page, "
                  "or a file's path.");
    }
    auto* renderer = web_renderer();
    if (renderer == nullptr || !renderer->navigate(QUrl(QString::fromStdString(*url)))) {
        const auto why = webProblem();
        return why.isEmpty() ? tr("Web pages cannot be shown in this build of SeventhSlide.") : why;
    }
    return {};
}

bool SideView::reloadWeb()
{
    auto* renderer = web_renderer();
    if (renderer == nullptr) {
        return false;
    }
    renderer->reload();
    return true;
}

// --- the live source ---

void SideView::set_live_sources(LiveSources* sources)
{
    if (live_sources_ != nullptr) {
        disconnect(live_sources_, nullptr, this, nullptr);
    }
    live_sources_ = sources;
    if (live_sources_ != nullptr) {
        connect(live_sources_, &LiveSources::stateChanged, this,
                &SideView::liveSourceStateChanged);
    }
    emit liveSourceStateChanged();
}

bool SideView::liveSourceMode() const
{
    const auto* st = runtime();
    return st != nullptr && st->live_source_active();
}

QString SideView::live_source_key() const
{
    const auto* st = runtime();
    if (st == nullptr || !st->live_source_active()) {
        return {};
    }
    const auto& live = st->scene_extras().live;
    return live ? QString::fromStdString(live->source) : QString{};
}

QString SideView::liveSourceProblem() const
{
    if (!liveSourceMode()) {
        return {};
    }
    const auto key = live_source_key().toStdString();
    if (key.empty()) {
        return tr("This live source is of a kind this build cannot receive.");
    }
    const auto* receiver = live_sources_ != nullptr ? live_sources_->receiver(key) : nullptr;
    return receiver != nullptr ? receiver->problem()
                               : tr("This live source cannot be received in this build.");
}

bool SideView::liveSourceLost() const
{
    if (!liveSourceMode() || live_sources_ == nullptr) {
        return false;
    }
    const auto* receiver = live_sources_->receiver(live_source_key().toStdString());
    return receiver != nullptr && receiver->lost();
}

}  // namespace seventhslide::app
