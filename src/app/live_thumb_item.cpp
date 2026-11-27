#include "live_thumb_item.hpp"

#include "live_library_controller.hpp"
#include "session_facade.hpp"

namespace seventhslide::app {

LiveThumbItem::LiveThumbItem(QQuickItem* parent)
    : LiveFrameItem(parent)
{
}

void LiveThumbItem::connect_session(SessionFacade& session)
{
    keep(connect(session.liveLibrary(), &LiveLibraryController::thumbnailsChanged, this,
                 &LiveThumbItem::take_frame));
}

void LiveThumbItem::setSourceKey(const QString& key)
{
    if (key_ == key) {
        return;
    }
    key_ = key;
    emit sourceKeyChanged();
    take_frame();
}

void LiveThumbItem::take_frame()
{
    SessionFacade* facade = session();
    show_frame(facade != nullptr ? facade->liveLibrary()->thumbnail(key_) : QImage());
}

} // namespace seventhslide::app
