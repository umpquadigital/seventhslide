#pragma once

// ThumbCache - revision-keyed LRU over rendered thumbnails.
//
// The operator UI gets its imagery from QQuickImageProviders, which are QML-only
// URLs. The remote mirrors them over HTTP, and this bounds the cost: rendering a
// SceneFrame is not free, and a phone scrolling a media grid would otherwise ask
// for dozens of renders a second.
//
// Entries are keyed on the revision the client asked for, so a bumped revision
// misses naturally and stale entries age out of the LRU rather than needing
// explicit invalidation.

#include <seventhslide/remote/remote_bridge.hpp>

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace seventhslide::remote {

/// The thumbnail width served for a requested ``width``: the nearest of a few
/// fixed widths at or above it. Bounding widths keeps cache keys finite and
/// stops a caller asking for a 4K render of every slide.
[[nodiscard]] int clamp_width(int width) noexcept;

class ThumbCache {
public:
    /// Roughly 24 MB of encoded thumbnails, or 512 entries, whichever binds first.
    static constexpr std::size_t k_max_bytes = 24u * 1024u * 1024u;
    static constexpr std::size_t k_max_entries = 512;

    [[nodiscard]] static std::string key_for(const ThumbRequest& r, const std::string& revision);

    [[nodiscard]] std::optional<ThumbResult> get(const std::string& key);
    void put(const std::string& key, const ThumbResult& value);
    void clear();

    [[nodiscard]] std::size_t entry_count() const noexcept { return map_.size(); }
    [[nodiscard]] std::size_t byte_count() const noexcept { return bytes_; }

private:
    struct Entry {
        std::string key;
        ThumbResult value;
    };

    void evict_if_needed();

    std::list<Entry> lru_;  ///< front = most recently used
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
    std::size_t bytes_ = 0;
};

} // namespace seventhslide::remote
