#include "seventhslide/sync/admin_key.hpp"

#include "seventhslide/crypto/encoding.hpp"

#include <stdexcept>

namespace seventhslide::sync {
namespace {

constexpr std::size_t k_min_passphrase = 12;

}  // namespace

std::optional<std::string> passphrase_problem(std::string_view passphrase)
{
    while (!passphrase.empty() && passphrase.front() == ' ') {
        passphrase.remove_prefix(1);
    }
    while (!passphrase.empty() && passphrase.back() == ' ') {
        passphrase.remove_suffix(1);
    }
    if (passphrase.size() < k_min_passphrase) {
        return "The admin password must be at least 12 characters long. A few ordinary words, "
               "with spaces, are easy to remember and hard to guess.";
    }
    return std::nullopt;
}

nlohmann::json LockedAdminKey::to_json() const
{
    return {{"admin", admin.base64()}, {"box", box.to_json()}};
}

std::optional<LockedAdminKey> LockedAdminKey::from_json(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return std::nullopt;
    }
    const auto admin = j.find("admin");
    const auto box = j.find("box");
    if (admin == j.end() || !admin->is_string() || box == j.end()) {
        return std::nullopt;
    }
    auto key = crypto::PublicKey::from_base64(admin->get<std::string>());
    auto locked = crypto::LockedBox::from_json(*box);
    if (!key || !locked) {
        return std::nullopt;
    }
    return LockedAdminKey{std::move(*key), std::move(*locked)};
}

LockedAdminKey lock_admin_key(const crypto::SigningKey& admin, std::string_view passphrase,
                              crypto::BoxParams params)
{
    return {admin.public_key(), crypto::lock(admin.seed(), passphrase, params)};
}

std::optional<crypto::SigningKey> unlock_admin_key(const LockedAdminKey& locked,
                                                   std::string_view passphrase)
{
    const auto seed = crypto::unlock(locked.box, passphrase);
    if (!seed) {
        return std::nullopt;
    }
    auto key = crypto::SigningKey::from_seed(*seed);
    if (!key || key->public_key() != locked.admin) {
        throw std::runtime_error("The library's locked admin key is damaged: what the password "
                                 "opens is not the admin key. Another computer's copy of the "
                                 "library may still have a good one.");
    }
    return key;
}

std::string recovery_code(const crypto::SigningKey& recovery)
{
    const auto flat = crypto::to_base32(recovery.seed());
    std::string out;
    for (std::size_t i = 0; i < flat.size(); i += 4) {
        if (!out.empty()) {
            out.push_back('-');
        }
        out += flat.substr(i, 4);
    }
    return out;
}

std::optional<crypto::SigningKey> key_from_recovery_code(std::string_view code)
{
    const auto seed = crypto::from_base32(code);
    return seed ? crypto::SigningKey::from_seed(*seed) : std::nullopt;
}

}  // namespace seventhslide::sync
