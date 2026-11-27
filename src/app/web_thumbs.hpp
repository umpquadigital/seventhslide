#pragma once

// Thumbnails of the Browser tab's pages. Each page is drawn off screen, one at
// a time, and its picture kept once it has loaded and settled, as a JPEG in
// the library's thumbs folder (``thumbs/web/``). A thumbnail is named by what
// the page looks like - its address, size and zoom - so an edited page gets a
// new one and two pages alike share one.

#include "web_renderer.hpp"

#include <seventhslide/model/web_page.hpp>

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {

class WebThumbs : public QObject {
    Q_OBJECT

public:
    /// The library's thumbs folder; empty while no library is open.
    using ThumbsDir = std::function<std::filesystem::path()>;

    /// After a page loads, how long it is given to settle (fonts, an
    /// opening animation) before its picture is kept.
    static constexpr int k_settle_ms = 1500;
    /// A page that has not loaded by then is given up on for this run.
    static constexpr int k_give_up_ms = 20000;
    /// The longest edge a thumbnail is kept at.
    static constexpr int k_max_edge = 512;

    explicit WebThumbs(ThumbsDir dir, QObject* parent = nullptr);
    ~WebThumbs() override;

    /// How pages are drawn; without one, none get a thumbnail.
    void set_factory(WebRendererFactory factory);
    /// Tests: settle for ``ms`` rather than ``k_settle_ms``, and give up
    /// after ``ms`` rather than ``k_give_up_ms``.
    void set_settle_ms(int ms) noexcept { settle_ms_ = ms; }
    void set_give_up_ms(int ms) noexcept { give_up_ms_ = ms; }

    /// The name ``page``'s thumbnail is kept under (its look's hash).
    [[nodiscard]] static std::string key_of(const model::WebPage& page);
    /// Where ``page``'s thumbnail is kept; empty while no library is open.
    [[nodiscard]] std::filesystem::path path_of(const model::WebPage& page) const;
    /// ``page``'s thumbnail as a file URL (stamped with its age, so a newer
    /// one is loaded afresh); empty when it has none yet.
    [[nodiscard]] QString url_for(const model::WebPage& page) const;

    /// Make a thumbnail for each of ``pages`` that has none, one after
    /// another. Pages already waiting, or given up on, are not asked again.
    void want(const std::vector<model::WebPage>& pages);
    /// Keep ``picture`` as ``page``'s thumbnail.
    void store(const model::WebPage& page, const QImage& picture);
    /// Delete the thumbnails none of ``pages`` has.
    void prune(const std::vector<model::WebPage>& pages);
    /// Forget everything waiting, and stop drawing (the library closes).
    void stop();

    /// Pages waiting, the one being drawn included (tests).
    [[nodiscard]] std::size_t pending() const noexcept;

signals:
    /// A thumbnail was kept.
    void made();

private:
    void next();
    void finish(bool keep);

    ThumbsDir dir_;
    WebRendererFactory factory_;
    int settle_ms_ = k_settle_ms;
    int give_up_ms_ = k_give_up_ms;
    std::deque<model::WebPage> queue_;
    /// Keys waiting or being drawn, and keys given up on.
    std::set<std::string> asked_;
    std::set<std::string> failed_;
    std::unique_ptr<WebRenderer> renderer_;
    model::WebPage current_;
    QTimer settle_;
    QTimer give_up_;
};

} // namespace seventhslide::app
