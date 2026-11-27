#include "live_video_item.hpp"

#include "session_facade.hpp"

namespace seventhslide::app {

LiveVideoItem::LiveVideoItem(QQuickItem* parent)
    : LiveFrameItem(parent)
{
}

void LiveVideoItem::connect_session(SessionFacade& session)
{
    // A new frame from the playback tick, or a change to what is live
    // (another clip, a paused one showing its first frame).
    keep(connect(&session, &SessionFacade::videoFrameChanged, this, &LiveVideoItem::take_frame));
    keep(connect(&session, &SessionFacade::previewRevisionChanged, this, &LiveVideoItem::take_frame));
}

void LiveVideoItem::setView(SideView* view)
{
    if (view_ == view) {
        return;
    }
    if (view_) {
        disconnect(view_, nullptr, this, nullptr);
    }
    view_ = view;
    if (view_) {
        // Another video on the side, or another stack in focus.
        connect(view_, &SideView::focusedStackChanged, this, &LiveVideoItem::take_frame);
    }
    emit viewChanged();
    take_frame();
}

void LiveVideoItem::take_frame()
{
    SessionFacade* facade = session();
    show_frame(facade != nullptr && view_ ? facade->outputPreviews()->video_frame(view_) : QImage());
}

} // namespace seventhslide::app
