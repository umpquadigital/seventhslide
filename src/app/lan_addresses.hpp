#pragma once

// This computer's addresses another on the local network can reach it at:
// what the remote's links and an SRT stream waiting for a receiver are
// given as.
// Seam: Qt Network.

#include <QStringList>

namespace seventhslide::app {

/// Each IPv4 address of this computer's, but loopback and link-local ones,
/// once each.
[[nodiscard]] QStringList lan_addresses();

}  // namespace seventhslide::app
