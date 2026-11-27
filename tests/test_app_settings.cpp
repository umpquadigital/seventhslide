#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/app_settings.hpp>
#include <seventhslide/model/machine_settings.hpp>

#include <utility>

using seventhslide::model::AppSettings;
using seventhslide::model::MachineSettings;
using seventhslide::model::k_default_theme_priority;
using seventhslide::model::normalize_theme_priority;

TEST_CASE("AppSettings round-trip and settings map", "[model]")
{
    AppSettings s;
    s.organization_name = "Grace Seventh-day Adventist Church";
    s.ccli_licence_number = "12345";
    s.theme_priority = normalize_theme_priority({"global", "content", "service"});

    const auto j = s.to_json();
    const auto back = AppSettings::from_json(j);
    REQUIRE(back.organization_name == "Grace Seventh-day Adventist Church");
    REQUIRE(back.ccli_licence_number == "12345");
    REQUIRE(back.theme_priority == s.theme_priority);

    const auto map = s.to_json();
    REQUIRE(map["organization_name"] == "Grace Seventh-day Adventist Church");
    REQUIRE(map["ccli_licence_number"] == "12345");

    const auto from_map = AppSettings::from_json(map);
    REQUIRE(from_map.ccli_licence_number == "12345");
    REQUIRE(from_map.theme_priority[0] == "global");
}

TEST_CASE("AppSettings drops keys it does not know and bad priorities", "[model]")
{
    nlohmann::json j = {{"theme_priority", nlohmann::json::array({"a", "b"})},
                        {"not_a_setting", "x"}};
    const auto s = AppSettings::from_json(j);
    REQUIRE(s.theme_priority.size() == 3);
    REQUIRE(s.theme_priority[0] == std::string{k_default_theme_priority[0]});
    REQUIRE_FALSE(s.to_json().contains("not_a_setting"));
}

TEST_CASE("normalize_remote_port clamps to the usable range", "[model]")
{
    using seventhslide::model::k_default_remote_port;
    using seventhslide::model::normalize_remote_port;

    // In range, kept as chosen.
    REQUIRE(normalize_remote_port(1024) == 1024);
    REQUIRE(normalize_remote_port(8765) == 8765);
    REQUIRE(normalize_remote_port(65535) == 65535);

    // Privileged ports need root to bind, so the app would fail to start the
    // remote rather than report a bad setting.
    REQUIRE(normalize_remote_port(0) == k_default_remote_port);
    REQUIRE(normalize_remote_port(80) == k_default_remote_port);
    REQUIRE(normalize_remote_port(1023) == k_default_remote_port);

    // Out of range in the other direction, including values a corrupt settings
    // row could hold.
    REQUIRE(normalize_remote_port(65536) == k_default_remote_port);
    REQUIRE(normalize_remote_port(-1) == k_default_remote_port);
    REQUIRE(normalize_remote_port(-8765) == k_default_remote_port);
}

TEST_CASE("MachineSettings round-trip and normalize a stored remote port", "[model]")
{
    MachineSettings s;
    s.remote_enabled = true;
    s.remote_pin = "4321";
    s.remote_port = 9100;
    const auto j = s.to_json();
    REQUIRE(j["remote_enabled"] == true);
    REQUIRE(j["remote_pin"] == "4321");
    REQUIRE(MachineSettings::from_json(j) == s);

    // The clamp has to run on load, not only in the settings UI - a hand-edited
    // or migrated value is exactly the case it exists for.
    const auto from_bad = MachineSettings::from_json(nlohmann::json{{"remote_port", 80}});
    REQUIRE(from_bad.remote_port == seventhslide::model::k_default_remote_port);
    const auto from_good = MachineSettings::from_json(nlohmann::json{{"remote_port", 9000}});
    REQUIRE(from_good.remote_port == 9000);
    REQUIRE(MachineSettings::from_json(nlohmann::json{{"remote_pin", " 12 \n"}}).remote_pin
            == "12");
}

TEST_CASE("MachineSettings keep the take's Fade length, half a second until set", "[model]")
{
    using seventhslide::model::k_max_take_fade_ms;
    using seventhslide::model::k_min_take_fade_ms;

    CHECK(MachineSettings{}.take_fade_ms == 500);
    // Never set (an older file, or a library's app settings): the default.
    CHECK(MachineSettings::from_json(nlohmann::json::object()).take_fade_ms == 500);
    MachineSettings s;
    s.take_fade_ms = 1200;
    CHECK(MachineSettings::from_json(s.to_json()) == s);
    // Out of range, the nearest end of it.
    CHECK(MachineSettings::from_json(nlohmann::json{{"take_fade_ms", 0}}).take_fade_ms
          == k_min_take_fade_ms);
    CHECK(MachineSettings::from_json(nlohmann::json{{"take_fade_ms", 99999}}).take_fade_ms
          == k_max_take_fade_ms);
}

TEST_CASE("MachineSettings keep the NDI transport, Automatic unless picked", "[model]")
{
    using seventhslide::model::NdiTransport;

    REQUIRE(MachineSettings{}.ndi_transport == NdiTransport::Automatic);
    REQUIRE(MachineSettings{}.to_json()["ndi_transport"] == "auto");
    // Each kept under its own key; "tcp", the first TCP there was, stays
    // single TCP.
    const std::pair<NdiTransport, const char*> kept[] = {
        {NdiTransport::ReliableUdp, "rudp"},
        {NdiTransport::Udp, "udp"},
        {NdiTransport::MultiTcp, "multi_tcp"},
        {NdiTransport::SingleTcp, "tcp"},
    };
    for (const auto& [transport, key] : kept) {
        MachineSettings s;
        s.ndi_transport = transport;
        const auto j = s.to_json();
        REQUIRE(j["ndi_transport"] == key);
        REQUIRE(MachineSettings::from_json(j) == s);
    }

    // Anything the app did not write reads as the runtime's own choice.
    REQUIRE(MachineSettings::from_json(nlohmann::json{{"ndi_transport", " tcp "}}).ndi_transport
            == NdiTransport::SingleTcp);
    REQUIRE(MachineSettings::from_json(nlohmann::json{{"ndi_transport", "multicast"}}).ndi_transport
            == NdiTransport::Automatic);
    REQUIRE(MachineSettings::from_json(nlohmann::json{{"ndi_transport", 1}}).ndi_transport
            == NdiTransport::Automatic);
}

TEST_CASE("Library and machine settings keep to their own keys", "[model]")
{
    // A library's settings never carry the remote's, and the remote's never
    // carry a library's.
    AppSettings library;
    library.organization_name = "Grace Church";
    const auto library_json = library.to_json();
    for (const auto key : {seventhslide::model::k_setting_remote_enabled,
                           seventhslide::model::k_setting_remote_pin,
                           seventhslide::model::k_setting_remote_port}) {
        INFO(key);
        REQUIRE_FALSE(library_json.contains(key));
    }
    MachineSettings machine;
    machine.remote_pin = "2468";
    REQUIRE_FALSE(machine.to_json().contains("organization_name"));
}

TEST_CASE("AppSettings trims the organization name and defaults it to empty", "[model]")
{
    REQUIRE(AppSettings::from_json(nlohmann::json::object()).organization_name.empty());
    const auto s = AppSettings::from_json({{"organization_name", "  Grace Church \n"}});
    REQUIRE(s.organization_name == "Grace Church");
}
