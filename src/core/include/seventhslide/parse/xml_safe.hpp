#pragma once

// Shared XML upload guards.
// Seam: plain C++20 (no Qt).

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace pugi {
class xml_document;
}

namespace seventhslide::parse {

/// True when ``data`` contains a ``<!DOCTYPE`` / ``<!doctype`` declaration
/// (case-insensitive ASCII scan). A comment that literally contains the
/// substring is also rejected.
[[nodiscard]] bool has_forbidden_doctype(std::string_view data) noexcept;

/// Throw ``Error`` when ``data`` declares a DOCTYPE (see ``has_forbidden_doctype``).
template <class Error>
void reject_doctype(std::string_view data)
{
    if (has_forbidden_doctype(data)) {
        throw Error("Failed to parse XML: DOCTYPE/DTD declarations are not allowed "
                    "in uploaded XML");
    }
}

/// The most elements deep an uploaded file may nest. Real songs and Bibles
/// nest a handful. The OpenLyrics reader walks the tree recursively, so a file
/// nested far deeper would overflow the stack; every reader of uploaded XML
/// holds to the same limit.
inline constexpr int k_max_xml_depth = 256;

/// True when an element of ``doc`` is more than ``k_max_xml_depth`` elements
/// deep (the document element is one deep). Walks without recursion.
[[nodiscard]] bool nests_too_deep(const pugi::xml_document& doc);

/// Throw ``Error`` when ``doc`` nests too deep (see ``nests_too_deep``). Every
/// reader of uploaded XML calls this after parsing, before it walks the tree.
template <class Error>
void reject_deep_nesting(const pugi::xml_document& doc)
{
    if (nests_too_deep(doc)) {
        throw Error("Failed to parse XML: elements are nested more than "
                    + std::to_string(k_max_xml_depth) + " deep");
    }
}

/// An element or attribute name without its namespace prefix.
[[nodiscard]] inline std::string_view xml_local_name(const char* name) noexcept
{
    const std::string_view s = name != nullptr ? name : "";
    const auto pos = s.rfind(':');
    return pos == std::string_view::npos ? s : s.substr(pos + 1);
}

/// Run ``parse``; any failure other than ``Error`` itself is rethrown as
/// ``Error`` so importers report one exception type.
template <class Error, class Fn>
auto with_xml_errors(Fn&& parse) -> decltype(parse())
{
    try {
        return parse();
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        throw Error(std::string("Failed to parse XML: ") + e.what());
    }
}

/// Read entire file as bytes. Throws ``std::runtime_error`` if the file cannot
/// be opened.
[[nodiscard]] std::string read_file_bytes(const std::filesystem::path& path);

}  // namespace seventhslide::parse
