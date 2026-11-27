#include <seventhslide/render/presenter_view.hpp>

#include <seventhslide/render/destination.hpp>
#if SEVENTHSLIDE_RENDER_RHI
#include <seventhslide/render/picture_texture.hpp>
#endif
#include <seventhslide/render/scene_presenter.hpp>

#include <QMetaObject>
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace seventhslide::render {
namespace {

/// The picture node. Owns exactly the texture it shows, replacing and
/// deleting it explicitly (on the render thread, where nodes live).
class SurfaceNode final : public QSGSimpleTextureNode {
public:
    SurfaceNode()
    {
        setOwnsTexture(false);
        setFiltering(QSGTexture::Linear);
    }
    ~SurfaceNode() override { delete texture_; }
    SurfaceNode(const SurfaceNode&) = delete;
    SurfaceNode& operator=(const SurfaceNode&) = delete;

    /// Show ``picture`` in ``window``: on a graphics API, in the texture
    /// shown now while it fits; on the software renderer, which draws only
    /// its own textures, in a new one.
    void show(QImage picture, QQuickWindow& window)
    {
#if SEVENTHSLIDE_RENDER_RHI
        if (window.rhi() != nullptr) {
            if (auto* kept = dynamic_cast<PictureTexture*>(texture_);
                kept != nullptr && kept->fits(picture)) {
                kept->set_picture(std::move(picture));
                markDirty(QSGNode::DirtyMaterial);  // the same texture, with new pixels
                return;
            }
            replace(new PictureTexture(std::move(picture)));
            return;
        }
#endif
        replace(window.createTextureFromImage(picture));
    }

private:
    void replace(QSGTexture* texture)
    {
        setTexture(texture);
        delete texture_;
        texture_ = texture;
    }

    QSGTexture* texture_ = nullptr;
};

}  // namespace

PresenterView::PresenterView(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    // The cadence every destination keeps (see OffscreenSink): transitions
    // step on the presenter's grid, so checking faster would find nothing new.
    // Started by ``tick`` when something moves.
    timer_ = new QTimer(this);
    timer_->setInterval(k_fade_step_ms);
    connect(timer_, &QTimer::timeout, this, &PresenterView::tick);
}

void PresenterView::set_presenter(ScenePresenter* presenter)
{
    if (presenter == presenter_) {
        return;
    }
    presenter_ = presenter;
    // The old presenter may have gone already: dropping its handle asks
    // nothing of it.
    watch_ = presenter_ != nullptr ? presenter_->watch([this] { wake(); }) : nullptr;
    shown_revision_ = k_unasked;
    refresh();
}

void PresenterView::set_letterbox(const QColor& color)
{
    if (color == letterbox_) {
        return;
    }
    letterbox_ = color;
    refresh();
}

void PresenterView::set_max_height(int max_height)
{
    max_height = std::max(0, max_height);
    if (max_height == max_height_) {
        return;
    }
    max_height_ = max_height;
    refresh();
}

QSize PresenterView::pixel_size() const
{
    const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize full(std::max(1, qRound(width() * dpr)), std::max(1, qRound(height() * dpr)));
    return destination_send_size(full, max_height_);
}

void PresenterView::refresh()
{
    tick();
}

void PresenterView::wake()
{
    // Ticking, the next tick finds it, on the cadence a video's pictures are
    // taken at. Asleep, a look as soon as the window's thread is free: queued,
    // so the presenter is not looked at in the middle of its change, and a
    // few changes in one pass make one look.
    if (timer_->isActive() || wake_queued_) {
        return;
    }
    wake_queued_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            wake_queued_ = false;
            tick();
        },
        Qt::QueuedConnection);
}

void PresenterView::tick()
{
    if (presenter_ != nullptr) {
        presenter_->advance();
    }
    // A running change moves on each tick; a new picture may be followed by
    // another (a video's), so the tick after it looks again. Otherwise, asleep
    // until the presenter wakes it.
    const bool moved = look();
    if (moved || (presenter_ != nullptr && presenter_->is_fading())) {
        if (!timer_->isActive()) {
            timer_->start();
        }
    } else {
        timer_->stop();
    }
}

bool PresenterView::look()
{
    const std::uint64_t revision = presenter_ != nullptr ? presenter_->revision() : 0;
    const QSize size = pixel_size();
    if (revision == shown_revision_ && size == shown_size_ && letterbox_.rgba() == shown_letterbox_) {
        return false;
    }
    if (in_flight_) {
        again_ = true;  // asked again once the picture in hand arrives
        return true;
    }
    shown_revision_ = revision;
    shown_size_ = size;
    shown_letterbox_ = letterbox_.rgba();
    if (presenter_ == nullptr || !presenter_->has_content()) {
        has_picture_ = false;
        pending_ = QImage();
        update();
        return true;
    }
    in_flight_ = true;
    presenter_->request(size, letterbox_, this, [this, revision](QImage picture) {
        take_picture(std::move(picture), revision);
    });
    return true;
}

void PresenterView::take_picture(QImage picture, std::uint64_t revision)
{
    in_flight_ = false;
    (void)revision;
    // Shared with every other destination on this output asking for the same
    // shape; released once uploaded.
    pending_ = std::move(picture);
    has_picture_ = !pending_.isNull();
    update();
    if (again_) {
        again_ = false;
        tick();
    }
}

void PresenterView::geometryChange(const QRectF& next, const QRectF& previous)
{
    QQuickItem::geometryChange(next, previous);
    if (next.size() != previous.size()) {
        refresh();
    }
}

void PresenterView::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        // Its nodes, and the picture in them, stay with the window it leaves.
        // In another, it asks for the picture again, and again whenever that
        // window's scene graph is invalidated (its graphics lost), which
        // deletes them too. Out of any window, it shows nothing until it is
        // in one.
        disconnect(invalidated_);
        invalidated_ = {};
        if (value.window != nullptr) {
            invalidated_ = connect(value.window, &QQuickWindow::sceneGraphInvalidated, this,
                                   &PresenterView::show_again, Qt::QueuedConnection);
            show_again();
        }
    } else if (change == ItemDevicePixelRatioHasChanged) {
        // The pixels it composites at follow its window's ratio: the window
        // moved to a screen of another ratio.
        refresh();
    }
}

void PresenterView::show_again()
{
    shown_revision_ = k_unasked;
    refresh();
}

QSGNode* PresenterView::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/)
{
    const QRectF rect = boundingRect();
    auto* picture = dynamic_cast<SurfaceNode*>(old);
    if (!pending_.isNull()) {
        if (picture == nullptr) {
            delete old;
            picture = new SurfaceNode();
        }
        picture->show(std::move(pending_), *window());
        picture->setRect(rect);
        pending_ = QImage();
        return picture;
    }
    if (has_picture_ && picture != nullptr) {
        // Asked to repaint with nothing new (the scene graph re-synced): keep
        // what is on screen.
        picture->setRect(rect);
        return picture;
    }
    // Nothing presented yet: the letterbox colour.
    auto* fill = dynamic_cast<QSGSimpleRectNode*>(old);
    if (fill == nullptr) {
        delete old;
        fill = new QSGSimpleRectNode();
    }
    fill->setRect(rect);
    fill->setColor(letterbox_);
    return fill;
}

}  // namespace seventhslide::render
