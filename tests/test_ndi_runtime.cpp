#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/render/ndi_abi.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

using seventhslide::render::ndi_api;
using seventhslide::render::ndi_missing_message;
using seventhslide::render::ndi_runtime_available;
using seventhslide::render::ndi_runtime_path;
using seventhslide::render::ndi_runtime_refresh;
using seventhslide::render::ndi_runtime_version;

// These must all pass on a machine with no NDI runtime installed - that is the
// normal case for CI and for most build machines.

TEST_CASE("NDI runtime probe is safe with no runtime installed", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();

    const bool available = ndi_runtime_available();

    if (available) {
        // A real runtime is installed on this machine: every entry point we
        // require must have resolved, and the version must be reported.
        const auto* api = ndi_api();
        REQUIRE(api != nullptr);
        REQUIRE(api->initialize != nullptr);
        REQUIRE(api->destroy != nullptr);
        REQUIRE(api->version != nullptr);
        REQUIRE(api->is_supported_CPU != nullptr);
        REQUIRE(api->send_create != nullptr);
        REQUIRE(api->send_destroy != nullptr);
        REQUIRE(api->send_send_video_v2 != nullptr);
        REQUIRE(api->send_send_video_async_v2 != nullptr);
        REQUIRE_FALSE(ndi_runtime_version().isEmpty());
        REQUIRE_FALSE(ndi_runtime_path().isEmpty());
    } else {
        REQUIRE(ndi_api() == nullptr);
        REQUIRE(ndi_runtime_version().isEmpty());
        REQUIRE(ndi_runtime_path().isEmpty());
    }
}

TEST_CASE("NDI probe result is stable across calls and refresh", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();

    const bool first = ndi_runtime_available();
    REQUIRE(ndi_runtime_available() == first);

    // Refresh exists so an operator can install NDI without restarting. It must
    // never downgrade an already-loaded runtime, and must not crash when there
    // is nothing to find.
    ndi_runtime_refresh();
    REQUIRE(ndi_runtime_available() >= first);

    const auto* a = ndi_api();
    const auto* b = ndi_api();
    REQUIRE(a == b);
}

TEST_CASE("NDI missing message names the install step", "[render][ndi]")
{
    const QString msg = ndi_missing_message();
    REQUIRE_FALSE(msg.isEmpty());
    REQUIRE(msg.contains(QStringLiteral("NDI")));
    REQUIRE(msg.contains(QStringLiteral("ndi.video")));
    // We must be explicit that the runtime is not bundled - that is the whole
    // licensing posture of this feature.
    REQUIRE(msg.contains(QStringLiteral("does not bundle")));
}

TEST_CASE("NDI fourcc packing matches the wire order", "[render][ndi]")
{
    namespace ndi = seventhslide::render::ndi;

    // 'B' | 'G'<<8 | 'R'<<16 | 'A'<<24
    REQUIRE(ndi::fourcc('B', 'G', 'R', 'A') == 0x41524742);
    REQUIRE(ndi::k_fourcc_bgra == 0x41524742);
    REQUIRE(ndi::k_fourcc_bgrx == 0x58524742);
    REQUIRE(ndi::k_fourcc_uyvy == 0x59565955);
    REQUIRE(ndi::k_fourcc_bgra != ndi::k_fourcc_bgrx);
}

TEST_CASE("An NDI transport switches every other way of sending off and keeps the operator's settings",
          "[render][ndi]")
{
    using seventhslide::render::NdiTransport;
    using seventhslide::render::ndi_config_sending_over;

    const auto parse = [](const QByteArray& json) {
        return QJsonDocument::fromJson(json).object();
    };
    // ndi.<way>.send.enable for rudp, unicast (UDP), multicast, tcp (multi-TCP).
    const auto sending = [](const QJsonObject& root) {
        QStringList on;
        for (const auto& way : {QStringLiteral("rudp"), QStringLiteral("unicast"),
                                QStringLiteral("multicast"), QStringLiteral("tcp")}) {
            const auto enable = root["ndi"].toObject()[way].toObject()["send"].toObject()["enable"];
            REQUIRE(enable.isBool());
            if (enable.toBool()) {
                on << way;
            }
        }
        return on;
    };

    // Each leaves its own way on and no other; single TCP leaves none, so
    // receivers take the runtime's single TCP connection.
    CHECK(sending(parse(ndi_config_sending_over(NdiTransport::ReliableUdp, {})))
          == QStringList{QStringLiteral("rudp")});
    CHECK(sending(parse(ndi_config_sending_over(NdiTransport::Udp, {})))
          == QStringList{QStringLiteral("unicast")});
    CHECK(sending(parse(ndi_config_sending_over(NdiTransport::MultiTcp, {})))
          == QStringList{QStringLiteral("tcp")});
    CHECK(sending(parse(ndi_config_sending_over(NdiTransport::SingleTcp, {}))).isEmpty());

    // None of their own, or one that does not parse: just the switches.
    for (const QByteArray& own : {QByteArray{}, QByteArray("{not json")}) {
        const auto root = parse(ndi_config_sending_over(NdiTransport::SingleTcp, own));
        REQUIRE(root.keys() == QStringList{QStringLiteral("ndi")});
    }

    // Theirs: their groups, machine name and receiving left as they were.
    const auto root = parse(ndi_config_sending_over(NdiTransport::MultiTcp, R"({
        "ndi": {"machinename": "Booth", "groups": {"send": "Stage"},
                "rudp": {"recv": {"enable": true}, "send": {"enable": true}}},
        "other": 1})"));
    CHECK(sending(root) == QStringList{QStringLiteral("tcp")});
    const auto ndi = root["ndi"].toObject();
    REQUIRE(ndi["machinename"] == QStringLiteral("Booth"));
    REQUIRE(ndi["groups"].toObject()["send"] == QStringLiteral("Stage"));
    REQUIRE(ndi["rudp"].toObject()["recv"].toObject()["enable"] == QJsonValue(true));
    REQUIRE(root["other"] == QJsonValue(1));
}
