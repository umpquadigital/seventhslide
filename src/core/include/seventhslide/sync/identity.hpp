#pragma once

// Who this computer is to the sync: its machine id (``library::MachineStore``),
// its name for people, and its signing key (``library::load_computer_key``).
// The engine signs what it writes with the key; the member list binds the
// machine id to the key.
//
// Seam: the crypto module.

#include "seventhslide/crypto/signing.hpp"

#include <string>

namespace seventhslide::sync {

struct Identity {
    std::string machine;
    std::string computer;
    crypto::SigningKey key;
};

}  // namespace seventhslide::sync
