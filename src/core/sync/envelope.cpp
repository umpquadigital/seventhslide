#include "seventhslide/sync/envelope.hpp"

#include "seventhslide/crypto/encoding.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <stdexcept>

namespace seventhslide::sync {

const crypto::PublicKey* Envelope::signed_as(std::string_view as) const
{
    for (const auto& s : signatures) {
        if (s.as == as) {
            return &s.key;
        }
    }
    return nullptr;
}

std::string seal(const nlohmann::json& body,
                 const std::vector<std::pair<std::string_view, const crypto::SigningKey*>>& signers)
{
    const auto text = body.dump();
    nlohmann::json signatures = nlohmann::json::array();
    for (const auto& [as, key] : signers) {
        signatures.push_back({{"as", std::string(as)},
                              {"key", key->public_key().base64()},
                              {"signature", crypto::to_base64(key->sign(text))}});
    }
    // One line (a JSON dump writes no line break), then the body as it is:
    // a large file is not held twice.
    const nlohmann::json head{{"format", k_envelope_format}, {"signatures", std::move(signatures)}};
    return head.dump() + "\n" + text;
}

Envelope unseal(std::string_view text, std::string_view what)
{
    const auto malformed = [&](const std::string& why) -> std::runtime_error {
        return std::runtime_error(std::string(what) + " could not be read (" + why + ")");
    };
    const auto end_of_head = text.find('\n');
    const auto head = nlohmann::json::parse(text.substr(0, end_of_head), nullptr, /*allow_exceptions=*/false);
    if (!head.is_object()) {
        throw malformed("not a signed document");
    }
    const auto format = head.find("format");
    if (format == head.end() || !format->is_number_integer()) {
        throw malformed("no format");
    }
    if (format->get<std::int64_t>() > k_envelope_format) {
        throw NewerFormat();
    }
    if (format->get<std::int64_t>() < k_envelope_format || end_of_head == std::string_view::npos) {
        throw malformed("not a signed document");
    }
    const auto signatures = head.find("signatures");
    if (signatures == head.end() || !signatures->is_array() || signatures->empty()) {
        throw malformed("not signed");
    }
    const auto covered = text.substr(end_of_head + 1);
    Envelope out{nlohmann::json::parse(covered, nullptr, /*allow_exceptions=*/false), {}};
    if (!out.body.is_object()) {
        throw malformed("no body");
    }
    for (const auto& s : *signatures) {
        if (!s.is_object() || !s.value("as", nlohmann::json()).is_string()
            || !s.value("key", nlohmann::json()).is_string()
            || !s.value("signature", nlohmann::json()).is_string()) {
            throw malformed("a signature is not one");
        }
        auto key = crypto::PublicKey::from_base64(s["key"].get<std::string>());
        const auto signature = crypto::from_base64(s["signature"].get<std::string>());
        if (!key || !signature) {
            throw malformed("a signature is not one");
        }
        if (!key->verify(covered, *signature)) {
            throw std::runtime_error(std::string(what)
                                     + " was refused: its signature is not its key's. The file "
                                       "was changed after it was signed.");
        }
        out.signatures.push_back({s["as"].get<std::string>(), std::move(*key)});
    }
    return out;
}

}  // namespace seventhslide::sync
