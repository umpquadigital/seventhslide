#include "output_preview_item.hpp"

#include "session_facade.hpp"

#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/presenter_view.hpp>

namespace seventhslide::app {

OutputPreviewItem::OutputPreviewItem(QQuickItem* parent)
    : SessionBoundItem(parent)
    , view_(new render::PresenterView(this))
{
    setClip(true);
    // Bars around a canvas of another aspect show the card behind them.
    view_->set_letterbox(Qt::transparent);
}

OutputPreviewItem::~OutputPreviewItem()
{
    detach();
}

void OutputPreviewItem::setOutputId(const QString& id)
{
    if (output_id_ == id) {
        return;
    }
    output_id_ = id;
    emit outputIdChanged();
    attach();
}

void OutputPreviewItem::connect_session(SessionFacade& session)
{
    keep(connect(session.outputPreviews(), &OutputPreviewController::outputHostChanged, this,
                 &OutputPreviewItem::attach));
}

void OutputPreviewItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    SessionBoundItem::geometryChange(newGeometry, oldGeometry);
    view_->setSize(newGeometry.size());
}

void OutputPreviewItem::attach()
{
    detach();
    SessionFacade* facade = session();
    render::OutputWindowHost* host = facade != nullptr ? facade->outputPreviews()->output_host() : nullptr;
    if (host == nullptr || output_id_.isEmpty()) {
        return;
    }
    host_ = host;
    held_id_ = output_id_;
    // The host deletes its presenters as it goes; stop drawing before then.
    connect(host, &QObject::destroyed, this, [this]() {
        view_->set_presenter(nullptr);
        held_id_.clear();
    });
    view_->set_presenter(host->hold_presenter(held_id_));
}

void OutputPreviewItem::detach()
{
    view_->set_presenter(nullptr);
    if (host_ != nullptr) {
        disconnect(host_, nullptr, this, nullptr);
        if (!held_id_.isEmpty()) {
            host_->release_presenter(held_id_);
        }
    }
    host_.clear();
    held_id_.clear();
}

} // namespace seventhslide::app
