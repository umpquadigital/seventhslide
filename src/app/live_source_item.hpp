#pragma once

// A live source (camera) in a well of the Controller: the picture of the one
// its side shows, as it arrives. No transport: a live source cannot be paused
// or sought.

#include "live_frame_item.hpp"

#include "side_view.hpp"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace seventhslide::app {

/// QML: ``LiveSourceItem { session: root.session; view: well.view }``
class LiveSourceItem : public LiveFrameItem {
    Q_OBJECT
    QML_ELEMENT
    /// The side whose source it shows.
    Q_PROPERTY(seventhslide::app::SideView* view READ view WRITE setView NOTIFY viewChanged)

public:
    explicit LiveSourceItem(QQuickItem* parent = nullptr);

    [[nodiscard]] SideView* view() const { return view_; }
    void setView(SideView* view);

signals:
    void viewChanged();

protected:
    void connect_session(SessionFacade& session) override;
    void session_changed() override { take_frame(); }

private:
    void take_frame();

    QPointer<SideView> view_;
};

} // namespace seventhslide::app
