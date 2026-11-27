// SRT destinations: what a new one is, how its stream settings are checked
// and kept, and a live one whose row follows its stream (waiting, sending).

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "destination_rows.hpp"
#include "srt_test_receiver.hpp"
#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>
#include <seventhslide/stream/stream_settings.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QVariantMap>

#include <chrono>
#include <string>

using seventhslide::render::OutputWindowHost;
using seventhslide::stream::SrtMode;
using seventhslide::stream::StreamSettings;
using namespace std::chrono_literals;

namespace {

using seventhslide::test::wait_until;

[[nodiscard]] QVariantMap row_of(const OutputWindowHost& host, const QString& id)
{
    for (const auto& r : seventhslide::app::destination_rows(host)) {
        if (r.toMap().value(QStringLiteral("id")).toString() == id) {
            return r.toMap();
        }
    }
    FAIL("no row " << id.toStdString());
    return {};
}

}  // namespace

TEST_CASE("A new SRT destination waits for a receiver on a port of its own",
          "[render][srt][destinations]")
{
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::stream::srt_supported()) {
        SKIP("this FFmpeg cannot send SRT");
    }
    OutputWindowHost host;
    const QString a = host.add_destination(QStringLiteral("out-main"), QStringLiteral("srt"));
    const QString b = host.add_destination(QStringLiteral("out-main"), QStringLiteral("srt"));
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(b.isEmpty());
    const auto* da = host.registry().find(a);
    const auto* db = host.registry().find(b);
    CHECK(da->stream.mode == SrtMode::Listener);
    CHECK(da->stream.port == seventhslide::stream::k_default_port);
    CHECK(db->stream.port == seventhslide::stream::k_default_port + 1);
    CHECK(da->target_key == QStringLiteral("srt://:9000"));
    CHECK(da->audio);

    // What the editor is shown: the settings, never the passphrase.
    const auto row = row_of(host, a);
    CHECK(row.value(QStringLiteral("type")).toString() == QStringLiteral("srt"));
    CHECK(row.value(QStringLiteral("sizable")).toBool());
    CHECK(row.value(QStringLiteral("audioCapable")).toBool());
    CHECK(row.value(QStringLiteral("streamState")).toString() == QStringLiteral("stopped"));
    const auto stream = row.value(QStringLiteral("stream")).toMap();
    CHECK(stream.value(QStringLiteral("mode")).toString() == QStringLiteral("listener"));
    CHECK(stream.value(QStringLiteral("port")).toInt() == 9000);
    CHECK(stream.value(QStringLiteral("preset")).toString() == QStringLiteral("low_delay"));
    CHECK_FALSE(stream.value(QStringLiteral("encrypted")).toBool());
    CHECK_FALSE(stream.contains(QStringLiteral("passphrase")));
}

