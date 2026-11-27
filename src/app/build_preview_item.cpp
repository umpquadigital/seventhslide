#include "build_preview_item.hpp"

#include "slide_edit_session.hpp"

#include <seventhslide/render/presenter_view.hpp>

namespace seventhslide::app {

BuildPreviewItem::BuildPreviewItem(QQuickItem* parent)
    : QQuickItem(parent)
    , view_(new render::PresenterView(this))
{
}

QObject* BuildPreviewItem::animations() const
{
    return animations_.data();
}

void BuildPreviewItem::setAnimations(QObject* animations)
{
    auto* next = qobject_cast<SlideAnimationsSession*>(animations);
    if (animations_ == next) {
        return;
    }
    if (animations_ != nullptr) {
        disconnect(animations_, &QObject::destroyed, this, nullptr);
    }
    animations_ = next;
    view_->set_presenter(next != nullptr ? next->presenter() : nullptr);
    if (next != nullptr) {
        // The presenter goes with its session; the view must not outlive it.
        connect(next, &QObject::destroyed, this, [this] { view_->set_presenter(nullptr); });
    }
    emit animationsChanged();
}

void BuildPreviewItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    view_->setSize(newGeometry.size());
}

}  // namespace seventhslide::app
