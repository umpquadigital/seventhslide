#pragma once

// A Qt Quick item showing one output's ScenePresenter: how an audience window
// draws, the same way NDI, Blackmagic and virtual camera destinations do.
//
// The presenter composites (once per size for every destination on the
// output, with its cached cross-fade layers) and this item uploads the result
// as a texture, the same one from picture to picture on a graphics API
// (``PictureTexture``). It keeps no scene state of its own - no fade engine,
// no image cache, no video frames - so a window cannot disagree with the
// other destinations about what the output shows or whether a transition
// runs.
//
// It looks at the presenter on the destinations' cadence (``k_fade_step_ms``)
// only while something moves there: a transition or a build running on its
// clock, or a new picture, which another may follow (a video's). Otherwise it
// sleeps, and the presenter (``ScenePresenter::watch``) wakes it when what it
// shows changes: a new frame, a clock's next second, a source's picture.
//
// Seam: Qt Quick. GUI thread, but for updatePaintNode, on the render thread
// while the GUI thread waits. The presenter's worker composites (RENDER-1):
// this asks for a picture and shows it when it comes, never compositing on the
// window's thread or the render thread.

#include <QColor>
#include <QImage>
#include <QQuickItem>

#include <cstdint>
#include <limits>
#include <memory>

class QTimer;

namespace seventhslide::render {

class ScenePresenter;

class PresenterView final : public QQuickItem {
    Q_OBJECT

public:
    explicit PresenterView(QQuickItem* parent = nullptr);

    /// The presentation to show. Not owned; must outlive the view or be
    /// replaced before it goes.
    void set_presenter(ScenePresenter* presenter);
    [[nodiscard]] ScenePresenter* presenter() const noexcept { return presenter_; }

    /// Bars around a canvas whose aspect differs from the item's, and the fill
    /// before there is anything to show.
    void set_letterbox(const QColor& color);

    /// Look at the presenter now rather than when it next wakes the view or
    /// on the next tick.
    void refresh();

    /// Composite no taller than ``max_height`` device pixels (0: the item's
    /// own size), in the item's shape, and stretch that over the item: see
    /// ``destination_send_size``.
    void set_max_height(int max_height);

    /// Size in device pixels the view composites at.
    [[nodiscard]] QSize pixel_size() const;

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& next, const QRectF& previous) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    /// Advance any transition and ask for a new picture when it moved; keep
    /// ticking while something moves, and stop once nothing does.
    void tick();
    /// Ask for a new picture if there is one: whether there was.
    bool look();
    /// The presenter's picture changed: look at once if not ticking.
    void wake();
    /// The worker's picture for revision ``revision`` arrived.
    void take_picture(QImage picture, std::uint64_t revision);
    /// What it showed is gone from its window: ask for the picture again.
    void show_again();

    ScenePresenter* presenter_ = nullptr;
    QColor letterbox_{Qt::black};
    int max_height_ = 0;
    QTimer* timer_ = nullptr;
    /// The presenter's calls to ``wake``, while held.
    std::shared_ptr<void> watch_;
    /// A look is queued from ``wake``.
    bool wake_queued_ = false;
    /// Its window's ``sceneGraphInvalidated``, to ``show_again``.
    QMetaObject::Connection invalidated_;
    /// No revision: none asked for since the presenter or the window
    /// changed. Neither a presenter's (from 1 up) nor none's (0).
    static constexpr std::uint64_t k_unasked = std::numeric_limits<std::uint64_t>::max();
    /// Presentation revision (and shape) last asked for.
    std::uint64_t shown_revision_ = k_unasked;
    QSize shown_size_;
    QRgb shown_letterbox_ = 0;
    /// A picture is being composited; one more is wanted when it comes.
    bool in_flight_ = false;
    bool again_ = false;
    /// The worker's picture, uploaded and released in updatePaintNode.
    QImage pending_;
    /// The presenter had something to show when last asked.
    bool has_picture_ = false;
};

}  // namespace seventhslide::render
