#pragma once

// A live source's thumbnail in the Live tab, kept a second fresh from its
// preview stream (LiveLibraryController).

#include "live_frame_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QString>

namespace seventhslide::app {

/// QML: ``LiveThumbItem { session: root.session; sourceKey: "ndi:HOST (Cam)" }``
class LiveThumbItem : public LiveFrameItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourceKey READ sourceKey WRITE setSourceKey NOTIFY sourceKeyChanged)

public:
    explicit LiveThumbItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QString sourceKey() const { return key_; }
    void setSourceKey(const QString& key);

signals:
    void sourceKeyChanged();

protected:
    void connect_session(SessionFacade& session) override;
    void session_changed() override { take_frame(); }

private:
    void take_frame();

    QString key_;
};

} // namespace seventhslide::app
