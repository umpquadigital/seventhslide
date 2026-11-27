#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/media/audio_levels.hpp>

#include <cmath>
#include <limits>

namespace media = seventhslide::media;
using Catch::Approx;

namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("Levels convert between decibels and gain", "[media][audio]")
{
    REQUIRE(media::db_to_gain(0.0) == Approx(1.0));
    REQUIRE(media::db_to_gain(-6.0) == Approx(0.501187).epsilon(1e-5));
    REQUIRE(media::db_to_gain(-20.0) == Approx(0.1));
    REQUIRE(media::db_to_gain(-k_inf) == 0.0);

    REQUIRE(media::gain_to_db(1.0) == Approx(0.0).margin(1e-12));
    REQUIRE(media::gain_to_db(0.1) == Approx(-20.0));
    REQUIRE(media::gain_to_db(0.0) == -k_inf);
    REQUIRE(media::gain_to_db(-1.0) == -k_inf);

    for (const double db : {-90.0, -40.0, -12.5, -3.0, 0.0}) {
        REQUIRE(media::gain_to_db(media::db_to_gain(db)) == Approx(db).margin(1e-9));
    }
}

TEST_CASE("A level is at most 0 dB, and silence below the fader's floor", "[media][audio]")
{
    REQUIRE(media::normalize_gain_db(-12.0) == -12.0);
    REQUIRE(media::normalize_gain_db(6.0) == 0.0);
    REQUIRE(media::normalize_gain_db(std::nan("")) == 0.0);
    REQUIRE(media::normalize_gain_db(media::k_fader_floor_db) == -k_inf);
    REQUIRE(media::normalize_gain_db(-200.0) == -k_inf);
    REQUIRE(media::normalize_gain_db(-k_inf) == -k_inf);
}

TEST_CASE("The fader's taper is OBS's", "[media][audio]")
{
    // The ends: silence at the bottom, 0 dB at the top, nothing past them.
    REQUIRE(media::fader_db(0.0) == -k_inf);
    REQUIRE(media::fader_db(-0.5) == -k_inf);
    REQUIRE(media::fader_db(1.0) == 0.0);
    REQUIRE(media::fader_db(1.5) == 0.0);
    REQUIRE(media::fader_position(-k_inf) == 0.0);
    REQUIRE(media::fader_position(0.0) == 1.0);
    REQUIRE(media::fader_position(3.0) == 1.0);

    // Values OBS's OBS_FADER_LOG gives: half travel is about -18.7 dB, and
    // most of the travel is spent on the levels people use.
    REQUIRE(media::fader_db(0.5) == Approx(-18.739).margin(0.001));
    REQUIRE(media::fader_position(-6.0) == Approx(0.7553).margin(1e-4));
    REQUIRE(media::fader_position(-30.0) == Approx(0.3676).margin(1e-4));

    // Up the fader is up the level, and position and level round-trip.
    double last = -k_inf;
    for (int i = 1; i <= 100; ++i) {
        const double p = i / 100.0;
        const double db = media::fader_db(p);
        REQUIRE(db > last);
        REQUIRE(media::fader_position(db) == Approx(p).margin(1e-9));
        last = db;
    }
}

TEST_CASE("The meter's scale runs from -60 dBFS to 0", "[media][audio]")
{
    REQUIRE(media::meter_position(-k_inf) == 0.0);
    REQUIRE(media::meter_position(-80.0) == 0.0);
    REQUIRE(media::meter_position(-60.0) == 0.0);
    REQUIRE(media::meter_position(-30.0) == Approx(0.5));
    REQUIRE(media::meter_position(0.0) == 1.0);
    REQUIRE(media::meter_position(4.0) == 1.0);
    REQUIRE(media::meter_position(std::nan("")) == 0.0);
}

TEST_CASE("A strip's readout", "[media][audio]")
{
    REQUIRE(media::format_gain_db(0.0) == "0.0 dB");
    REQUIRE(media::format_gain_db(-0.04) == "0.0 dB");
    REQUIRE(media::format_gain_db(-12.46) == "-12.5 dB");
    REQUIRE(media::format_gain_db(-6.0) == "-6.0 dB");
    REQUIRE(media::format_gain_db(-k_inf) == "-inf dB");
    REQUIRE(media::format_gain_db(-120.0) == "-inf dB");
    REQUIRE(media::format_gain_db(5.0) == "0.0 dB");
}
