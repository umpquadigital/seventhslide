#include <seventhslide/render/output_window.hpp>

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/render/css_color.hpp>

#include <seventhslide/render/device_catalog.hpp>
#include <seventhslide/render/presenter_view.hpp>
#include <seventhslide/render/scene_presenter.hpp>

#if defined(Q_OS_MACOS)
#include "output_window_mac.hpp"
#endif

#include <QCloseEvent>
#include <QExposeEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>

#include <algorithm>

namespace seventhslide::render {

OutputWindow::OutputWindow(QString output_id, QString output_name, QScreen* screen,
                           QString destination_id)
    : QQuickWindow()
    , output_id_(std::move(output_id))
    , output_name_(std::move(output_name))
    , destination_id_(std::move(destination_id))
{
    setObjectName(QStringLiteral("OutputWindow:%1").arg(destination_id_));
    setFlags(Qt::Window | Qt::FramelessWindowHint);
    setColor(Qt::black);
    setPersistentSceneGraph(true);

    view_ = new PresenterView(contentItem());

    if (screen) {
        screen_key_ = DeviceCatalog::screen_key(screen);
        setScreen(screen);
        setGeometry(screen->geometry());
    } else if (auto* primary = QGuiApplication::primaryScreen()) {
        screen_key_ = DeviceCatalog::screen_key(primary);
        setScreen(primary);
        setGeometry(primary->geometry());
    }

    apply_letterbox_color();
    update_title();
    sync_view_geometry();
}

OutputWindow::OutputWindow(QString output_id, QString output_name, QRect geometry,
                           QString destination_id)
    : QQuickWindow()
    , output_id_(std::move(output_id))
    , output_name_(std::move(output_name))
    , destination_id_(std::move(destination_id))
    , desktop_(true)
{
    setObjectName(QStringLiteral("OutputWindow:%1").arg(destination_id_));
    // The system's frame: a title bar to move it by and edges to size it.
    setFlags(Qt::Window);
    setColor(Qt::black);
    setPersistentSceneGraph(true);
    setMinimumSize(QSize(160, 90));

    view_ = new PresenterView(contentItem());
    setGeometry(geometry);

    // Where the operator leaves it is kept with its destination.
    const auto moved = [this] { emit placeChanged(destination_id_, this->geometry()); };
    connect(this, &QWindow::xChanged, this, moved);
    connect(this, &QWindow::yChanged, this, moved);
    connect(this, &QWindow::widthChanged, this, moved);
    connect(this, &QWindow::heightChanged, this, moved);

    apply_letterbox_color();
    update_title();
    sync_view_geometry();
}

OutputWindow::~OutputWindow() = default;

void OutputWindow::set_output_name(QString name)
{
    if (name == output_name_) {
        return;
    }
    output_name_ = std::move(name);
    update_title();
}

void OutputWindow::move_to_screen(QScreen* screen)
{
    if (!screen) {
        return;
    }
    screen_key_ = DeviceCatalog::screen_key(screen);

    if (visibility() == QWindow::FullScreen) {
        setVisibility(QWindow::Windowed);
    }

    setScreen(screen);
    setGeometry(screen->geometry());
    show_full_screen();
    sync_view_geometry();
}

void OutputWindow::show_full_screen()
{
#if defined(Q_OS_MACOS)
    if (QScreen* s = screen()) {
        setGeometry(s->geometry());
    }
    // Shown without becoming the key window: Cocoa's show() would make it
    // key, taking the arrows, B and F from the operator window, and a stray
    // Esc meant for the operator would close this output. Clicking the
    // output still makes it key.
    setProperty("_q_showWithoutActivating", true);
    show();
    apply_stacking();
#else
    showFullScreen();
#endif
}

void OutputWindow::show_on_desktop()
{
    // Opening it (Live, or switching it on) must not take the arrows and the
    // shortcuts from the operator's window. Cocoa, Windows and X11 honour
    // this; Wayland has no such request, and its compositor decides.
    setProperty("_q_showWithoutActivating", true);
    showNormal();
}

void OutputWindow::apply_stacking()
{
    if (desktop_) {
        return;
    }
#if defined(Q_OS_MACOS)
    // Above everything on an audience screen. On the operator's own screen
    // (no other to put it on) an ordinary window, which Command+` and
    // Command+Tab get past, so the operator is never shut out.
    QScreen* own = DeviceCatalog::operator_screen();
    if (own != nullptr && screen() == own) {
        keep_as_window(*this);
    } else {
        keep_above_menu_bar(*this);
    }
#endif
}

void OutputWindow::set_presenter(ScenePresenter* presenter)
{
    view_->set_presenter(presenter);
    presentation_changed();
}

void OutputWindow::set_max_height(int max_height)
{
    view_->set_max_height(max_height);
}

ScenePresenter* OutputWindow::presenter() const noexcept
{
    return view_->presenter();
}

void OutputWindow::presentation_changed()
{
    apply_letterbox_color();
    update_title();
    view_->refresh();
}

const scene::SceneFrame* OutputWindow::current_frame() const noexcept
{
    const ScenePresenter* presenter = view_->presenter();
    return presenter != nullptr ? presenter->current_frame() : nullptr;
}

QImage OutputWindow::snapshot()
{
    const QSize px = view_->pixel_size();
    ScenePresenter* presenter = view_->presenter();
    if (presenter != nullptr && presenter->has_content()) {
        return presenter->surface(px, letterbox_);
    }
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(letterbox_);
    return img;
}

void OutputWindow::apply_letterbox_color()
{
    // A colour (or video) background carries on into the bars, so a canvas
    // narrower than the screen reads as one picture.
    QColor color(Qt::black);
    const scene::SceneFrame* frame = current_frame();
    if (frame != nullptr
        && (frame->background.type == layout::BackgroundType::Color
            || frame->background.type == layout::BackgroundType::Video)
        && !frame->background.color.empty()) {
        color = css_color_or(frame->background.color, color);
    }
    letterbox_ = color;
    setColor(letterbox_);
    view_->set_letterbox(letterbox_);
}

void OutputWindow::update_title()
{
    QString title = QStringLiteral("SeventhSlide - %1").arg(output_name_);
    if (const scene::SceneFrame* frame = current_frame()) {
        title += QStringLiteral(" [%1]").arg(
            QString::fromUtf8(scene::to_string(frame->mode)));
    }
    setTitle(title);
}

QSize OutputWindow::logical_size() const
{
    if (width() > 0 && height() > 0) {
        return size();
    }
    if (QScreen* s = screen()) {
        return s->geometry().size();
    }
    return {geom::k_reference_canvas.width, geom::k_reference_canvas.height};
}

void OutputWindow::sync_view_geometry()
{
    const QSize logical = logical_size();
    view_->setPosition(QPointF(0, 0));
    view_->setSize(QSizeF(std::max(1, logical.width()), std::max(1, logical.height())));
}

void OutputWindow::resizeEvent(QResizeEvent* event)
{
    QQuickWindow::resizeEvent(event);
    sync_view_geometry();
}

void OutputWindow::exposeEvent(QExposeEvent* event)
{
    QQuickWindow::exposeEvent(event);
    if (isExposed()) {
        sync_view_geometry();
        view_->refresh();
    }
}

void OutputWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        emit closeRequested(destination_id_);
        return;
    }
    QQuickWindow::keyPressEvent(event);
}

void OutputWindow::closeEvent(QCloseEvent* event)
{
    if (may_close_) {
        QQuickWindow::closeEvent(event);
        return;
    }
    // Not the host's: the host closes it, keeping its destination on. Later,
    // not inside the system's close.
    event->ignore();
    QMetaObject::invokeMethod(
        this, [this] { emit closeRequested(destination_id_); }, Qt::QueuedConnection);
}

}  // namespace seventhslide::render
