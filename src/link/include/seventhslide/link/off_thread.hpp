#pragma once

// Work the link does off its thread: a file read through, a change file
// checked at the gate. ``run_off_thread`` runs ``work()`` on a pool, then
// ``done`` with what it returned, back on ``context``'s thread; not at all
// once ``context`` is gone, so ``done`` may use whatever ``context`` goes
// with.
//
// Seam: Qt Core (QThreadPool, QPromise, QFutureWatcher).

#include <QFutureWatcher>
#include <QObject>
#include <QPromise>
#include <QThreadPool>

#include <memory>
#include <type_traits>
#include <utility>

namespace seventhslide::link {

template <typename Work, typename Done>
void run_off_thread(QObject* context, QThreadPool& pool, Work work, Done done)
{
    using Result = std::invoke_result_t<Work&>;
    auto promise = std::make_shared<QPromise<Result>>();
    // The watcher is the context's: gone with it, so is ``done``.
    auto* watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcherBase::finished, context,
                     [watcher, done = std::move(done)]() mutable {
                         watcher->deleteLater();
                         auto future = watcher->future();
                         if (future.resultCount() > 0) {
                             done(future.takeResult());
                         }
                     });
    watcher->setFuture(promise->future());
    promise->start();
    pool.start([promise, work = std::move(work)]() mutable {
        promise->addResult(work());
        promise->finish();
    });
}

}  // namespace seventhslide::link
