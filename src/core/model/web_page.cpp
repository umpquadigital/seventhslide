#include "seventhslide/model/web_page.hpp"

#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace seventhslide::model {
namespace {

/// What follows ``scheme://`` up to the path: ``www.example.com``.
std::string_view host_of(std::string_view url)
{
    const auto sep = url.find("://");
    if (sep == std::string_view::npos) {
        return {};
    }
    auto rest = url.substr(sep + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (const auto at = rest.rfind('@'); at != std::string_view::npos) {
        rest.remove_prefix(at + 1);
    }
    return rest.substr(0, rest.find(':'));
}

/// ``text`` starts with a scheme and no ``//``: ``mailto:a@b``,
/// ``javascript:…``. A host and port (``localhost:8080``) is not one.
bool names_scheme(std::string_view text)
{
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0
        || !std::isalpha(static_cast<unsigned char>(text.front()))) {
        return false;
    }
    const auto scheme = text.substr(0, colon);
    if (!std::ranges::all_of(scheme, [](unsigned char c) {
            return std::isalnum(c) || c == '+' || c == '-' || c == '.';
        })) {
        return false;
    }
    auto rest = text.substr(colon + 1);
    const auto port = rest.substr(0, rest.find_first_of("/?#"));
    const bool is_port = !port.empty() && std::ranges::all_of(port, [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
    return !is_port;
}

/// ``path`` as a file URL's path: what an address reads otherwise (``%``,
/// ``?``, ``#``, a space) escaped, so the file is the one named.
std::string url_path(std::string_view path)
{
    std::string out;
    out.reserve(path.size());
    for (const char c : path) {
        switch (c) {
        case '%': out += "%25"; break;
        case '?': out += "%3F"; break;
        case '#': out += "%23"; break;
        case ' ': out += "%20"; break;
        default: out += c; break;
        }
    }
    return out;
}

/// A Windows path's place in a file URL: a drive's ``C:\Users\me\wall.html``
/// (either slash) is ``/C:/Users/me/wall.html``, a share's
/// ``\\server\share\wall.html`` is ``//server/share/wall.html``; empty when
/// ``t`` is neither.
std::string windows_path(std::string_view t)
{
    std::string out;
    if (t.size() >= 3 && std::isalpha(static_cast<unsigned char>(t[0])) != 0 && t[1] == ':'
        && (t[2] == '\\' || t[2] == '/')) {
        out = "/" + std::string(t);
    } else if (t.size() >= 3 && t.starts_with("\\\\") && t[2] != '\\' && t[2] != '/') {
        out = std::string(t);
    }
    std::ranges::replace(out, '\\', '/');
    return out;
}

} // namespace

std::optional<std::string> web_url_from_text(std::string_view text)
{
    const auto t = util::trim_view(text);
    const auto windows = windows_path(t);
    // Words are not an address; a path may have spaces in it (not tabs or lines).
    const bool path = (!t.empty() && t.front() == '/') || !windows.empty();
    if (t.empty() || std::ranges::any_of(t, [path](unsigned char c) {
            return std::isspace(c) != 0 && !(path && c == ' ');
        })) {
        return std::nullopt;
    }
    std::string url;
    if (!windows.empty()) {
        // "C:\Users\me\wall.html", "\\server\share\wall.html": a file on
        // Windows, on a drive or a share (whose server is the URL's host).
        url = (windows.starts_with("//") ? "file:" : "file://") + url_path(windows);
    } else if (path) {
        // "/home/me/wall.html": a file on this computer.
        url = "file://" + url_path(t);
    } else if (t.front() == '\\') {
        // "\\", "\\\server": a share's path with no server; no site's address
        // starts so.
        return std::nullopt;
    } else if (const auto sep = t.find("://"); sep != std::string_view::npos) {
        const auto scheme = util::to_lower(t.substr(0, sep));
        if (scheme != "http" && scheme != "https" && scheme != "file") {
            return std::nullopt;
        }
        url = scheme + std::string(t.substr(sep));
    } else if (names_scheme(t)) {
        // "javascript:…", "mailto:…": not a page.
        return std::nullopt;
    } else {
        // "example.com/page": a site's address without its scheme.
        url = "https://" + std::string(t);
    }
    // A web address names its site; a file's may leave it out, not its path.
    const bool file = url.starts_with("file:");
    if (file ? url.size() == std::string_view("file://").size() : host_of(url).empty()) {
        return std::nullopt;
    }
    return url;
}

WebPage normalized(WebPage page)
{
    page.width = std::clamp(page.width, k_web_min_side, k_web_max_side);
    page.height = std::clamp(page.height, k_web_min_side, k_web_max_side);
    page.zoom = std::isfinite(page.zoom) ? std::clamp(page.zoom, k_web_min_zoom, k_web_max_zoom)
                                         : 1.0;
    page.name = util::trim(page.name);
    if (page.name.empty()) {
        const auto host = host_of(page.url);
        page.name = host.empty() ? page.url : std::string(host);
    }
    return page;
}

nlohmann::json web_item_data(const WebPage& page)
{
    nlohmann::json j = {{"name", page.name},
                        {"url", page.url},
                        {"width", page.width},
                        {"height", page.height},
                        {"zoom", page.zoom}};
    if (page.id) {
        j["page_id"] = *page.id;
    }
    return j;
}

WebPage web_page_from_item_data(const nlohmann::json& data)
{
    WebPage page;
    if (!data.is_object()) {
        return page;
    }
    if (const auto id = data.find("page_id"); id != data.end() && id->is_number_integer()) {
        page.id = id->get<std::int64_t>();
    }
    if (const auto url = data.find("url"); url != data.end() && url->is_string()) {
        page.url = web_url_from_text(url->get<std::string>()).value_or(std::string());
    }
    if (const auto name = data.find("name"); name != data.end() && name->is_string()) {
        page.name = name->get<std::string>();
    }
    if (const auto w = data.find("width"); w != data.end() && w->is_number()) {
        page.width = w->get<int>();
    }
    if (const auto h = data.find("height"); h != data.end() && h->is_number()) {
        page.height = h->get<int>();
    }
    if (const auto z = data.find("zoom"); z != data.end() && z->is_number()) {
        page.zoom = z->get<double>();
    }
    return normalized(std::move(page));
}

} // namespace seventhslide::model
