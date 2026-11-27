#include "seventhslide/link/sync_settings.hpp"

#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <nlohmann/json.hpp>

#include <cctype>

namespace seventhslide::link {
namespace {

using nlohmann::json;

constexpr int k_file_format = 1;
constexpr const char* k_filename = "sync_settings.json";

}  // namespace

std::optional<std::int64_t> read_upload_cap(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty() || text == "none") {
        return 0;
    }
    // Megabits, to a thousandth: kilobits exactly, with no floating point.
    std::int64_t kbit = 0;
    std::size_t i = 0;
    bool digits = false;
    for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
        kbit = kbit * 10 + (text[i] - '0');
        digits = true;
        if (kbit > k_most_upload_cap_kbit) {
            return std::nullopt;
        }
    }
    kbit *= 1000;
    if (i < text.size() && (text[i] == '.' || text[i] == ',')) {
        ++i;
        std::int64_t place = 100;
        for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
            if (place == 0 && text[i] != '0') {
                return std::nullopt;  // finer than a kilobit
            }
            kbit += (text[i] - '0') * place;
            place /= 10;
            digits = true;
        }
    }
    if (!digits || i != text.size()) {
        return std::nullopt;
    }
    // Nought, however written, is none.
    if (kbit != 0 && (kbit < k_least_upload_cap_kbit || kbit > k_most_upload_cap_kbit)) {
        return std::nullopt;
    }
    return kbit;
}

std::string upload_cap_text(std::int64_t kbit)
{
    if (kbit <= 0) {
        return {};
    }
    auto out = std::to_string(kbit / 1000);
    if (const auto rest = kbit % 1000; rest != 0) {
        auto decimals = std::to_string(1000 + rest).substr(1);
        while (decimals.back() == '0') {
            decimals.pop_back();
        }
        out += "." + decimals;
    }
    return out;
}

SyncSettings SyncSettings::load(const std::filesystem::path& data)
{
    SyncSettings out;
    const auto text = util::read_file(data / k_filename);
    if (!text) {
        return out;
    }
    const auto j = json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return out;
    }
    if (const auto it = j.find("computer_name"); it != j.end() && it->is_string()) {
        out.computer_name = it->get<std::string>();
    }
    if (const auto it = j.find("picture"); it != j.end()) {
        out.picture = sync::picture_from_json(*it);
    }
    if (const auto it = j.find("addresses"); it != j.end() && it->is_array()) {
        for (const auto& a : *it) {
            if (a.is_string()) {
                out.addresses.push_back(a.get<std::string>());
            }
        }
    }
    if (const auto it = j.find("port"); it != j.end() && it->is_number_integer()
                                        && it->get<std::int64_t>() >= 0 && it->get<std::int64_t>() <= 65535) {
        out.port = static_cast<std::uint16_t>(it->get<std::int64_t>());
    }
    if (const auto it = j.find("discovery"); it != j.end() && it->is_boolean()) {
        out.discovery = it->get<bool>();
    }
    if (const auto it = j.find("sync_automatically"); it != j.end() && it->is_boolean()) {
        out.sync_automatically = it->get<bool>();
    }
    if (const auto it = j.find("share_address"); it != j.end() && it->is_boolean()) {
        out.share_address = it->get<bool>();
    }
    // One out of range, written by hand, is none: a cap of a few bits a second
    // would stop syncing.
    if (const auto it = j.find("upload_cap_kbit"); it != j.end() && it->is_number_integer()
                                                   && it->get<std::int64_t>() >= k_least_upload_cap_kbit
                                                   && it->get<std::int64_t>() <= k_most_upload_cap_kbit) {
        out.upload_cap_kbit = it->get<std::int64_t>();
    }
    return out;
}

bool SyncSettings::save(const std::filesystem::path& data) const
{
    json j{{"format", k_file_format},
           {"computer_name", computer_name},
           {"addresses", addresses},
           {"discovery", discovery},
           {"sync_automatically", sync_automatically},
           {"share_address", share_address},
           {"upload_cap_kbit", upload_cap_kbit}};
    if (port) {
        j["port"] = *port;
    }
    if (!picture.empty()) {
        j["picture"] = sync::picture_to_json(picture);
    }
    return util::write_file_atomically(data / k_filename, j.dump(2));
}

}  // namespace seventhslide::link
