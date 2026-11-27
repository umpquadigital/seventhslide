#include "seventhslide/model/machine_settings.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/util/strings.hpp"

namespace seventhslide::model {

std::string_view ndi_transport_key(NdiTransport transport) noexcept
{
    switch (transport) {
    case NdiTransport::ReliableUdp:
        return "rudp";
    case NdiTransport::Udp:
        return "udp";
    case NdiTransport::MultiTcp:
        return "multi_tcp";
    case NdiTransport::SingleTcp:
        return "tcp";
    case NdiTransport::Automatic:
        break;
    }
    return "auto";
}

NdiTransport ndi_transport_from_key(std::string_view key) noexcept
{
    for (const auto transport : {NdiTransport::ReliableUdp, NdiTransport::Udp,
                                 NdiTransport::MultiTcp, NdiTransport::SingleTcp}) {
        if (key == ndi_transport_key(transport)) {
            return transport;
        }
    }
    return NdiTransport::Automatic;
}

nlohmann::json MachineSettings::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    j[std::string(k_setting_remote_enabled)] = remote_enabled;
    j[std::string(k_setting_remote_pin)] = remote_pin;
    j[std::string(k_setting_remote_port)] = remote_port;
    j[std::string(k_setting_sync_pause_live)] = sync_pause_live;
    j[std::string(k_setting_ndi_transport)] = std::string(ndi_transport_key(ndi_transport));
    j[std::string(k_setting_take_fade_ms)] = take_fade_ms;
    return j;
}

MachineSettings MachineSettings::from_json(const nlohmann::json& j)
{
    MachineSettings s;
    if (!j.is_object()) {
        return s;
    }
    // The constants name string literals, so ``data()`` is NUL-terminated.
    s.remote_enabled = json::style_bool(j, k_setting_remote_enabled.data(), false);
    s.remote_pin = util::trim(json::str_or(j, k_setting_remote_pin.data()));
    s.remote_port = normalize_remote_port(
        json::int_or(j, k_setting_remote_port.data(), k_default_remote_port));
    s.sync_pause_live = json::style_bool(j, k_setting_sync_pause_live.data(), true);
    s.ndi_transport =
        ndi_transport_from_key(util::trim(json::str_or(j, k_setting_ndi_transport.data())));
    s.take_fade_ms = normalize_take_fade_ms(
        json::int_or(j, k_setting_take_fade_ms.data(), k_default_take_fade_ms));
    return s;
}

}  // namespace seventhslide::model
