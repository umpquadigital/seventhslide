#pragma once

// Run work on the GUI thread from a QQuickImageProvider worker.
//
// Qt calls requestImage() on its pixmap-reader thread whenever an Image has
// asynchronous: true. Session, the outputs and the Freeform editor are only
// ever changed on the GUI thread, so a provider may read them only there.

#include "entry_error.hpp"

#include <QMetaObject>
#include <QObject>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace seventhslide::app {

/// Set once the application starts quitting. Workers still waiting in
/// ``call_on_gui_thread`` give up, so engine teardown (which joins the
/// pixmap-reader thread) cannot deadlock against a queued call.
inline std::atomic<bool> g_gui_calls_closed{false};

/// Run ``fn`` on ``context``'s thread and return its result.
///
/// On that thread already, ``fn`` runs inline. From any other thread the call
/// is queued and this blocks until it has run. After ``g_gui_calls_closed``
/// is set it returns a default-constructed result instead of waiting.
///
/// A queued ``fn`` that throws is logged and yields a default-constructed
/// result: the exception would otherwise escape from Qt's event dispatch and
/// end the process, and the waiting worker would never be released. (Inline,
/// an exception reaches the caller as usual.)
template <typename Fn>
[[nodiscard]] std::invoke_result_t<Fn> call_on_gui_thread(QObject* context, Fn fn)
{
    using Result = std::invoke_result_t<Fn>;
    if (context == nullptr) {
        return Result{};
    }
    if (QThread::currentThread() == context->thread()) {
        return fn();
    }
    struct Pending {
        QSemaphore done;
        Result result{};
    };
    auto pending = std::make_shared<Pending>();
    QMetaObject::invokeMethod(
        context,
        [pending, fn]() mutable {
            try {
                pending->result = fn();
            } catch (const std::exception& ex) {
                (void)report_entry_error("call_on_gui_thread", ex);
            }
            pending->done.release();
        },
        Qt::QueuedConnection);
    while (!pending->done.tryAcquire(1, 50)) {
        if (g_gui_calls_closed.load()) {
            return Result{};
        }
    }
    return std::move(pending->result);
}

}  // namespace seventhslide::app
