#include "seventhslide/update/releases.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <tuple>
#include <utility>

namespace seventhslide::update {
namespace {

/// The parts of ``s`` between each ``sep``, empty ones included.
std::vector<std::string_view> split(std::string_view s, char sep)
{
    std::vector<std::string_view> out;
    while (true) {
        const auto at = s.find(sep);
        out.push_back(s.substr(0, at));
        if (at == std::string_view::npos) {
            return out;
        }
        s.remove_prefix(at + 1);
    }
}

bool all_digits(std::string_view s) noexcept
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return util::is_digit(c); });
}

/// A dotted identifier of a label or build metadata: letters, digits and
/// dashes, never empty.
bool is_identifier(std::string_view s) noexcept
{
    return !s.empty()
           && std::all_of(s.begin(), s.end(), [](char c) { return util::is_alnum(c) || c == '-'; });
}

/// A number with no leading zero, that fits an int.
std::optional<int> number_of(std::string_view s)
{
    if (!all_digits(s) || (s.size() > 1 && s.front() == '0')) {
        return std::nullopt;
    }
    int value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

/// One label identifier against another: numbers by value, below any word,
/// and words by their ASCII order.
std::strong_ordering compare_identifier(std::string_view a, std::string_view b)
{
    const bool a_number = all_digits(a);
    const bool b_number = all_digits(b);
    if (a_number && b_number) {
        // Neither has a leading zero, so the longer one is the larger, and
        // two of one length compare as text. No number is too big to order.
        if (a.size() != b.size()) {
            return a.size() <=> b.size();
        }
        return a <=> b;
    }
    if (a_number != b_number) {
        return a_number ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return a <=> b;
}

/// The lower-case hex of GitHub's ``sha256:<hex>`` digest, or empty.
std::string sha256_of(std::string_view digest)
{
    constexpr std::string_view prefix = "sha256:";
    if (!digest.starts_with(prefix)) {
        return {};
    }
    digest.remove_prefix(prefix.size());
    const bool hex = digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) {
                         return util::is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                     });
    return hex ? util::to_lower(digest) : std::string{};
}

std::int64_t size_of(const nlohmann::json& asset)
{
    const auto* v = json::find(asset, "size");
    return v != nullptr && v->is_number_integer() ? std::max<std::int64_t>(v->get<std::int64_t>(), 0)
                                                  : 0;
}

// ---- Release notes ------------------------------------------------------

/// ``text`` with every ``<!-- ... -->`` taken out; an unclosed one runs to
/// the end.
std::string without_comments(std::string_view text)
{
    std::string out;
    while (true) {
        const auto open = text.find("<!--");
        out.append(text.substr(0, open));
        if (open == std::string_view::npos) {
            return out;
        }
        const auto close = text.find("-->", open + 4);
        if (close == std::string_view::npos) {
            return out;
        }
        text.remove_prefix(close + 3);
    }
}

bool is_punct(char c) noexcept
{
    return c > ' ' && c < 0x7f && !util::is_alnum(c);
}

std::string inline_text(std::string_view s);

/// At ``s[at] == '['``: a ``[text](target)`` link's end, past the ``)``,
/// and its text. Nullopt when no link starts there.
std::optional<std::pair<std::size_t, std::string_view>> link_at(std::string_view s, std::size_t at)
{
    const auto close = s.find(']', at + 1);
    if (close == std::string_view::npos || close + 1 >= s.size() || s[close + 1] != '(') {
        return std::nullopt;
    }
    const auto end = s.find(')', close + 2);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{end + 1, s.substr(at + 1, close - at - 1)};
}

