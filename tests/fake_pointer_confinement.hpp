#pragma once

// Pointer holds for the tests: no windowing system is asked, and each hold
// writes where it holds the pointer to a log the test keeps. The real ones
// (pointer_confinement_native.hpp) need a desktop session.

#include "pointer_confinement.hpp"

#include <QPointer>
#include <QRectF>
#include <QWindow>

#include <cstddef>
#include <deque>
#include <memory>

namespace seventhslide::test {

class FakePointerHold;

/// What the holds were asked to do, in the order they were made. Kept by the
/// test, so it outlives the session that holds them.
struct PointerHoldLog {
    struct Entry {
        QPointer<QWindow> window;
        /// Where it holds the pointer, as last told.
        QRectF area;
        int moves = 0;
        /// Not yet let go.
        bool held = true;
    };
    std::deque<Entry> holds;
    /// The hold made last, while it lives.
    QPointer<FakePointerHold> live;
};

class FakePointerHold final : public app::PointerHold {
public:
    FakePointerHold(PointerHoldLog& log, std::size_t index)
        : log_(log)
        , index_(index)
    {
    }
    ~FakePointerHold() override { let_go(); }

    void move(const QRectF& area) override
    {
        auto& entry = log_.holds[index_];
        if (entry.held) {
            entry.area = area;
            ++entry.moves;
        }
    }

    /// The system lets the pointer go (the person broke the hold).
    void system_lets_go() { end(); }

protected:
    void let_go() override { log_.holds[index_].held = false; }

private:
    PointerHoldLog& log_;
    std::size_t index_;
};

class FakePointerConfinement final : public app::PointerConfinement {
public:
    explicit FakePointerConfinement(PointerHoldLog& log)
        : log_(log)
    {
    }

protected:
    std::unique_ptr<app::PointerHold> make_hold(QWindow& window, const QRectF& area) override
    {
        log_.holds.push_back({&window, area, 0, true});
        auto hold = std::make_unique<FakePointerHold>(log_, log_.holds.size() - 1);
        log_.live = hold.get();
        return hold;
    }

private:
    PointerHoldLog& log_;
};

} // namespace seventhslide::test
