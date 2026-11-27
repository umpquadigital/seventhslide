#pragma once

// The editor's build preview: the current slide played as an
// output would play it - it comes in, then each step's builds - over the
// stage, from the slide editor's own presenter.

#include <QtQml/qqmlregistration.h>
#include <QPointer>
#include <QQuickItem>

namespace seventhslide::render {
class PresenterView;
}

namespace seventhslide::app {

class SlideAnimationsSession;

/// QML: ``BuildPreviewItem { animations: dialog.edit.animations }``.
class BuildPreviewItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* animations READ animations WRITE setAnimations NOTIFY animationsChanged)

public:
    explicit BuildPreviewItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* animations() const;
    void setAnimations(QObject* animations);

signals:
    void animationsChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    QPointer<SlideAnimationsSession> animations_;
    render::PresenterView* view_ = nullptr;
};

}  // namespace seventhslide::app
