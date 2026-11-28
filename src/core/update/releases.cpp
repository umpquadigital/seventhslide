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
    // The bracket that closes this one, past any inside it: a badge is a
    // picture inside a link, [![build](badge.svg)](page).
    std::size_t close = std::string_view::npos;
    int depth = 0;
    for (std::size_t j = at; j < s.size(); ++j) {
        if (s[j] == '[') {
            ++depth;
        } else if (s[j] == ']' && --depth == 0) {
            close = j;
            break;
        }
    }
    if (close == std::string_view::npos || close + 1 >= s.size() || s[close + 1] != '(') {
        return std::nullopt;
    }
    std::size_t end = std::string_view::npos;
    depth = 0;
    for (std::size_t j = close + 1; j < s.size(); ++j) {
        if (s[j] == '(') {
            ++depth;
        } else if (s[j] == ')' && --depth == 0) {
            end = j;
            break;
        }
    }
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

/// ``text`` with each link to ``repo``'s own pull request or issue as its
/// number (``#41``), and each comparing two of its versions as the two.
std::string shorten_links(std::string_view text, const Repository& repo)
{
    constexpr std::string_view host = "https://github.com/";
    std::string out;
    while (true) {
        const auto at = text.find(host);
        out.append(text.substr(0, at));
        if (at == std::string_view::npos) {
            return out;
        }
        auto end = text.find_first_of(" \t\n)>]\"'", at);
        end = end == std::string_view::npos ? text.size() : end;
        // A sentence's own stop after a link is not the link's.
        while (end > at && std::string_view{".,;:!?"}.find(text[end - 1]) != std::string_view::npos) {
            --end;
        }
        const std::string_view url = text.substr(at, end - at);
        const auto parts = split(url.substr(host.size()), '/');
        std::string shown{url};
        if (parts.size() == 4 && util::iequals(parts[0], repo.owner)
            && util::iequals(parts[1], repo.name)) {
            if ((parts[2] == "pull" || parts[2] == "issues") && all_digits(parts[3])) {
                shown = "#" + std::string(parts[3]);
            } else if (parts[2] == "compare" && parts[3].find("...") != std::string_view::npos) {
                shown = std::string(parts[3]);
            }
        }
        out += shown;
        text.remove_prefix(end);
    }
}

/// How far a line is indented, a tab counting four.
int indent_of(std::string_view line)
{
    int n = 0;
    for (const char c : line) {
        if (c == ' ') {
            ++n;
        } else if (c == '\t') {
            n += 4;
        } else {
            break;
        }
    }
    return n;
}

/// A numbered item's number and what follows it (``2. Text``, ``2) Text``).
std::optional<std::pair<std::string_view, std::string_view>> numbered(std::string_view body)
{
    const auto digits = body.find_first_not_of("0123456789");
    if (digits == 0 || digits == std::string_view::npos || digits > 9 || digits + 1 >= body.size()
        || (body[digits] != '.' && body[digits] != ')') || body[digits + 1] != ' ') {
        return std::nullopt;
    }
    return std::pair{body.substr(0, digits), body.substr(digits + 2)};
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
                                const Version& running)
{
    const Release* newest = newest_release(releases, channel);
    if (newest == nullptr || newest->version <= running) {
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
        // Waiting a while for the lock that unattended upgrades may hold.
        return {"/usr/bin/pkexec", "/usr/bin/apt-get", "install", "--yes",
                "-o", "DPkg::Lock::Timeout=120", std::string(path)};
    case Installer::rpm:
        return {"/usr/bin/pkexec", "/usr/bin/dnf", "install", "--assumeyes", std::string(path)};
    case Installer::none:
    case Installer::windows_setup:
    case Installer::disk_image:
        break;
    }
    return {};
}

std::vector<NotesBlock> notes_blocks(std::string_view markdown, const Repository& repo)
{
    using Kind = NotesBlock::Kind;
    const std::string text = without_comments(markdown);
    std::vector<NotesBlock> blocks;
    // The last block takes the next line as more of itself: a paragraph's
    // or a list item's line wrapped in the Markdown.
    bool open = false;
    // The fence a code block opened with, ``` or ~~~; empty outside one.
    std::string_view fence;
    // How far each list item around the next line is indented, outermost
    // first: an item indented past one is inside it, however far.
    std::vector<int> items;
    for (auto line : split(text, '\n')) {
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::string_view trimmed = util::trim_view(line);
        if (!fence.empty()) {
            if (trimmed.starts_with(fence)) {
                fence = {};
            } else {
                std::string& code = blocks.back().text;
                code += code.empty() ? "" : "\n";
                code += line;
            }
            continue;
        }
        if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
            fence = trimmed.substr(0, 3);
            blocks.push_back({Kind::code, {}, {}, 0});
            open = false;
            continue;
        }
        if (trimmed.empty()) {
            open = false;
            continue;
        }
        // A line of = or - under a paragraph makes the paragraph a heading.
        if (open && blocks.back().kind == Kind::paragraph
            && trimmed.find_first_not_of(trimmed.front() == '=' ? '=' : '-') == std::string_view::npos
            && (trimmed.front() == '=' || trimmed.front() == '-')) {
            blocks.back().kind = Kind::heading;
            items.clear();
            open = false;
            continue;
        }
        if (is_rule(trimmed)) {
            open = false;
            continue;
        }
        std::string_view body = trimmed;
        while (body.starts_with('>')) {
            body = util::trim_view(body.substr(1));
        }
        const auto hashes = std::min(body.find_first_not_of('#'), body.size());
        if (hashes > 0 && hashes <= 6 && (hashes == body.size() || body[hashes] == ' ')) {
            blocks.push_back({Kind::heading, inline_text(body.substr(hashes)), {}, 0});
            items.clear();
            open = false;
            continue;
        }
        const bool bullet =
            body.size() > 1 && (body[0] == '-' || body[0] == '*' || body[0] == '+') && body[1] == ' ';
        const auto number = bullet ? std::nullopt : numbered(body);
        if (bullet || number) {
            const int indent = indent_of(line);
            while (!items.empty() && items.back() >= indent) {
                items.pop_back();
            }
            const int depth = std::min(static_cast<int>(items.size()), 3);
            items.push_back(indent);
            blocks.push_back(bullet ? NotesBlock{Kind::item, inline_text(body.substr(2)), "\u2022", depth}
                                    : NotesBlock{Kind::item, inline_text(number->second),
                                                 std::string(number->first) + ".", depth});
            open = true;
            continue;
        }
        if (open) {
            blocks.back().text += ' ';
            blocks.back().text += inline_text(body);
        } else {
            blocks.push_back({Kind::paragraph, inline_text(body), {}, 0});
            if (indent_of(line) == 0) {
                items.clear();
            }
            open = true;
        }
    }
    std::vector<NotesBlock> out;
    for (auto& block : blocks) {
        if (block.kind != Kind::code) {
            block.text = util::trim(shorten_links(block.text, repo));
        }
        if (!util::trim_view(block.text).empty()) {
            out.push_back(std::move(block));
        }
    }
    return out;
}

} // namespace seventhslide::update
