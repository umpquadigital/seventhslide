#include <seventhslide/remote/remote_thumbs.hpp>

#include <array>
#include <algorithm>

namespace seventhslide::remote {

namespace {
constexpr std::array<int, 3> k_widths{160, 320, 640};
}

int clamp_width(int width) noexcept
{
    for (const int w : k_widths) {
        if (width <= w) {
            return w;
        }
    }
    return k_widths.back();
}

std::string ThumbCache::key_for(const ThumbRequest& r, const std::string& revision)
{
    std::string key;
    key.reserve(r.kind.size() + r.name.size() + r.sub.size() + r.stack.size() + r.side.size()
                + revision.size() + 32);
    key += r.kind;
    key += '\x1f';
    key += r.name;
    key += '\x1f';
    key += r.sub;
    key += '\x1f';
    key += r.stack;
    key += '\x1f';
    key += r.side;
    key += '\x1f';
    key += std::to_string(r.id);
    key += '\x1f';
    key += std::to_string(r.slide_index);
    key += '\x1f';
    key += std::to_string(r.width);
    key += '\x1f';
    key += revision;
    return key;
}

std::optional<ThumbResult> ThumbCache::get(const std::string& key)
{
    const auto it = map_.find(key);
    if (it == map_.end()) {
        return std::nullopt;
    }
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->value;
}

void ThumbCache::put(const std::string& key, const ThumbResult& value)
{
    if (!value.ok) {
        return;
    }
    if (const auto it = map_.find(key); it != map_.end()) {
        bytes_ -= it->second->value.bytes.size();
        lru_.erase(it->second);
        map_.erase(it);
    }
    lru_.push_front(Entry{key, value});
    map_[key] = lru_.begin();
    bytes_ += value.bytes.size();
    evict_if_needed();
}

void ThumbCache::evict_if_needed()
{
    while (!lru_.empty() && (bytes_ > k_max_bytes || map_.size() > k_max_entries)) {
        const Entry& victim = lru_.back();
        bytes_ -= victim.value.bytes.size();
        map_.erase(victim.key);
        lru_.pop_back();
    }
}

void ThumbCache::clear()
{
    lru_.clear();
    map_.clear();
    bytes_ = 0;
}

} // namespace seventhslide::remote
