#pragma once

// The items that draw from the session: the live pictures in the Controller,
// the output cards' previews and the editor stage's painted shapes, pictures
// and text. Each takes the session as a typed ``session`` property, follows
// what it needs of it while it holds it, and draws again when it changes.
//
// An item painted with QPainter is a QQuickPaintedItem and the rest are
// QQuickItems, and moc takes no template, so there are two bases. What they
// do is SessionLink's.

#include <QtQml/qqmlregistration.h>
#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickPaintedItem>

#include <utility>
#include <vector>

// The property's type is only named here; moc needs it whole to register it.
Q_MOC_INCLUDE("session_facade.hpp")

namespace seventhslide::app {

class SessionFacade;

/// The session an item holds, and the connections it made to follow it.
class SessionLink {
public:
    [[nodiscard]] SessionFacade* get() const;
    /// Hold ``next`` (null: none), letting go of every connection kept for
    /// the one before. False, and nothing changes, when it is the one held.
    /// A session destroyed while held is still the one held until it is let
    /// go, so the item can let go of it as it goes.
    bool reset(SessionFacade* next);
    /// Keep ``connection`` while this session is held.
    void keep(QMetaObject::Connection connection);

private:
    /// Which session is held; session_ says whether it is still there.
    const SessionFacade* held_ = nullptr;
    QPointer<SessionFacade> session_;
    std::vector<QMetaObject::Connection> connections_;
};

/// QML: ``session: root.session`` on an item drawn as a scene graph node.
class SessionBoundItem : public QQuickItem {
    Q_OBJECT
    QML_ANONYMOUS
    /// The session it draws from; null draws nothing.
    Q_PROPERTY(seventhslide::app::SessionFacade* session READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit SessionBoundItem(QQuickItem* parent = nullptr);

    [[nodiscard]] SessionFacade* session() const { return link_.get(); }
    void setSession(SessionFacade* session);

signals:
    void sessionChanged();

protected:
    /// Connect what the item follows of the session just taken, each
    /// connection kept with keep().
    virtual void connect_session(SessionFacade& /*session*/) {}
    /// Draw again from the session now held (or none).
    virtual void session_changed() = 0;
    void keep(QMetaObject::Connection connection) { link_.keep(std::move(connection)); }

private:
    SessionLink link_;
};

/// QML: ``session: root.session`` on an item painted with QPainter.
class SessionBoundPaintedItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ANONYMOUS
    /// The session whose fonts and image cache it paints with; null paints
    /// nothing that needs them.
    Q_PROPERTY(seventhslide::app::SessionFacade* session READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit SessionBoundPaintedItem(QQuickItem* parent = nullptr);

    [[nodiscard]] SessionFacade* session() const { return link_.get(); }
    void setSession(SessionFacade* session);

signals:
    void sessionChanged();

protected:
    /// Connect what the item follows of the session just taken, each
    /// connection kept with keep().
    virtual void connect_session(SessionFacade& /*session*/) {}
    void keep(QMetaObject::Connection connection) { link_.keep(std::move(connection)); }

private:
    SessionLink link_;
};

} // namespace seventhslide::app
