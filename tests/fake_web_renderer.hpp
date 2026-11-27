#pragma once

// A web-page renderer for the tests: it "draws" a page as one solid colour at
// the page's size, at once, and counts what it is asked to do. The real one
// (Qt WebEngine) needs a GPU and its set-up before the application exists.

#include "web_renderer.hpp"

#include <seventhslide/audio/stream_source.hpp>

#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QPointer>
#include <QQuickWindow>
#include <QSinglePointEvent>

#include <memory>
#include <vector>

namespace seventhslide::test {

class FakeWebRenderer final : public app::WebRenderer {
public:
    void load(const QUrl& url, QSize size, double zoom) override
    {
        url_ = url;
        size_ = size;
        zoom_ = zoom;
        if (draw_on_load_) {
            draw(QColor(Qt::red));
            emit loaded();
        }
    }
    void reload() override
    {
        ++reloads_;
        draw(QColor(Qt::green));
    }
    [[nodiscard]] QUrl url() const override { return history_.empty() ? url_ : history_.back(); }
    [[nodiscard]] bool can_go_back() const override { return !history_.empty(); }
    void go_back() override
    {
        if (!history_.empty()) {
            history_.pop_back();
            emit navigated();
        }
    }
    bool navigate(const QUrl& url) override
    {
        if (!started_) {
            return false;
        }
        navigations_.push_back(url);
        follow(url);
        return true;
    }
    /// A link followed to ``to``.
    void follow(const QUrl& to)
    {
        history_.push_back(to);
        emit navigated();
    }
    [[nodiscard]] QImage frame() const override { return frame_; }
    [[nodiscard]] QString problem() const override { return problem_; }
    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }
    bool send_input(QEvent* event) override
    {
        Input in{event->type(), {}, 0};
        if (const auto* pointer = dynamic_cast<const QSinglePointEvent*>(event)) {
            in.at = pointer->position();
        }
        if (const auto* key = dynamic_cast<const QKeyEvent*>(event)) {
            in.key = key->key();
        }
        inputs_.push_back(in);
        if (page_window_ != nullptr) {
            QCoreApplication::sendEvent(page_window_, event);
        }
        return true;
    }

    void draw(const QColor& colour)
    {
        QImage next(size_, QImage::Format_ARGB32_Premultiplied);
        next.fill(colour);
        frame_ = std::move(next);
        emit frameChanged();
    }
    void fail(const QString& why)
    {
        problem_ = why;
        emit problemChanged();
    }

    /// What ``send_input`` was handed.
    struct Input {
        QEvent::Type type;
        QPointF at;
        int key;
    };
    std::vector<Input> inputs_;
    /// Set: input is also delivered to this window, as the real renderer
    /// delivers it to the page's own (off-screen) one.
    QPointer<QQuickWindow> page_window_;

    /// False: the page is still loading until ``draw``.
    bool draw_on_load_ = true;
    /// False: it could not start (the real one's ``start`` failing), so it
    /// goes nowhere; ``fail`` says why.
    bool started_ = true;
    /// What the page "plays": push to it as a page's script would.
    audio::StreamSource sound_;
    QUrl url_;
    /// Pages a link took it to, after ``url_``.
    std::vector<QUrl> history_;
    /// What ``navigate`` was asked to go to.
    std::vector<QUrl> navigations_;
    QSize size_;
    double zoom_ = 0.0;
    int reloads_ = 0;
    QImage frame_;
    QString problem_;
};

/// Makes FakeWebRenderers and keeps a weak pointer to each, so a test can
/// see which are running.
struct FakeWebRenderers {
    std::vector<QPointer<FakeWebRenderer>> made;
    /// False: each page stays loading until the test draws it.
    bool draw_on_load = true;

    [[nodiscard]] app::WebRendererFactory factory()
    {
        return [this] {
            auto r = std::make_unique<FakeWebRenderer>();
            r->draw_on_load_ = draw_on_load;
            made.emplace_back(r.get());
            return std::unique_ptr<app::WebRenderer>(std::move(r));
        };
    }
    [[nodiscard]] int running() const
    {
        int n = 0;
        for (const auto& r : made) {
            n += r ? 1 : 0;
        }
        return n;
    }
    [[nodiscard]] FakeWebRenderer* last() const { return made.empty() ? nullptr : made.back().data(); }
};

} // namespace seventhslide::test
