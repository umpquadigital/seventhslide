#pragma once

// A web page in a well of the Controller: the page its side shows, worked
// from there (see WebPageItem).

#include "side_view.hpp"
#include "web_page_item.hpp"

#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QString>

namespace seventhslide::app {

/// QML: ``LiveWebItem { session: root.session; view: well.view }``
class LiveWebItem : public WebPageItem {
    Q_OBJECT
    QML_ELEMENT
    /// The side whose page it shows.
    Q_PROPERTY(seventhslide::app::SideView* view READ view WRITE setView NOTIFY viewChanged)

public:
    explicit LiveWebItem(QQuickItem* parent = nullptr);

    [[nodiscard]] SideView* view() const { return view_; }
    void setView(SideView* view);

signals:
    void viewChanged();

protected:
    void connect_session(SessionFacade& session) override;
    void session_changed() override { take_frame(); }
    /// The side's page's renderer; null when it shows none.
    [[nodiscard]] WebRenderer* renderer() const override;
    /// The session's.
    [[nodiscard]] PointerConfinement* pointer_confinement() const override;

private:
    /// Take the page's latest picture if it is a new one.
    void take_frame();

    QPointer<SideView> view_;
    /// The page's source, as last shown.
    QString source_;
};

} // namespace seventhslide::app
