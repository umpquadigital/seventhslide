#pragma once

// Names for the files the app writes outside the library (exports), made from
// titles people typed, which may hold anything.
//
// Plain C++20 (no Qt).

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace seventhslide::util {

/// The bytes ``portable_file_name`` keeps of a title, so a numbered prefix
/// and an extension still fit the 255 every common file system allows.
inline constexpr std::size_t k_portable_name_max_bytes = 160;

/**
 * ``title`` as a file or folder name that Windows, macOS and Linux all
 * accept, read as close to the title as they allow: ``:`` becomes ``.`` (so
 * "John 3:16" reads "John 3.16"), ``/ \ |`` become ``-``, ``"`` becomes
 * ``'``, ``* ? < >`` and control characters go; runs of spaces fold to one,
 * and spaces and dots are trimmed from the ends. A name Windows keeps for a
 * device (``CON``, ``NUL``, ``COM1``...) gets a ``_`` after it, and a name
 * longer than ``k_portable_name_max_bytes`` is cut on a character boundary.
 * ``fallback`` (itself made portable) when nothing is left.
 */
[[nodiscard]] std::string portable_file_name(std::string_view title,
                                             std::string_view fallback);

/// ``stem`` + ``extension`` (``".png"``, or empty) when ``taken``, a set of
/// lower-case names, does not hold it; otherwise the first of
/// ``stem 2``, ``stem 3``, ... + ``extension`` that it does not.
[[nodiscard]] std::string unique_file_name(std::string_view stem,
                                           std::string_view extension,
                                           const std::unordered_set<std::string>& taken);

}  // namespace seventhslide::util
