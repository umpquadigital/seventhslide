#pragma once

// Least-recently-used cache of decoded images for QQuickImageProvider
// subclasses. Providers are called from QML loader threads, hence the lock.

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include <algorithm>
#include <list>

namespace seventhslide::app {

class ImageLruCache {
public:
    explicit ImageLruCache(int capacity) : capacity_(std::max(8, capacity)) {}

    /// The cached image for ``key``, or a null image. A hit becomes most recent.
    [[nodiscard]] QImage get(const QString& key)
    {
        QMutexLocker lock(&mutex_);
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return {};
        }
        lru_.splice(lru_.begin(), lru_, it.value());
        return it.value()->image;
    }

    /// Store ``image`` under ``key`` (null images are ignored), evicting the
    /// least recently used entries beyond capacity.
    void put(const QString& key, const QImage& image)
    {
        if (image.isNull()) {
            return;
        }
        QMutexLocker lock(&mutex_);
        put_locked(key, image);
    }

    /// Bumped by every ``clear``: an image made before a clear is stale.
    [[nodiscard]] int generation()
    {
        QMutexLocker lock(&mutex_);
        return generation_;
    }

    /// ``put``, unless the cache was cleared since ``generation``. A render
    /// started before a clear (another library opened, a theme changed) must
    /// not come back under a key that now means something else.
    void put_if_current(const QString& key, const QImage& image, int generation)
    {
        if (image.isNull()) {
            return;
        }
        QMutexLocker lock(&mutex_);
        if (generation != generation_) {
            return;
        }
        put_locked(key, image);
    }

    void clear()
    {
        QMutexLocker lock(&mutex_);
        index_.clear();
        lru_.clear();
        ++generation_;
    }

private:
    void put_locked(const QString& key, const QImage& image)
    {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            it.value()->image = image;
            lru_.splice(lru_.begin(), lru_, it.value());
            return;
        }
        lru_.push_front(Entry{key, image});
        index_.insert(key, lru_.begin());
        while (static_cast<int>(lru_.size()) > capacity_) {
            index_.remove(lru_.back().key);
            lru_.pop_back();
        }
    }

    struct Entry {
        QString key;
        QImage image;
    };

    int capacity_;
    int generation_ = 0;
    QMutex mutex_;
    std::list<Entry> lru_;
    QHash<QString, std::list<Entry>::iterator> index_;
};

} // namespace seventhslide::app
