#pragma once

// At most one call per interval, and never the last one lost.

#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>
#include <utility>

namespace seventhslide::app {

/// Calls ``fn`` at most once per ``interval``. A poke after a quiet spell
/// calls it at once; pokes inside the interval fold into one call when it
/// ends, so whatever the last poke announced still goes out. GUI thread.
class Throttle {
public:
    Throttle(std::chrono::milliseconds interval, std::function<void()> fn)
        : fn_(std::move(fn))
    {
        timer_.setSingleShot(true);
        timer_.setInterval(interval);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
            if (pending_) {
                pending_ = false;
                fn_();
                timer_.start();
            }
        });
    }
    Throttle(const Throttle&) = delete;
    Throttle& operator=(const Throttle&) = delete;
    Throttle(Throttle&&) = delete;
    Throttle& operator=(Throttle&&) = delete;
    ~Throttle() = default;

    void poke()
    {
        if (timer_.isActive()) {
            pending_ = true;
            return;
        }
        fn_();
        timer_.start();
    }

private:
    std::function<void()> fn_;
    QTimer timer_;
    bool pending_ = false;
};

}  // namespace seventhslide::app
