#pragma once

// SHA-256, for fingerprints and the sync's media declarations.
//
// Plain C++20 over OpenSSL (no Qt).

#include <memory>
#include <string>
#include <string_view>

namespace seventhslide::crypto {

/// The 32-byte digest of ``bytes``.
[[nodiscard]] std::string sha256(std::string_view bytes);
/// The digest as 64 lower-case hex digits.
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

/// SHA-256 over bytes given a piece at a time, for a file too large to hold.
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(Sha256&&) noexcept;
    Sha256& operator=(Sha256&&) noexcept;
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    /// Take in the next ``bytes``.
    void update(std::string_view bytes);
    /// The digest of everything taken in, as 64 lower-case hex digits. Once
    /// asked, nothing more can be taken in.
    [[nodiscard]] std::string finish_hex();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace seventhslide::crypto
