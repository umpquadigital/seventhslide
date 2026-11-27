#include <catch2/catch_test_macros.hpp>

#include <seventhslide/remote/remote_thumbs.hpp>

using seventhslide::remote::clamp_width;
using seventhslide::remote::ThumbCache;
using seventhslide::remote::ThumbRequest;
using seventhslide::remote::ThumbResult;

namespace {

ThumbResult make(std::size_t bytes)
{
    ThumbResult r;
    r.ok = true;
    r.mime = "image/png";
    r.bytes.assign(bytes, 'x');
    return r;
}

} // namespace

TEST_CASE("Thumbnails: only allowlisted widths are served", "[remote][R5]")
{
    // A caller asking for something else is rounded, never honoured verbatim -
    // this is what stops one request costing a 4K render.
    REQUIRE(clamp_width(1) == 160);
    REQUIRE(clamp_width(200) == 320);
    REQUIRE(clamp_width(640) == 640);
    REQUIRE(clamp_width(3840) == 640);
}

TEST_CASE("Thumbnails: the revision is the cache key", "[remote][R5]")
{
    ThumbRequest r;
    r.kind = "preview";
    r.name = "Main";
    r.slide_index = 2;
    r.width = 320;

    ThumbCache cache;
    cache.put(ThumbCache::key_for(r, "7"), make(64));

    REQUIRE(cache.get(ThumbCache::key_for(r, "7")).has_value());
    // A bumped revision misses naturally; no explicit invalidation needed.
    REQUIRE_FALSE(cache.get(ThumbCache::key_for(r, "8")).has_value());

    ThumbRequest other = r;
    other.slide_index = 3;
    REQUIRE_FALSE(cache.get(ThumbCache::key_for(other, "7")).has_value());

    // The same slide of another stack's deck is another picture.
    ThumbRequest lobby = r;
    lobby.stack = "lobby";
    REQUIRE_FALSE(cache.get(ThumbCache::key_for(lobby, "7")).has_value());
}

TEST_CASE("Thumbnails: the cache evicts by bytes and keeps recent entries", "[remote][R5]")
{
    ThumbCache cache;
    const std::size_t chunk = ThumbCache::k_max_bytes / 4;

    ThumbRequest r;
    r.kind = "preview";
    r.name = "Main";
    r.width = 320;

    for (int i = 0; i < 4; ++i) {
        r.slide_index = i;
        cache.put(ThumbCache::key_for(r, "1"), make(chunk));
    }
    REQUIRE(cache.byte_count() <= ThumbCache::k_max_bytes);

    // Touch the oldest so it is no longer the eviction victim.
    r.slide_index = 0;
    REQUIRE(cache.get(ThumbCache::key_for(r, "1")).has_value());

    r.slide_index = 99;
    cache.put(ThumbCache::key_for(r, "1"), make(chunk));

    r.slide_index = 0;
    REQUIRE(cache.get(ThumbCache::key_for(r, "1")).has_value());
    REQUIRE(cache.byte_count() <= ThumbCache::k_max_bytes);
}

TEST_CASE("Thumbnails: failed renders are never cached", "[remote][R5]")
{
    ThumbCache cache;
    ThumbRequest r;
    r.kind = "preview";
    r.name = "Missing";

    cache.put(ThumbCache::key_for(r, "1"), ThumbResult{});
    REQUIRE(cache.entry_count() == 0);
    REQUIRE_FALSE(cache.get(ThumbCache::key_for(r, "1")).has_value());
}
