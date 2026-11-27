#include "seventhslide/crypto/signing.hpp"

#include "openssl_support.hpp"
#include "seventhslide/crypto/encoding.hpp"
#include "seventhslide/crypto/random.hpp"
#include "seventhslide/crypto/sha256.hpp"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>

#include <utility>

namespace seventhslide::crypto {
namespace {

using openssl::Owned;

Owned<EVP_PKEY> private_key(std::string_view seed)
{
    Owned<EVP_PKEY> key(EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(seed.data()),
        seed.size()));
    if (!key) {
        openssl::fail("The signing key could not be loaded");
    }
    return key;
}

Owned<EVP_PKEY> public_key_handle(std::string_view bytes)
{
    Owned<EVP_PKEY> key(EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size()));
    if (!key) {
        openssl::fail("The public key could not be loaded");
    }
    return key;
}

std::string raw_public(EVP_PKEY* key)
{
    std::string out(PublicKey::k_size, '\0');
    std::size_t size = out.size();
    openssl::check(EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char*>(out.data()),
                                               &size),
                   "The public key could not be read from the signing key");
    out.resize(size);
    return out;
}

}  // namespace

std::optional<PublicKey> PublicKey::from_bytes(std::string_view bytes)
{
    if (bytes.size() != k_size) {
        return std::nullopt;
    }
    PublicKey key;
    key.bytes_ = std::string(bytes);
    return key;
}

std::optional<PublicKey> PublicKey::from_base64(std::string_view text)
{
    const auto bytes = crypto::from_base64(text);
    return bytes ? from_bytes(*bytes) : std::nullopt;
}

std::string PublicKey::base64() const
{
    return to_base64(bytes_);
}

std::string PublicKey::fingerprint() const
{
    return sha256_hex(bytes_);
}

bool PublicKey::verify(std::string_view message, std::string_view signature) const
{
    if (signature.size() != k_signature_size) {
        return false;
    }
    const auto key = public_key_handle(bytes_);
    Owned<EVP_MD_CTX> ctx(EVP_MD_CTX_new());
    openssl::check(EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()),
                   "A signature could not be checked");
    const int result = EVP_DigestVerify(
        ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
        reinterpret_cast<const unsigned char*>(message.data()), message.size());
    if (result < 0) {
        openssl::fail("A signature could not be checked");
    }
    ERR_clear_error();  // a signature that does not match leaves one behind
    return result == 1;
}

SigningKey::SigningKey(std::string seed, PublicKey public_key)
    : seed_(std::move(seed)), public_(std::move(public_key))
{
}

SigningKey::~SigningKey()
{
    OPENSSL_cleanse(seed_.data(), seed_.size());
}

SigningKey SigningKey::generate()
{
    auto key = from_seed(random_bytes(k_seed_size));
    if (!key) {
        openssl::fail("A signing key could not be made");
    }
    return *key;
}

std::optional<SigningKey> SigningKey::from_seed(std::string_view seed)
{
    if (seed.size() != k_seed_size) {
        return std::nullopt;
    }
    const auto key = private_key(seed);
    auto public_key = PublicKey::from_bytes(raw_public(key.get()));
    if (!public_key) {
        return std::nullopt;
    }
    return SigningKey(std::string(seed), std::move(*public_key));
}

std::optional<SigningKey> SigningKey::from_pem(std::string_view pem)
{
    Owned<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    Owned<EVP_PKEY> key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    ERR_clear_error();
    if (!key || !EVP_PKEY_is_a(key.get(), "ED25519")) {
        return std::nullopt;
    }
    std::string seed(k_seed_size, '\0');
    std::size_t size = seed.size();
    if (EVP_PKEY_get_raw_private_key(key.get(), reinterpret_cast<unsigned char*>(seed.data()),
                                     &size)
            <= 0
        || size != k_seed_size) {
        ERR_clear_error();
        return std::nullopt;
    }
    auto result = from_seed(seed);
    OPENSSL_cleanse(seed.data(), seed.size());
    return result;
}

std::string SigningKey::pem() const
{
    const auto key = private_key(seed_);
    Owned<BIO> bio(BIO_new(BIO_s_mem()));
    openssl::check(PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr,
                                            nullptr),
                   "The signing key could not be written");
    return openssl::contents(bio.get());
}

std::string SigningKey::sign(std::string_view message) const
{
    const auto key = private_key(seed_);
    Owned<EVP_MD_CTX> ctx(EVP_MD_CTX_new());
    openssl::check(EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()),
                   "Signing failed");
    std::string signature(PublicKey::k_signature_size, '\0');
    std::size_t size = signature.size();
    openssl::check(EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()),
                                  &size, reinterpret_cast<const unsigned char*>(message.data()),
                                  message.size()),
                   "Signing failed");
    signature.resize(size);
    return signature;
}

std::string SigningKey::certificate_pem(std::string_view common_name) const
{
    const auto key = private_key(seed_);
    Owned<X509> cert(X509_new());
    openssl::check(X509_set_version(cert.get(), 2), "The certificate could not be made");

    // A random serial: two certificates from one key (a reinstall from the
    // same seed) are still told apart.
    const auto serial_bytes = random_bytes(16);
    BIGNUM* serial = BN_bin2bn(reinterpret_cast<const unsigned char*>(serial_bytes.data()),
                               static_cast<int>(serial_bytes.size()), nullptr);
    if (serial == nullptr) {
        openssl::fail("The certificate could not be made");
    }
    BN_to_ASN1_INTEGER(serial, X509_get_serialNumber(cert.get()));
    BN_free(serial);

    // From a day ago (another computer's clock may be behind) for a hundred
    // years: the certificate is never what stops two computers talking.
    if (X509_gmtime_adj(X509_getm_notBefore(cert.get()), -24 * 60 * 60) == nullptr
        || X509_time_adj_ex(X509_getm_notAfter(cert.get()), 100 * 365, 0, nullptr) == nullptr) {
        openssl::fail("The certificate could not be made");
    }

    // Built apart and set as both names: OpenSSL 4 gives the certificate's own
    // subject name back only as const.
    Owned<X509_NAME> name(X509_NAME_new());
    if (name == nullptr) {
        openssl::fail("The certificate could not be made");
    }
    openssl::check(X509_NAME_add_entry_by_txt(
                       name.get(), "CN", MBSTRING_UTF8,
                       reinterpret_cast<const unsigned char*>(common_name.data()),
                       static_cast<int>(common_name.size()), -1, 0),
                   "The certificate could not be made");
    openssl::check(X509_set_subject_name(cert.get(), name.get()), "The certificate could not be made");
    openssl::check(X509_set_issuer_name(cert.get(), name.get()), "The certificate could not be made");
    openssl::check(X509_set_pubkey(cert.get(), key.get()), "The certificate could not be made");
    // Ed25519 signs the whole certificate itself: no digest is named.
    openssl::check(X509_sign(cert.get(), key.get(), nullptr), "The certificate could not be signed");

    Owned<BIO> bio(BIO_new(BIO_s_mem()));
    openssl::check(PEM_write_bio_X509(bio.get(), cert.get()),
                   "The certificate could not be written");
    return openssl::contents(bio.get());
}

}  // namespace seventhslide::crypto
