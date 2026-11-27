#include "seventhslide/util/file_name.hpp"

#include "seventhslide/util/strings.hpp"

#include <array>

namespace seventhslide::util {

namespace {

/// Names Windows keeps for devices, whatever extension follows them.
[[nodiscard]] bool is_reserved_device_name(std::string_view name)
{
    const auto dot = name.find('.');
    const std::string_view base = trim_view(name.substr(0, dot));
    constexpr std::array<std::string_view, 4> plain{"con", "prn", "aux", "nul"};
    for (const auto word : plain) {
        if (iequals(base, word)) {
            return true;
        }
    }
    return base.size() == 4 && (starts_with_ci(base, "com") || starts_with_ci(base, "lpt"))
           && base[3] >= '1' && base[3] <= '9';
}

[[nodiscard]] std::string trim_name_ends(std::string_view s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '.')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '.')) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

[[nodiscard]] std::string portable_or_empty(std::string_view title)
{
    std::string out;
    out.reserve(title.size());
    for (const char ch : title) {
        const auto byte = static_cast<unsigned char>(ch);
        char put = ch;
        switch (ch) {
        case ':':
            put = '.';
            break;
        case '/':
        case '\\':
        case '|':
            put = '-';
            break;
        case '"':
            put = '\'';
            break;
        case '*':
        case '?':
        case '<':
        case '>':
            continue;
        default:
            if (byte < 0x20 || byte == 0x7F) {
                put = ' ';
            }
            break;
        }
        if (put == ' ' && (out.empty() || out.back() == ' ')) {
            continue;
        }
        out.push_back(put);
    }
    out = trim_name_ends(utf8_prefix(trim_name_ends(out), k_portable_name_max_bytes));
    if (!out.empty() && is_reserved_device_name(out)) {
        out += '_';
    }
    return out;
}

}  // namespace

std::string portable_file_name(std::string_view title, std::string_view fallback)
{
    auto name = portable_or_empty(title);
    if (name.empty()) {
        name = portable_or_empty(fallback);
    }
    return name.empty() ? std::string("Untitled") : name;
}

std::string unique_file_name(std::string_view stem,
                             std::string_view extension,
                             const std::unordered_set<std::string>& taken)
{
    std::string candidate = std::string(stem) + std::string(extension);
    for (int n = 2; taken.contains(to_lower(candidate)); ++n) {
        candidate = std::string(stem) + " " + std::to_string(n) + std::string(extension);
    }
    return candidate;
}

}  // namespace seventhslide::util
