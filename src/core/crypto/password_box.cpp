#include "seventhslide/crypto/password_box.hpp"

#include "openssl_support.hpp"
#include "seventhslide/crypto/encoding.hpp"
#include "seventhslide/crypto/random.hpp"

#include <openssl/crypto.h>
#include <openssl/kdf.h>

#include <cstdint>

namespace seventhslide::crypto {
namespace {

using openssl::Owned;

constexpr std::size_t k_salt_size = 16;
constexpr std::size_t k_nonce_size = 12;
constexpr std::size_t k_tag_size = 16;
constexpr std::size_t k_key_size = 32;
// scrypt needs 128 * r * N bytes; allow for the default and a little over.
constexpr std::uint64_t k_max_memory = std::uint64_t{512} * 1024 * 1024;

std::string derive_key(std::string_view passphrase, const LockedBox& box)
{
    if (box.params.log2_n < 1 || box.params.log2_n > 24 || box.params.r < 1
        || box.params.p < 1) {
        throw std::runtime_error("The locked box names a cost that cannot be paid");
    }
    std::string key(k_key_size, '\0');
    openssl::check(EVP_PBE_scrypt(passphrase.data(), passphrase.size(),
                                  reinterpret_cast<const unsigned char*>(box.salt.data()),
                                  box.salt.size(), std::uint64_t{1} << box.params.log2_n,
                                  static_cast<std::uint64_t>(box.params.r),
                                  static_cast<std::uint64_t>(box.params.p), k_max_memory,
                                  reinterpret_cast<unsigned char*>(key.data()), key.size()),
                   "The passphrase could not be turned into a key");
    return key;
}

struct Wiped {
    std::string& bytes;
    ~Wiped() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
};

}  // namespace

nlohmann::json LockedBox::to_json() const
{
    return {{"kdf", "scrypt"},
            {"log2_n", params.log2_n},
            {"r", params.r},
            {"p", params.p},
            {"salt", to_base64(salt)},
            {"cipher", "aes-256-gcm"},
            {"nonce", to_base64(nonce)},
            {"sealed", to_base64(sealed)}};
}

std::optional<LockedBox> LockedBox::from_json(const nlohmann::json& j)
{
    const auto is = [&](const char* name, std::string_view value) {
        const auto it = j.find(name);
        return it != j.end() && it->is_string() && it->get<std::string>() == value;
    };
    if (!j.is_object() || !is("kdf", "scrypt") || !is("cipher", "aes-256-gcm")) {
        return std::nullopt;
    }
    const auto number = [&](const char* name) -> std::optional<int> {
        const auto it = j.find(name);
        if (it == j.end() || !it->is_number_integer()) {
            return std::nullopt;
        }
        const auto v = it->get<std::int64_t>();
        return v >= 1 && v <= 64 ? std::optional<int>(static_cast<int>(v)) : std::nullopt;
    };
    const auto bytes = [&](const char* name) -> std::optional<std::string> {
        const auto it = j.find(name);
        return it != j.end() && it->is_string() ? from_base64(it->get<std::string>())
                                                 : std::nullopt;
    };
    const auto log2_n = number("log2_n");
    const auto r = number("r");
    const auto p = number("p");
    const auto salt = bytes("salt");
    const auto nonce = bytes("nonce");
    const auto sealed = bytes("sealed");
    if (!log2_n || !r || !p || !salt || !nonce || !sealed || salt->size() != k_salt_size
        || nonce->size() != k_nonce_size || sealed->size() < k_tag_size) {
        return std::nullopt;
    }
    return LockedBox{{*log2_n, *r, *p}, *salt, *nonce, *sealed};
}

LockedBox lock(std::string_view secret, std::string_view passphrase, BoxParams params)
{
    LockedBox box{params, random_bytes(k_salt_size), random_bytes(k_nonce_size), {}};
    std::string key = derive_key(passphrase, box);
    const Wiped wiped{key};

    Owned<EVP_CIPHER_CTX> ctx(EVP_CIPHER_CTX_new());
    openssl::check(EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
                                      reinterpret_cast<const unsigned char*>(key.data()),
                                      reinterpret_cast<const unsigned char*>(box.nonce.data())),
                   "The secret could not be locked");
    box.sealed.resize(secret.size() + k_tag_size);
    auto* out = reinterpret_cast<unsigned char*>(box.sealed.data());
    int n = 0;
    openssl::check(EVP_EncryptUpdate(ctx.get(), out, &n,
                                     reinterpret_cast<const unsigned char*>(secret.data()),
                                     static_cast<int>(secret.size())),
                   "The secret could not be locked");
    int tail = 0;
    openssl::check(EVP_EncryptFinal_ex(ctx.get(), out + n, &tail), "The secret could not be locked");
    openssl::check(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(k_tag_size),
                                       out + n + tail),
                   "The secret could not be locked");
    box.sealed.resize(static_cast<std::size_t>(n + tail) + k_tag_size);
    return box;
}

std::optional<std::string> unlock(const LockedBox& box, std::string_view passphrase)
{
    if (box.salt.size() != k_salt_size || box.nonce.size() != k_nonce_size
        || box.sealed.size() < k_tag_size) {
        return std::nullopt;
    }
    std::string key = derive_key(passphrase, box);
    const Wiped wiped{key};

    Owned<EVP_CIPHER_CTX> ctx(EVP_CIPHER_CTX_new());
    openssl::check(EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
                                      reinterpret_cast<const unsigned char*>(key.data()),
                                      reinterpret_cast<const unsigned char*>(box.nonce.data())),
                   "The locked box could not be opened");
    const std::size_t body = box.sealed.size() - k_tag_size;
    std::string secret(body, '\0');
    int n = 0;
    openssl::check(EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(secret.data()),
                                     &n, reinterpret_cast<const unsigned char*>(box.sealed.data()),
                                     static_cast<int>(body)),
                   "The locked box could not be opened");
    std::string tag = box.sealed.substr(body);
    openssl::check(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(k_tag_size),
                                       tag.data()),
                   "The locked box could not be opened");
    int tail = 0;
    const int ok = EVP_DecryptFinal_ex(ctx.get(),
                                       reinterpret_cast<unsigned char*>(secret.data()) + n, &tail);
    if (ok <= 0) {
        ERR_clear_error();
        OPENSSL_cleanse(secret.data(), secret.size());
        return std::nullopt;  // the wrong passphrase, or a box that was changed
    }
    secret.resize(static_cast<std::size_t>(n + tail));
    return secret;
}

}  // namespace seventhslide::crypto
