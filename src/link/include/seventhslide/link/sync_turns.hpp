#pragma once

// How many libraries sync at once: a server with many libraries on a small
// computer (a Raspberry Pi) works through them a few at a time, rather than
// opening and applying all of them together at start and running short of
// memory. The libraries that share a computer share one; each sync takes a
// turn and gives it back.
//
// On the thread the libraries are driven from (the main one).
//
// Seam: plain C++20.

#include <deque>
#include <functional>
#include <utility>

namespace seventhslide::link {

class SyncTurns {
public:
    explicit SyncTurns(int at_once) : at_once_(at_once < 1 ? 1 : at_once) {}
    SyncTurns(const SyncTurns&) = delete;
    SyncTurns& operator=(const SyncTurns&) = delete;

    /// Run ``go`` now if a turn is free, or once one is, in the order asked;
    /// ``owner`` names who asked, for ``forget``.
    void take(const void* owner, std::function<void()> go)
    {
        if (busy_ < at_once_) {
            begin(std::move(go));
        } else {
            waiting_.emplace_back(owner, std::move(go));
        }
    }
    /// A turn taken is over: the next waiting one goes.
    void give()
    {
        if (busy_ > 0) {
            --busy_;
        }
        if (!waiting_.empty() && busy_ < at_once_) {
            auto next = std::move(waiting_.front().second);
            waiting_.pop_front();
            begin(std::move(next));
        }
    }
    /// ``owner`` is going: what it still waits for is not run.
    void forget(const void* owner)
    {
        std::erase_if(waiting_, [&](const auto& w) { return w.first == owner; });
    }
    /// The most that ran at once so far.
    [[nodiscard]] int most_at_once() const noexcept { return most_; }

private:
    void begin(std::function<void()> go)
    {
        ++busy_;
        most_ = busy_ > most_ ? busy_ : most_;
        go();
    }

    int at_once_;
    int busy_ = 0;
    int most_ = 0;
    std::deque<std::pair<const void*, std::function<void()>>> waiting_;
};

}  // namespace seventhslide::link
