#include "live_source_item.hpp"

#include "session_facade.hpp"

namespace seventhslide::app {

LiveSourceItem::LiveSourceItem(QQuickItem* parent)
    : LiveFrameItem(parent)
{
}

void LiveSourceItem::connect_session(SessionFacade& session)
{
    // A new picture.
    keep(connect(&session, &SessionFacade::liveSourceFrameChanged, this, &LiveSourceItem::take_frame));
}

void LiveSourceItem::setView(SideView* view)
{
    if (view_ == view) {
        return;
    }
    if (view_) {
        disconnect(view_, nullptr, this, nullptr);
    }
    view_ = view;
    if (view_) {
        // Another source on the side, or another stack in focus.
        connect(view_, &SideView::focusedStackChanged, this, &LiveSourceItem::take_frame);
    }
    emit viewChanged();
    take_frame();
}

void LiveSourceItem::take_frame()
{
    SessionFacade* facade = session();
    show_frame(facade != nullptr && view_
                   ? facade->outputPreviews()->live_source_frame(view_->live_source_key())
                   : QImage());
}

} // namespace seventhslide::app
