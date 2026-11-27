#include "session_bound_item.hpp"

#include "session_facade.hpp"

namespace seventhslide::app {

SessionFacade* SessionLink::get() const
{
    return session_.data();
}

bool SessionLink::reset(SessionFacade* next)
{
    if (held_ == next) {
        return false;
    }
    for (const auto& connection : connections_) {
        QObject::disconnect(connection);
    }
    connections_.clear();
    held_ = next;
    session_ = next;
    return true;
}

void SessionLink::keep(QMetaObject::Connection connection)
{
    connections_.push_back(std::move(connection));
}

SessionBoundItem::SessionBoundItem(QQuickItem* parent)
    : QQuickItem(parent)
{
}

void SessionBoundItem::setSession(SessionFacade* session)
{
    if (!link_.reset(session)) {
        return;
    }
    if (session != nullptr) {
        // A session destroyed first is let go of as if set to none.
        keep(connect(session, &QObject::destroyed, this, [this] { setSession(nullptr); }));
        connect_session(*session);
    }
    emit sessionChanged();
    session_changed();
}

SessionBoundPaintedItem::SessionBoundPaintedItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
}

void SessionBoundPaintedItem::setSession(SessionFacade* session)
{
    if (!link_.reset(session)) {
        return;
    }
    if (session != nullptr) {
        // A session destroyed first is let go of as if set to none.
        keep(connect(session, &QObject::destroyed, this, [this] { setSession(nullptr); }));
        connect_session(*session);
    }
    emit sessionChanged();
    update();
}

} // namespace seventhslide::app