/// One line's inline Markdown taken out: emphasis, code marks, pictures,
/// links (their text kept), autolinks' brackets and backslash escapes.
std::string inline_text(std::string_view s)
{
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        const std::string_view rest = s.substr(i);
        if (c == '\\' && i + 1 < s.size() && is_punct(s[i + 1])) {
            out += s[i + 1];
            i += 2;
        } else if (rest.starts_with("**") || rest.starts_with("__") || rest.starts_with("~~")) {
            i += 2;
        } else if (c == '`') {
            ++i;
        } else if (c == '!' && i + 1 < s.size() && s[i + 1] == '[') {
            if (const auto link = link_at(s, i + 1)) {
                i = link->first;
            } else {
                out += c;
                ++i;
            }
        } else if (c == '[') {
            if (const auto link = link_at(s, i)) {
                out += inline_text(link->second);
                i = link->first;
            } else {
                out += c;
                ++i;
            }
        } else if (c == '<' && (rest.starts_with("<https://") || rest.starts_with("<http://"))
                   && rest.find('>') != std::string_view::npos) {
            const auto end = rest.find('>');
            out.append(rest.substr(1, end - 1));
            i += end + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

/// A line of only ``-``, ``*`` or ``_`` (three or more, spaces between
/// allowed): a horizontal rule.
bool is_rule(std::string_view line)
{
    const auto t = util::trim_view(line);
    if (t.size() < 3 || (t.front() != '-' && t.front() != '*' && t.front() != '_')) {
        return false;
    }
    const char mark = t.front();
    return std::count(t.begin(), t.end(), mark) >= 3
           && std::all_of(t.begin(), t.end(), [mark](char c) { return c == mark || c == ' '; });
}

/// One line of notes outside a code block, as plain text.
std::string line_text(std::string_view line)
{
    if (is_rule(line)) {
        return {};
    }
    const auto indent = line.find_first_not_of(" \t");
    if (indent == std::string_view::npos) {
        return {};
    }
    std::string out{line.substr(0, indent)};
    std::string_view body = line.substr(indent);
    while (body.starts_with('>')) {
        body.remove_prefix(1);
        body = util::trim_view(body);
    }
    const auto hashes = std::min(body.find_first_not_of('#'), body.size());
    if (hashes > 0 && hashes <= 6 && (hashes == body.size() || body[hashes] == ' ')) {
        body = util::trim_view(body.substr(hashes));
    } else if (body.size() > 1 && (body[0] == '-' || body[0] == '*' || body[0] == '+')
               && body[1] == ' ') {
        out += "• ";
        body.remove_prefix(2);
    }
    out += inline_text(body);
    while (!out.empty() && util::is_space(out.back())) {
        out.pop_back();
    }
    return out;
}

} // namespace

std::string Version::text() const
{
    std::string out = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    for (std::size_t i = 0; i < label.size(); ++i) {
        out += i == 0 ? '-' : '.';
        out += label[i];
    }
    return out;
}

std::strong_ordering operator<=>(const Version& a, const Version& b)
{
    if (const auto c = std::tie(a.major, a.minor, a.patch) <=> std::tie(b.major, b.minor, b.patch);
        c != 0) {
        return c;
    }
    // A final version comes after every beta of it.
    if (a.label.empty() || b.label.empty()) {
        return a.label.empty() <=> b.label.empty();
    }
    const auto n = std::min(a.label.size(), b.label.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (const auto c = compare_identifier(a.label[i], b.label[i]); c != 0) {
            return c;
        }
    }
    return a.label.size() <=> b.label.size();
}

std::optional<Version> parse_version(std::string_view text)
{
    if (text.starts_with('v') || text.starts_with('V')) {
        text.remove_prefix(1);
    }
    if (const auto plus = text.find('+'); plus != std::string_view::npos) {
        const auto build = split(text.substr(plus + 1), '.');
        if (!std::all_of(build.begin(), build.end(), is_identifier)) {
            return std::nullopt;
        }
        text = text.substr(0, plus);
    }
    const auto dash = text.find('-');
    const auto numbers = split(text.substr(0, dash), '.');
    if (numbers.size() != 3) {
        return std::nullopt;
    }
    const auto major = number_of(numbers[0]);
    const auto minor = number_of(numbers[1]);
    const auto patch = number_of(numbers[2]);
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    Version v{*major, *minor, *patch, {}};
    if (dash != std::string_view::npos) {
        for (const auto part : split(text.substr(dash + 1), '.')) {
            // A number in a label may be any size, but has no leading zero.
            if (!is_identifier(part) || (all_digits(part) && part.size() > 1 && part.front() == '0')) {
                return std::nullopt;
            }
            v.label.emplace_back(part);
        }
    }
    return v;
}

const ReleaseFile* Release::file(std::string_view name) const
{
    const auto it = std::find_if(files.begin(), files.end(),
                                 [name](const ReleaseFile& f) { return f.name == name; });
    return it == files.end() ? nullptr : &*it;
}

bool is_release_url(std::string_view url, const Repository& repo)
{
    constexpr std::string_view host = "https://github.com/";
    if (repo.owner.empty() || repo.name.empty() || !url.starts_with(host)) {
        return false;
    }
    const auto parts = split(url.substr(host.size()), '/');
    // owner / name / releases / at least one more part.
    if (parts.size() < 4 || !util::iequals(parts[0], repo.owner) || !util::iequals(parts[1], repo.name)
        || parts[2] != "releases") {
        return false;
    }
    return std::all_of(parts.begin() + 3, parts.end(), [](std::string_view part) {
        return !part.empty() && part != "." && part != ".."
               && std::all_of(part.begin(), part.end(), [](char c) {
                      return util::is_alnum(c) || c == '.' || c == '-' || c == '_';
                  });
    });
}

std::optional<std::vector<Release>> parse_releases(std::string_view json_text,
                                                   const Repository& repo)
{
    const nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (!j.is_array()) {
        return std::nullopt;
    }
    std::vector<Release> out;
    for (const auto& el : j) {
        if (!el.is_object() || json::bool_or(el, "draft", false)) {
            continue;
        }
        Release r;
        r.tag = json::str_or(el, "tag_name");
        const auto version = parse_version(r.tag);
        r.page_url = json::str_or(el, "html_url");
        if (!version || !is_release_url(r.page_url, repo)) {
            continue;
        }
        r.version = *version;
        r.title = json::str_or(el, "name");
        if (util::trim_view(r.title).empty()) {
            r.title = r.tag;
        }
        r.beta = json::bool_or(el, "prerelease", false) || version->is_beta();
        r.notes = json::str_or(el, "body");
        r.published_at = json::str_or(el, "published_at");
        for (const auto& asset : json::array_or_empty(el, "assets")) {
            ReleaseFile f;
            f.name = json::str_or(asset, "name");
            f.url = json::str_or(asset, "browser_download_url");
            // An upload still under way is "open"; GitHub lists it anyway.
            if (f.name.empty() || json::str_or(asset, "state", "uploaded") != "uploaded"
                || !is_release_url(f.url, repo) || !f.url.ends_with("/" + f.name)) {
                continue;
            }
            f.size = size_of(asset);
            f.sha256 = sha256_of(json::str_or(asset, "digest"));
            r.files.push_back(std::move(f));
        }
        out.push_back(std::move(r));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Release& a, const Release& b) { return a.version > b.version; });
    return out;
}

