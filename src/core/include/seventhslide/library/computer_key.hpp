#pragma once

// This computer's signing key: its identity to every other computer it syncs
// with. Made the first time it is needed and kept in the data directory beside
// ``MachineStore``'s file, readable by this user only. A reinstalled computer
// has a new key, and is invited again.
//
// Seam: std::filesystem and the crypto module.

#include "seventhslide/crypto/signing.hpp"

#include <filesystem>
#include <string_view>

namespace seventhslide::library {

inline constexpr std::string_view k_computer_key_filename = "computer_key.pem";

[[nodiscard]] std::filesystem::path computer_key_file(const std::filesystem::path& data_dir);

/// The key in the file, or a new one written there first. Throws
/// ``std::runtime_error`` when the file holds something that is not a key,
/// or a new one cannot be written: a key not kept would make this computer
/// someone new on every run.
[[nodiscard]] crypto::SigningKey load_computer_key(const std::filesystem::path& data_dir);

}  // namespace seventhslide::library
