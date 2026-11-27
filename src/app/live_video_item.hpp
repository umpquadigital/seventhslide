#pragma once

// A video in a well of the Controller: the one its side shows, drawn from
// its player's frames themselves rather than reloaded through an image
// provider on every frame.

#include "live_frame_item.hpp"
#include "side_view.hpp"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace seventhslide::app {

/**
 * QML: ``LiveVideoItem { session: root.session; view: well.view }``
 *
 * Shows the current frame of the video its view's side shows, stretched to
 * the item. Its implicit size is the frame's, so a parent can fit it by
 * aspect; both are zero until a frame arrives.
 */
class LiveVideoItem : public LiveFrameItem {
    Q_OBJECT
    QML_ELEMENT
    /// The side whose video it shows.
    Q_PROPERTY(seventhslide::app::SideView* view READ view WRITE setView NOTIFY viewChanged)

public:
    explicit LiveVideoItem(QQuickItem* parent = nullptr);

    [[nodiscard]] SideView* view() const { return view_; }
    void setView(SideView* view);

signals:
    void viewChanged();

protected:
    void connect_session(SessionFacade& session) override;
    void session_changed() override { take_frame(); }

private:
    /// Take the video's current frame if it is a new one.
    void take_frame();

    QPointer<SideView> view_;
};

} // namespace seventhslide::app
