#pragma once

// OpenSSL as the crypto module uses it: owning handles, and one way to turn
// a failed call into an exception. Not part of the public API.

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace seventhslide::crypto::openssl {

struct Free {
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
    void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); }
    void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); }
    void operator()(BIO* p) const { BIO_free(p); }
    void operator()(X509* p) const { X509_free(p); }
    void operator()(X509_NAME* p) const { X509_NAME_free(p); }
};
template <typename T>
using Owned = std::unique_ptr<T, Free>;

/// The crypto library failed at ``what``: a ``std::runtime_error`` naming it
/// and OpenSSL's own account of the failure.
[[noreturn]] inline void fail(const std::string& what)
{
    std::string why;
    while (const unsigned long code = ERR_get_error()) {
        if (!why.empty()) {
            why += "; ";
        }
        why += ERR_reason_error_string(code) ? ERR_reason_error_string(code) : "unknown error";
    }
    throw std::runtime_error(what + (why.empty() ? "" : " (" + why + ")"));
}

/// ``result``, or ``fail(what)`` when it says the call failed (0 or less).
inline void check(int result, const std::string& what)
{
    if (result <= 0) {
        fail(what);
    }
}

/// The bytes a memory BIO holds.
inline std::string contents(BIO* bio)
{
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    return n > 0 ? std::string(data, static_cast<std::size_t>(n)) : std::string{};
}

}  // namespace seventhslide::crypto::openssl
