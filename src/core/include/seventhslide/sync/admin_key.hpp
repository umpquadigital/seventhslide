#pragma once

// The library's admin key, locked under the admin password, and the recovery
// key whose code goes on paper. The locked key travels with the library to
// every editor computer and server; the password unlocks it on any of them for
// one change, and is never kept.
//
// Seam: the crypto module; nlohmann/json for the file form.

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/crypto/signing.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::sync {

/// Why ``passphrase`` will not do as the admin password, in words for the
/// operator; ``std::nullopt`` when it will. A stolen copy of the locked key
/// can be guessed at offline, so a short password is refused outright.
[[nodiscard]] std::optional<std::string> passphrase_problem(std::string_view passphrase);

struct LockedAdminKey {
    /// The admin key's public half: what the locked box must open to.
    crypto::PublicKey admin;
    crypto::LockedBox box;

    [[nodiscard]] nlohmann::json to_json() const;
    /// Refuses anything but a locked key as ``to_json`` writes one.
    [[nodiscard]] static std::optional<LockedAdminKey> from_json(const nlohmann::json& j);

    friend bool operator==(const LockedAdminKey&, const LockedAdminKey&) = default;
};

[[nodiscard]] LockedAdminKey lock_admin_key(const crypto::SigningKey& admin,
                                            std::string_view passphrase,
                                            crypto::BoxParams params = crypto::k_box_params);

/// The admin key, or ``std::nullopt`` for the wrong password. Throws
/// ``std::runtime_error`` when the box opens to something that is not the
/// key it claims to hold: the file was damaged or swapped.
[[nodiscard]] std::optional<crypto::SigningKey> unlock_admin_key(const LockedAdminKey& locked,
                                                                 std::string_view passphrase);

/// The recovery key as the code shown once and written down: 13 groups of
/// four letters and digits.
[[nodiscard]] std::string recovery_code(const crypto::SigningKey& recovery);
/// The key from a code as typed (either case, with or without the dashes);
/// ``std::nullopt`` for anything that is not one.
[[nodiscard]] std::optional<crypto::SigningKey> key_from_recovery_code(std::string_view code);

}  // namespace seventhslide::sync
