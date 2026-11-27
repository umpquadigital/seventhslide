#pragma once

// What this computer is called, for the name others see it by in sync until
// the person gives it one.
//
// Seam: Qt Core (QSysInfo); macOS's SystemConfiguration.

#include <string>

namespace seventhslide::platform {

/// macOS: the Computer Name from System Settings > General > Sharing ("Papa's
/// MacBook Air"), which stays put. Elsewhere, and when it has none, the host
/// name (QSysInfo::machineHostName). On macOS that is the network's name for
/// it ("PapaMac-II.local"), which can follow the network it is on.
[[nodiscard]] std::string computer_name();

}  // namespace seventhslide::platform