Channel default_channel(const Version& running) noexcept
{
    return running.is_beta() ? Channel::beta : Channel::stable;
}

const Release* newest_release(const std::vector<Release>& releases, Channel channel)
{
    const Release* best = nullptr;
    for (const auto& r : releases) {
        if ((channel == Channel::beta || !r.beta) && (best == nullptr || r.version > best->version)) {
            best = &r;
        }
    }
    return best;
}

const Release* release_to_offer(const std::vector<Release>& releases,
                                Channel channel,
                                const Version& running,
                                const std::optional<Version>& skipped)
{
    const Release* newest = newest_release(releases, channel);
    if (newest == nullptr || newest->version <= running || (skipped && newest->version == *skipped)) {
        return nullptr;
    }
    return newest;
}

Installer installer_for(std::string_view file_name) noexcept
{
    const auto ends = [file_name](std::string_view ext) {
        return file_name.size() > ext.size() && util::iequals(file_name.substr(file_name.size() - ext.size()), ext);
    };
    if (ends(".exe")) {
        return Installer::windows_setup;
    }
    if (ends(".dmg")) {
        return Installer::disk_image;
    }
    if (ends(".deb")) {
        return Installer::deb;
    }
    if (ends(".rpm")) {
        return Installer::rpm;
    }
    return Installer::none;
}

std::vector<std::string> install_command(Installer installer, std::string_view path)
{
    if (!path.starts_with('/')) {
        return {};
    }
    switch (installer) {
    case Installer::deb:
        return {"pkexec", "apt-get", "install", "--yes", std::string(path)};
    case Installer::rpm:
        return {"pkexec", "dnf", "install", "--assumeyes", std::string(path)};
    case Installer::none:
    case Installer::windows_setup:
    case Installer::disk_image:
        break;
    }
    return {};
}

std::string notes_text(std::string_view markdown)
{
    const std::string text = without_comments(markdown);
    std::vector<std::string> lines;
    bool in_code = false;
    for (auto line : split(text, '\n')) {
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (util::trim_view(line).starts_with("```")) {
            in_code = !in_code;
            continue;
        }
        lines.push_back(in_code ? std::string{line} : line_text(line));
    }
    std::string out;
    bool blank = false;
    for (const auto& line : lines) {
        if (line.empty()) {
            blank = !out.empty();
            continue;
        }
        if (!out.empty()) {
            out += blank ? "\n\n" : "\n";
        }
        out += line;
        blank = false;
    }
    return out;
}

} // namespace seventhslide::update
