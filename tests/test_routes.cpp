// How good a way each connection is, and the upload speed cap (routes.hpp,
// sync_settings.hpp): plain arithmetic and the settings file, no connection.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/link/routes.hpp>
#include <seventhslide/link/sync_settings.hpp>

#include "test_temp_dir.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

using Catch::Approx;
using seventhslide::link::Clock;
using seventhslide::link::Route;
using seventhslide::link::Throughput;
using seventhslide::link::UploadCap;
using seventhslide::link::clearly_better;

TEST_CASE("A connection's speed is measured only while media is due over it", "[link][routes]")
{
    Throughput t;
    CHECK_FALSE(t.per_second());
    // Idle, nothing is learnt: a connection with nothing to fetch is not slow.
    t.sample(0, 60, false);
    CHECK_FALSE(t.per_second());
    // A second is too little to go by, at an ordinary speed.
    t.sample(1'000'000, 1, true);
    CHECK_FALSE(t.per_second());
    t.sample(1'000'000, 1, true);
    t.sample(1'000'000, 1, true);
    REQUIRE(t.per_second());
    CHECK(*t.per_second() == Approx(1'000'000));
    // Idle again: what it was measured at stands.
    t.sample(0, 600, false);
    CHECK(*t.per_second() == Approx(1'000'000));
    // Due and nothing came: that is slow.
    t.sample(0, 3, true);
    CHECK(*t.per_second() == Approx(500'000));
    // Over the last ten seconds of fetching, not all of them.
    for (int i = 0; i < 20; ++i) {
        t.sample(4'000'000, 1, true);
    }
    CHECK(*t.per_second() == Approx(4'000'000));

    // A fast connection may be done in less than the seconds it takes: enough
    // bytes are as good.
    Throughput fast;
    fast.sample(32 * 1024 * 1024, 0.5, true);
    REQUIRE(fast.per_second());
    CHECK(*fast.per_second() == Approx(64.0 * 1024 * 1024));
}

TEST_CASE("Media goes to the clearly better of two routes", "[link][routes]")
{
    const Route near_slow{true, 1'000};
    const Route far_fast{false, 100'000'000};
    const Route far_unmeasured{false, std::nullopt};
    // On this network, however fast the other: it uses no uplink.
    CHECK(clearly_better(near_slow, far_fast));
    CHECK_FALSE(clearly_better(far_fast, near_slow));
    CHECK(clearly_better(Route{true, std::nullopt}, far_unmeasured));
    // Else twice as fast, both measured.
    CHECK(clearly_better(Route{false, 2'000'000}, Route{false, 1'000'000}));
    CHECK_FALSE(clearly_better(Route{false, 1'500'000}, Route{false, 1'000'000}));
    CHECK_FALSE(clearly_better(Route{false, 1'000'000}, Route{false, 2'000'000}));
    CHECK_FALSE(clearly_better(far_fast, far_unmeasured));
    CHECK_FALSE(clearly_better(far_unmeasured, far_fast));
    // Nothing is better than itself.
    CHECK_FALSE(clearly_better(far_fast, far_fast));
}

TEST_CASE("The upload speed cap holds what goes to its rate, in short frames", "[link][routes]")
{
    const auto start = Clock::now();
    UploadCap cap;
    // None: everything goes, in the largest frames.
    CHECK(cap.take(100'000'000, start));
    CHECK(cap.wait(start).count() == 0);
    CHECK(cap.frame(256 * 1024) == 256 * 1024);

    const std::int64_t rate = 1'000'000;  // 8 Mbit/s
    cap.set_rate(rate, start);
    const auto frame = cap.frame(256 * 1024);
    // An eighth of a second's worth, to a power of two: short bursts.
    CHECK(frame == 64 * 1024);
    std::int64_t sent = 0;
    auto now = start;
    while (now - start < std::chrono::seconds(10)) {
        if (cap.take(frame, now)) {
            sent += frame;
        } else {
            CHECK(cap.wait(now).count() > 0);
        }
        now += std::chrono::milliseconds(1);
    }
    // Ten seconds' worth, give or take a frame and what gathers in a moment.
    CHECK(sent >= 10 * rate - frame);
    CHECK(sent <= 10 * rate + rate / 4 + frame);

    // Idle a long while, it does not make up for it all at once.
    now += std::chrono::minutes(1);
    std::int64_t burst = 0;
    while (cap.take(frame, now)) {
        burst += frame;
    }
    CHECK(burst <= rate / 4 + frame);

    // Slow and fast: the smallest frames, and frames no larger than asked.
    cap.set_rate(12'500, now);
    CHECK(cap.frame(256 * 1024) == 16 * 1024);
    cap.set_rate(100'000'000, now);
    CHECK(cap.frame(256 * 1024) == 256 * 1024);
    // Lifted, nothing waits.
    cap.set_rate(0, now);
    CHECK(cap.take(1, now));
}

TEST_CASE("An upload speed cap is typed in megabits a second", "[link][routes]")
{
    using seventhslide::link::read_upload_cap;
    using seventhslide::link::upload_cap_text;
    CHECK(read_upload_cap("5") == 5'000);
    CHECK(read_upload_cap(" 0.5 ") == 500);
    CHECK(read_upload_cap("1.25") == 1'250);
    CHECK(read_upload_cap("1,5") == 1'500);
    CHECK(read_upload_cap("10000") == 10'000'000);
    // None.
    CHECK(read_upload_cap("") == 0);
    CHECK(read_upload_cap("none") == 0);
    CHECK(read_upload_cap("0") == 0);
    CHECK(read_upload_cap("0.0") == 0);
    CHECK(read_upload_cap("2.500") == 2'500);
    // Not one, or out of range.
    CHECK_FALSE(read_upload_cap("fast"));
    CHECK_FALSE(read_upload_cap("5 Mbit"));
    CHECK_FALSE(read_upload_cap("."));
    CHECK_FALSE(read_upload_cap("-1"));
    CHECK_FALSE(read_upload_cap("0.05"));
    CHECK_FALSE(read_upload_cap("1.0005"));
    CHECK_FALSE(read_upload_cap("10001"));
    CHECK_FALSE(read_upload_cap("99999999999999999999"));

    CHECK(upload_cap_text(5'000) == "5");
    CHECK(upload_cap_text(500) == "0.5");
    CHECK(upload_cap_text(1'250) == "1.25");
    CHECK(upload_cap_text(1'005) == "1.005");
    CHECK(upload_cap_text(0).empty());
    CHECK(seventhslide::link::upload_cap_bytes(8'000) == 1'000'000);
}

TEST_CASE("An upload speed cap out of range in the settings file is none", "[link][routes]")
{
    seventhslide::test::TempDir data;
    const auto write = [&](const std::string& text) {
        std::ofstream(data.path / "sync_settings.json") << text;
    };
    using seventhslide::link::SyncSettings;
    write(R"({"format": 1, "upload_cap_kbit": 5000})");
    CHECK(SyncSettings::load(data.path).upload_cap_kbit == 5'000);
    // A few bits a second would stop syncing.
    write(R"({"format": 1, "upload_cap_kbit": 5})");
    CHECK(SyncSettings::load(data.path).upload_cap_kbit == 0);
    write(R"({"format": 1, "upload_cap_kbit": -1})");
    CHECK(SyncSettings::load(data.path).upload_cap_kbit == 0);
    write(R"({"format": 1, "upload_cap_kbit": "fast"})");
    CHECK(SyncSettings::load(data.path).upload_cap_kbit == 0);

    SyncSettings saved;
    saved.upload_cap_kbit = 2'500;
    REQUIRE(saved.save(data.path));
    CHECK(SyncSettings::load(data.path).upload_cap_kbit == 2'500);
}
