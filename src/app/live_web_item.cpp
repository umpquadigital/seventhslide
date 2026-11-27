#include "live_web_item.hpp"

#include "session_facade.hpp"
#include "web_sources.hpp"

namespace seventhslide::app {

LiveWebItem::LiveWebItem(QQuickItem* parent)
    : WebPageItem(parent)
{
}

void LiveWebItem::connect_session(SessionFacade& session)
{
    // A new picture.
    keep(connect(&session, &SessionFacade::webFrameChanged, this, &LiveWebItem::take_frame));
}

void LiveWebItem::setView(SideView* view)
{
    if (view_ == view) {
        return;
    }
    if (view_) {
        disconnect(view_, nullptr, this, nullptr);
    }
    view_ = view;
    if (view_) {
        // Another page on the side, or another stack in focus.
        connect(view_, &SideView::focusedStackChanged, this, &LiveWebItem::take_frame);
    }
    emit viewChanged();
    take_frame();
}

void LiveWebItem::take_frame()
{
    SessionFacade* facade = session();
    const QString source = facade != nullptr && view_ ? view_->webSource() : QString();
    QImage next = facade != nullptr ? facade->outputPreviews()->live_web_frame(source) : QImage();
    // Another page, or none: the one that was being worked is given back.
    if (source != source_ || next.isNull()) {
        release();
    }
    source_ = source;
    show_frame(std::move(next));
}

WebRenderer* LiveWebItem::renderer() const
{
    SessionFacade* facade = session();
    if (facade == nullptr || !view_ || frame().isNull()) {
        return nullptr;
    }
    return facade->web_sources()->renderer(view_->webSource().toStdString());
}

PointerConfinement* LiveWebItem::pointer_confinement() const
{
    SessionFacade* facade = session();
    return facade != nullptr ? facade->pointer_confinement() : nullptr;
}

} // namespace seventhslide::app