TEST_CASE("An SRT destination's stream settings are checked and kept with it",
          "[render][srt][destinations]")
{
    seventhslide::test::ensure_qt_application();
    OutputWindowHost host;
    const nlohmann::json srt{{"id", "s"}, {"output", "out-main"}, {"type", "srt"}, {"enabled", false}};
    const nlohmann::json ndi{{"id", "n"}, {"output", "out-main"}, {"type", "ndi"}};
    host.replace_destinations(nlohmann::json::array({srt, ndi}));
    // A record with no stream settings streams as a new one does.
    REQUIRE(host.registry().find(QStringLiteral("s"))->stream == StreamSettings{});

    // The editor's changes, a few at a time, over what it has.
    const auto changed = [&](const QVariantMap& changes) {
        return seventhslide::app::stream_settings_from(
            changes, host.registry().find(QStringLiteral("s"))->stream);
    };
    // Sending to a receiver, saved before its address is: switched on, it
    // says what it still needs.
    auto caller = changed({{QStringLiteral("mode"), QStringLiteral("caller")}});
    REQUIRE(caller);
    REQUIRE(host.set_destination_stream(QStringLiteral("s"), *caller));
    CHECK(host.registry().find(QStringLiteral("s"))->stream.mode == SrtMode::Caller);
    if (seventhslide::stream::srt_supported()) {
        CHECK_FALSE(host.set_destination_enabled(QStringLiteral("s"), true));
        CHECK(host.last_error().contains(QStringLiteral("address")));
    }
    // A wrong port is refused, and changes nothing.
    auto bad = changed({{QStringLiteral("port"), 70000}});
    REQUIRE(bad);
    CHECK_FALSE(host.set_destination_stream(QStringLiteral("s"), *bad));
    CHECK_FALSE(host.last_error().isEmpty());
    CHECK(host.registry().find(QStringLiteral("s"))->stream.port == 9000);
    caller = changed({{QStringLiteral("mode"), QStringLiteral("caller")},
                      {QStringLiteral("host"), QStringLiteral(" studio.local ")},
                      {QStringLiteral("port"), QStringLiteral("9100")},
                      {QStringLiteral("preset"), QStringLiteral("reliable")},
                      {QStringLiteral("codec"), QStringLiteral("h264")},
                      {QStringLiteral("bitrateKbps"), 8000},
                      {QStringLiteral("passphrase"), QStringLiteral("correct horse battery")}});
    REQUIRE(caller);
    REQUIRE(host.set_destination_stream(QStringLiteral("s"), *caller));
    const auto* d = host.registry().find(QStringLiteral("s"));
    CHECK(d->stream.host == "studio.local");
    CHECK(d->stream.port == 9100);
    CHECK(d->target_key == QStringLiteral("srt://studio.local:9100"));
    CHECK(row_of(host, QStringLiteral("s")).value(QStringLiteral("stream")).toMap()
              .value(QStringLiteral("encrypted")).toBool());

    // Waiting for a receiver again: on every address, with the receiver's
    // kept for switching back.
    const auto listener = changed({{QStringLiteral("mode"), QStringLiteral("listener")}});
    REQUIRE(listener);
    REQUIRE(host.set_destination_stream(QStringLiteral("s"), *listener));
    CHECK(d->target_key == QStringLiteral("srt://:9100"));
    CHECK(d->stream.host == "studio.local");
    REQUIRE(host.set_destination_stream(QStringLiteral("s"), *caller));
    CHECK(d->target_key == QStringLiteral("srt://studio.local:9100"));

    // Names it does not know, and kinds that do not stream.
    CHECK_FALSE(changed({{QStringLiteral("mode"), QStringLiteral("rendezvous")}}));
    CHECK_FALSE(changed({{QStringLiteral("codec"), QStringLiteral("av1")}}));
    CHECK_FALSE(host.set_destination_stream(QStringLiteral("n"), StreamSettings{}));
    CHECK_FALSE(changed({{QStringLiteral("port"), QStringLiteral("x")}}).value().port > 0);

    // Kept with the destination for the next launch, passphrase and all.
    OutputWindowHost reloaded;
    reloaded.replace_destinations(host.destination_records());
    CHECK(reloaded.registry().find(QStringLiteral("s"))->stream == d->stream);
}

TEST_CASE("A live SRT destination's row follows its stream", "[render][srt][live]")
{
    seventhslide::test::ensure_qt_application();
    namespace stream = seventhslide::stream;
    if (!stream::srt_supported()
        || (stream::usable_encoders(stream::VideoCodec::H264).empty()
            && stream::usable_encoders(stream::VideoCodec::H265).empty())) {
        SKIP("this FFmpeg cannot send SRT, or no H.264/H.265 encoder opens here");
    }
    OutputWindowHost host;
    const QString id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("srt"));
    REQUIRE_FALSE(id.isEmpty());
    StreamSettings s = host.registry().find(id)->stream;
    s.port = seventhslide::test::free_udp_port();
    REQUIRE(s.port > 0);
    if (!stream::usable_encoders(stream::VideoCodec::H264).empty()) {
        s.codec = stream::VideoCodec::H264;
    }
    REQUIRE(host.set_destination_stream(id, s));

    QSignalSpy told(&host, &OutputWindowHost::sinkStatusChanged);
    REQUIRE(host.set_destination_enabled(id, true));
    REQUIRE(host.is_live(*host.registry().find(id)));
    const auto state = [&] {
        return row_of(host, id).value(QStringLiteral("streamState")).toString();
    };
    // Said as it happens, not found on the next reload.
    REQUIRE(wait_until([&] { return told.count() > 0 && state() == QStringLiteral("waiting"); },
                       5000));
    // Where a receiver finds it, on each of this computer's addresses (none
    // on a computer with no network).
    const auto waiting = row_of(host, id).value(QStringLiteral("stream")).toMap();
    for (const QString& url : waiting.value(QStringLiteral("reachAt")).toStringList()) {
        CHECK(url.endsWith(QStringLiteral(":%1").arg(s.port)));
    }

    seventhslide::test::SrtTestReceiver receiver;
    REQUIRE(receiver.open("srt://127.0.0.1:" + std::to_string(s.port), {{"mode", "caller"}}, 15s));
    REQUIRE(wait_until([&] { return state() == QStringLiteral("sending"); }, 5000));
    const auto sending = row_of(host, id);
    CHECK_FALSE(sending.value(QStringLiteral("encoder")).toString().isEmpty());
    CHECK(sending.value(QStringLiteral("bitrateKbps")).toInt() > 0);

    // Another SRT destination waiting on the same port takes it.
    const QString other = host.add_destination(QStringLiteral("out-main"), QStringLiteral("srt"));
    REQUIRE(host.set_destination_stream(other, s));
    REQUIRE(host.set_destination_enabled(other, true));
    CHECK_FALSE(host.registry().find(id)->enabled);
    host.shutdown();
}
