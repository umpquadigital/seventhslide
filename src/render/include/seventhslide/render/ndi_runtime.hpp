#pragma once

// Runtime discovery of the NDI library.
//
// SeventhSlide never ships, links, or redistributes the NDI runtime. The
// operator installs NDI Tools or the NDI SDK themselves; we look for the
// library at run time and light up the NDI destination type only when it is
// actually present. Everything here is safe to call on a machine that has
// never heard of NDI.

#include <QByteArray>
#include <QString>

namespace seventhslide::render {

namespace ndi {
struct Api;
}

/// How receivers on other computers take this computer's NDI sources.
enum class NdiTransport {
    /// The runtime's own choice: reliable UDP, falling back to whatever else
    /// a receiver takes.
    Automatic,
    /// Reliable UDP only.
    ReliableUdp,
    /// Plain UDP only (the runtime's "unicast").
    Udp,
    /// The runtime's multi-TCP: several connections per receiver.
    MultiTcp,
    /// One TCP connection per receiver, for networks UDP does not get
    /// through. Pictures and sound share it, so the sound waits behind each
    /// picture.
    SingleTcp,
};

/// Send over ``transport`` from when the runtime loads. Any but Automatic is
/// a copy of the operator's own NDI configuration with every other way of
/// sending switched off (ndi_config_sending_over), written into
/// ``config_dir`` and named by ``NDI_CONFIG_DIR``. The runtime reads its
/// configuration once, as it starts: once loaded, a change takes effect when
/// the app next starts.
void ndi_set_transport(NdiTransport transport, const QString& config_dir);

/// What the loaded runtime sends over, or will once it loads.
[[nodiscard]] NdiTransport ndi_transport_in_effect();

/// ``ndi-config.v1.json`` for sending over ``transport`` alone: ``own`` (the
/// operator's, which may be empty or unreadable) with each way of sending
/// (reliable UDP, UDP, multicast, multi-TCP) switched on for ``transport`` and
/// off for the rest, and everything else kept. SingleTcp switches all four
/// off, leaving receivers the runtime's single TCP connection.
[[nodiscard]] QByteArray ndi_config_sending_over(NdiTransport transport, const QByteArray& own);

/// True when an NDI runtime was found, loaded, and initialized. Cached after
/// the first probe; call ``ndi_runtime_refresh()`` to look again.
[[nodiscard]] bool ndi_runtime_available();

/// Version string reported by the runtime (e.g. "NDI SDK LINUX 6.1.1.0"),
/// empty when unavailable.
[[nodiscard]] QString ndi_runtime_version();

/// Absolute path of the loaded library, empty when unavailable. Useful in
/// support logs when several NDI versions are installed.
[[nodiscard]] QString ndi_runtime_path();

/// Operator-facing explanation of why NDI is greyed out, including the
/// platform-specific install hint. Never empty.
[[nodiscard]] QString ndi_missing_message();

/// Re-probe for a runtime. Lets an operator install NDI without restarting
/// SeventhSlide. A runtime that is already loaded is kept as-is.
void ndi_runtime_refresh();

/// Resolved entry points, or nullptr when unavailable. Render-layer internal.
[[nodiscard]] const ndi::Api* ndi_api();

}  // namespace seventhslide::render
