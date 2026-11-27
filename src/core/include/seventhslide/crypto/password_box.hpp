#pragma once

// A secret locked under a passphrase, so it can travel with a library and be
// used only by someone who knows the passphrase: the sync's admin key
// (``sync/admin_key.hpp``). The passphrase becomes a key by scrypt, tuned slow
// (about a second, 128 MiB) so a stolen copy is slow to guess at; the secret
// is sealed under that key with AES-256-GCM, so a wrong passphrase opens
// nothing and a changed box is noticed.
//
// Plain C++20 over OpenSSL (no Qt); nlohmann/json for the file form.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::crypto {

/// scrypt's cost: 2^log2_n blocks of 128 * r bytes, p times over.
struct BoxParams {
    int log2_n = 17;
    int r = 8;
    int p = 1;

    friend bool operator==(const BoxParams&, const BoxParams&) = default;
};

/// About a second on a desktop of 2025, 128 MiB of memory.
inline constexpr BoxParams k_box_params{};
/// Instant, for tests; no protection against guessing.
inline constexpr BoxParams k_box_params_quick{4, 8, 1};

struct LockedBox {
    BoxParams params;
    std::string salt;    // 16 bytes
    std::string nonce;   // 12 bytes
    std::string sealed;  // the secret encrypted, the 16-byte tag after it

    [[nodiscard]] nlohmann::json to_json() const;
    /// Refuses anything but a box as ``to_json`` writes one.
    [[nodiscard]] static std::optional<LockedBox> from_json(const nlohmann::json& j);

    friend bool operator==(const LockedBox&, const LockedBox&) = default;
};

/// Lock ``secret`` under ``passphrase``. Throws ``std::runtime_error`` when
/// the crypto library fails.
[[nodiscard]] LockedBox lock(std::string_view secret, std::string_view passphrase,
                             BoxParams params = k_box_params);

/// The secret, or ``std::nullopt`` for the wrong passphrase (or a box that
/// has been changed: the two cannot be told apart).
[[nodiscard]] std::optional<std::string> unlock(const LockedBox& box, std::string_view passphrase);

}  // namespace seventhslide::crypto
