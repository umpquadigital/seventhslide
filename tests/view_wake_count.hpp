#pragma once

// Counting how often output windows' views (PresenterView) are woken.

#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <set>
#include <utility>

namespace seventhslide::test {

/// Counts the timer events that wake the views' own timers (each a child of
/// its view), from when it is made until it goes.
class WakeCount final : public QObject {
public:
    explicit WakeCount(std::set<const QObject*> views)
        : views_(std::move(views))
    {
        QCoreApplication::instance()->installEventFilter(this);
    }
    ~WakeCount() override { QCoreApplication::instance()->removeEventFilter(this); }
    WakeCount(const WakeCount&) = delete;
    WakeCount& operator=(const WakeCount&) = delete;

    int wakes = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Timer && watched->parent() != nullptr
            && views_.contains(watched->parent())) {
            ++wakes;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::set<const QObject*> views_;
};

}  // namespace seventhslide::test
