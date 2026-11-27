#pragma once

// The newest picture, from the window's thread to a thread of a sink's own
// (the NDI sender, the DeckLink writer, the virtual camera's writer), which
// converts and sends it there. A newer picture replaces one not yet taken, so
// a slow transport shows the newest frame and never a backlog; a keepalive
// asks for the last again and never replaces a picture still waiting.
// Seam: Qt Gui only.

#include <QImage>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace seventhslide::render {

class LatestFrame {
public:
    /// What ``wait`` found.
    struct Next {
        /// ``stop`` was called: the thread is to end.
        bool stopped = false;
        /// Something was handed over: a picture, or a keepalive.
        bool handed = false;
        /// The picture, or null for a keepalive (or for nothing handed).
        QImage image;
    };

    /// Hand over ``image``, or a null image for a keepalive.
    void put(QImage image)
    {
        {
            const std::lock_guard lock(mu_);
            if (!image.isNull() || !handed_) {
                image_ = std::move(image);
            }
            handed_ = true;
        }
        wake_.notify_one();
    }

    /// Wait for something handed over, or for ``stop``, at most ``timeout``
    /// (none: for as long as it takes), and take it.
    [[nodiscard]] Next wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt)
    {
        std::unique_lock lock(mu_);
        const auto ready = [this] { return stop_ || handed_; };
        if (timeout) {
            wake_.wait_for(lock, *timeout, ready);
        } else {
            wake_.wait(lock, ready);
        }
        if (stop_) {
            return {.stopped = true};
        }
        return {.handed = std::exchange(handed_, false), .image = std::exchange(image_, QImage())};
    }

    /// Wake the thread to end. Under the lock, so a wait about to begin
    /// cannot miss it.
    void stop()
    {
        {
            const std::lock_guard lock(mu_);
            stop_ = true;
        }
        wake_.notify_all();
    }

    /// As new, for a thread starting again: nothing waiting, not stopped.
    void reset()
    {
        const std::lock_guard lock(mu_);
        image_ = QImage();
        handed_ = false;
        stop_ = false;
    }

private:
    std::mutex mu_;
    std::condition_variable wake_;
    QImage image_;
    bool handed_ = false;
    bool stop_ = false;
};

}  // namespace seventhslide::render
