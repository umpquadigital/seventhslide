#pragma once

// An output card's live preview: the output's own presentation, drawn the way
// its audience window draws it.

#include "session_bound_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QPointer>
#include <QString>

namespace seventhslide::render {
class OutputWindowHost;
class PresenterView;
}

namespace seventhslide::app {

/**
 * QML: ``OutputPreviewItem { outputId: "..."; session: root.session }``
 *
 * Holds the output's ScenePresenter from the session's output host and shows
 * it through a PresenterView, like every destination of the output: the
 * preview cannot disagree with the audience about the picture or about
 * whether a transition runs, and composes nothing of its own. Without a host
 * (a test that sets none) it shows the card's background.
 */
class OutputPreviewItem : public SessionBoundItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString outputId READ outputId WRITE setOutputId NOTIFY outputIdChanged)

public:
    explicit OutputPreviewItem(QQuickItem* parent = nullptr);
    ~OutputPreviewItem() override;

    [[nodiscard]] QString outputId() const { return output_id_; }
    void setOutputId(const QString& id);

signals:
    void outputIdChanged();

protected:
    void connect_session(SessionFacade& session) override;
    void session_changed() override { attach(); }
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    /// Hold the presenter of ``output_id_`` from the session's host, letting
    /// go of the one held before.
    void attach();
    void detach();

    QString output_id_;
    render::PresenterView* view_ = nullptr;
    /// The host the presenter is held from, and the output it is held for.
    QPointer<render::OutputWindowHost> host_;
    QString held_id_;
};

} // namespace seventhslide::app
