#include "seventhslide/crypto/sha256.hpp"

#include "openssl_support.hpp"
#include "seventhslide/crypto/encoding.hpp"

namespace seventhslide::crypto {

std::string sha256(std::string_view bytes)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    openssl::check(EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr),
                   "SHA-256 failed");
    return std::string(reinterpret_cast<const char*>(digest), size);
}

std::string sha256_hex(std::string_view bytes)
{
    return to_hex(sha256(bytes));
}

struct Sha256::State {
    openssl::Owned<EVP_MD_CTX> ctx{EVP_MD_CTX_new()};
    bool finished = false;
};

Sha256::Sha256() : state_(std::make_unique<State>())
{
    if (!state_->ctx) {
        openssl::fail("SHA-256 failed");
    }
    openssl::check(EVP_DigestInit_ex(state_->ctx.get(), EVP_sha256(), nullptr), "SHA-256 failed");
}

Sha256::~Sha256() = default;
Sha256::Sha256(Sha256&&) noexcept = default;
Sha256& Sha256::operator=(Sha256&&) noexcept = default;

void Sha256::update(std::string_view bytes)
{
    if (!state_ || state_->finished) {
        throw std::logic_error("SHA-256 used after it finished or moved");
    }
    openssl::check(EVP_DigestUpdate(state_->ctx.get(), bytes.data(), bytes.size()), "SHA-256 failed");
}

std::string Sha256::finish_hex()
{
    if (!state_ || state_->finished) {
        throw std::logic_error("SHA-256 already finished");
    }
    // Finished whether or not it succeeds: a failed context is not used again.
    state_->finished = true;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    openssl::check(EVP_DigestFinal_ex(state_->ctx.get(), digest, &size), "SHA-256 failed");
    return to_hex(std::string_view(reinterpret_cast<const char*>(digest), size));
}

}  // namespace seventhslide::crypto
