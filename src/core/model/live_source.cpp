#include "seventhslide/model/live_source.hpp"

#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>

namespace seventhslide::model {

std::optional<std::string> stream_url_from_text(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty() || std::ranges::any_of(text, [](unsigned char c) { return std::isspace(c); })) {
        return std::nullopt;
    }
    const auto sep = text.find("://");
    if (sep == std::string_view::npos || sep == 0 || text.size() == sep + 3) {
        return std::nullopt;
    }
    const std::string scheme = util::to_lower(text.substr(0, sep));
    static constexpr std::string_view k_schemes[] = {"srt",  "rtmp", "rtmps", "rtsp", "rtsps",
                                                     "rist", "udp",  "rtp",   "http", "https"};
    if (std::ranges::find(k_schemes, scheme) == std::end(k_schemes)) {
        return std::nullopt;
    }
    return scheme + std::string(text.substr(sep));
}

namespace {

constexpr std::string_view k_hidden = "\u2022\u2022\u2022";

/// A query value that lets someone in: SRT's passphrase, a stream key, a
/// token or signature.
[[nodiscard]] bool secret_parameter(std::string_view name)
{
    static constexpr std::string_view k_secret[] = {
        "passphrase", "password", "pass",   "pwd",       "key",          "streamkey",
        "stream_key", "token",    "secret", "signature", "access_token", "auth"};
    const auto n = util::to_lower(name);
    return std::ranges::find(k_secret, n) != std::end(k_secret);
}

} // namespace

std::string stream_display_name(std::string_view url)
{
    const auto sep = url.find("://");
    if (sep == std::string_view::npos) {
        return std::string(url);
    }
    const auto scheme = util::to_lower(url.substr(0, sep));
    auto rest = url.substr(sep + 3);
    if (const auto hash = rest.find('#'); hash != std::string_view::npos) {
        rest = rest.substr(0, hash);
    }
    std::string_view query;
    if (const auto q = rest.find('?'); q != std::string_view::npos) {
        query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    const auto slash = rest.find('/');
    auto authority = rest.substr(0, slash);
    auto path = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);

    std::string out = scheme + "://";
    // user:password@host: the user stays, the password goes.
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
        const auto userinfo = authority.substr(0, at);
        const auto colon = userinfo.find(':');
        out += userinfo.substr(0, colon);
        if (colon != std::string_view::npos) {
            out += ":";
            out += k_hidden;
        }
        out += "@";
        authority = authority.substr(at + 1);
    }
    out += authority;
    // RTMP: /application/stream-key.
    if ((scheme == "rtmp" || scheme == "rtmps") && path.size() > 1) {
        const auto key = path.find('/', 1);
        if (key != std::string_view::npos && key + 1 < path.size()) {
            out += path.substr(0, key + 1);
            out += k_hidden;
            path = {};
        }
    }
    out += path;
    if (!query.empty()) {
        out += "?";
        bool first = true;
        while (!query.empty()) {
            const auto amp = query.find('&');
            const auto part = query.substr(0, amp);
            query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
            if (!first) {
                out += "&";
            }
            first = false;
            const auto eq = part.find('=');
            if (eq != std::string_view::npos && secret_parameter(part.substr(0, eq))) {
                out += part.substr(0, eq + 1);
                out += k_hidden;
            } else {
                out += part;
            }
        }
    }
    return out;
}

nlohmann::json live_item_data(const LiveSource& source)
{
    nlohmann::json j = {{"kind", source.kind}, {"name", source.name}};
    if (!source.url.empty()) {
        j["url"] = source.url;
    }
    return j;
}

LiveSource live_source_from_item_data(const nlohmann::json& data)
{
    LiveSource source;
    if (!data.is_object()) {
        return source;
    }
    const auto kind = data.find("kind");
    const auto name = data.find("name");
    if (kind == data.end() || !kind->is_string() || name == data.end() || !name->is_string()) {
        return source;
    }
    const auto k = kind->get<std::string>();
    // A stream is its address; its name is what the operator called it (the
    // address when nothing).
    if (k == k_live_kind_stream) {
        const auto url = data.find("url");
        const auto address = url != data.end() && url->is_string()
                                 ? stream_url_from_text(url->get<std::string>())
                                 : std::nullopt;
        if (!address) {
            return source;
        }
        source.kind = k;
        source.url = *address;
        // Named by hand, or else by its address, shown without its secrets. A
        // name that is the address itself (one typed in whole) is shown the
        // same way, so it gives away nothing the address would not.
        const auto& given = name->get_ref<const std::string&>();
        source.name = given.empty() || given == *address ? stream_display_name(*address) : given;
        return source;
    }
    if ((k != k_live_kind_ndi && k != k_live_kind_device && k != k_live_kind_decklink)
        || name->get<std::string>().empty()) {
        return source;
    }
    source.kind = kind->get<std::string>();
    source.name = name->get<std::string>();
    return source;
}

std::string live_source_key(const LiveSource& source)
{
    if (source.kind == k_live_kind_stream) {
        return source.url.empty() ? std::string() : source.kind + ":" + source.url;
    }
    return source.empty() ? std::string() : source.kind + ":" + source.name;
}

} // namespace seventhslide::model
