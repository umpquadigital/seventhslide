#pragma once

// Signing keys and signatures: Ed25519 through OpenSSL. A ``SigningKey`` is
// a computer's identity, the library's admin key, or its recovery key; the
// ``PublicKey`` is the half everyone else holds, to check what the key
// signed. Each key also makes the certificate that proves its computer on a
// TLS connection (Qt's TLS takes an Ed25519 certificate, given the key as
// an OpenSSL key object; the link does that from the PEM).
//
// Plain C++20 over OpenSSL (no Qt).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::crypto {

class PublicKey {
public:
    static constexpr std::size_t k_size = 32;
    static constexpr std::size_t k_signature_size = 64;

    /// The 32 raw bytes of a key; anything else is refused.
    [[nodiscard]] static std::optional<PublicKey> from_bytes(std::string_view bytes);
    /// The key as ``base64`` gives it.
    [[nodiscard]] static std::optional<PublicKey> from_base64(std::string_view text);

    [[nodiscard]] const std::string& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::string base64() const;
    /// SHA-256 of the key, 64 hex digits: the key's name wherever one is
    /// needed (a file name, a computer's status).
    [[nodiscard]] std::string fingerprint() const;

    /// Whether ``signature`` is this key's signature of ``message``. A
    /// signature of the wrong size is simply not one.
    [[nodiscard]] bool verify(std::string_view message, std::string_view signature) const;

    friend bool operator==(const PublicKey&, const PublicKey&) = default;
    friend auto operator<=>(const PublicKey&, const PublicKey&) = default;

private:
    std::string bytes_;
};

class SigningKey {
public:
    static constexpr std::size_t k_seed_size = 32;

    [[nodiscard]] static SigningKey generate();
    /// A key from its 32-byte seed (``seed()``); anything else is refused.
    [[nodiscard]] static std::optional<SigningKey> from_seed(std::string_view seed);
    /// A key from its PEM (``pem()``): one PKCS#8 Ed25519 private key,
    /// unencrypted. Anything else is refused.
    [[nodiscard]] static std::optional<SigningKey> from_pem(std::string_view pem);

    SigningKey(const SigningKey&) = default;
    SigningKey(SigningKey&&) noexcept = default;
    SigningKey& operator=(const SigningKey&) = default;
    SigningKey& operator=(SigningKey&&) noexcept = default;
    /// The seed is wiped.
    ~SigningKey();

    /// The 32 bytes the whole key is made from: the secret.
    [[nodiscard]] const std::string& seed() const noexcept { return seed_; }
    /// The key as a PEM file (PKCS#8, unencrypted): how a computer keeps its
    /// own, and how the link hands it to TLS.
    [[nodiscard]] std::string pem() const;
    [[nodiscard]] const PublicKey& public_key() const noexcept { return public_; }

    /// The 64-byte signature of ``message``.
    [[nodiscard]] std::string sign(std::string_view message) const;

    /// A self-signed X.509 certificate for this key, as PEM, with
    /// ``common_name`` as its subject, good for a hundred years: what the
    /// computer shows on a TLS connection. Whoever holds the public key can
    /// check it is this key's.
    [[nodiscard]] std::string certificate_pem(std::string_view common_name) const;

    friend bool operator==(const SigningKey& a, const SigningKey& b) { return a.seed_ == b.seed_; }

private:
    SigningKey(std::string seed, PublicKey public_key);

    std::string seed_;
    PublicKey public_;
};

}  // namespace seventhslide::crypto
