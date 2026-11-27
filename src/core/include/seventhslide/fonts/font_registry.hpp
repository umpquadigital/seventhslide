#pragma once

// Which font file a family names, from what the open library holds and nothing
// else: a slide looks the same whatever a computer has installed. A family the
// library has no font for is shown in a stand-in that SeventhSlide ships, and
// whoever listens is told, so an operator can be.
//
// The tables are handed in as plain data: this module knows nothing of libraries
// or databases (``app::FontController`` fills them). Any thread may look up.
//
// Seam: plain C++20 + std::filesystem (no Qt).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seventhslide::fonts {

/// One face of a family.
struct FontFaceEntry {
    std::string subfamily;
    std::filesystem::path path;
    /// The file holds other weights or styles along an axis.
    bool variable = false;

    bool operator==(const FontFaceEntry&) const = default;
};

/// A set of fonts, by lower-case family.
using FontIndex = std::unordered_map<std::string, std::vector<FontFaceEntry>>;

/// Classify a face subfamily string -> (is_bold, is_italic).
[[nodiscard]] std::pair<bool, bool> classify_subfamily(std::string_view sub);

/// The face of ``family`` nearest the request (or null).
[[nodiscard]] const FontFaceEntry* index_match(const FontIndex& index,
                                               std::string_view family,
                                               bool want_bold,
                                               bool want_italic);

/// Where a font came from.
enum class FontSource : std::uint8_t { Library, StandIn };

/// The file a family names.
struct FontMatch {
    std::filesystem::path path;
    FontSource source = FontSource::Library;
    bool variable = false;
};

class FontRegistry {
public:
    /// Told of a family looked up that is shown in a stand-in, and of the
    /// ``library_revision`` it was looked up in. From whichever thread looked it
    /// up, under this registry's lock, so it only passes the news on.
    using Listener = std::function<void(const std::string& family, std::uint64_t library_revision)>;

    /// The open library's fonts, in place of those there were.
    void set_library_fonts(FontIndex fonts);
    [[nodiscard]] FontIndex library_fonts() const;
    /// Goes up each time the library's fonts are replaced: news from before
    /// that is out of date.
    [[nodiscard]] std::uint64_t library_revision() const;
    /// The fonts the program ships, for the stand-in: always here, whatever
    /// has been taken out of the library or has yet to reach this computer.
    void set_shipped_fonts(FontIndex fonts);
    /// The family shown for one nobody has.
    void set_stand_in_family(std::string family);
    void set_listener(Listener listener);

    /// The file for ``family``: the library's, then the stand-in. Nullopt only
    /// when there are no fonts at all.
    [[nodiscard]] std::optional<FontMatch> find(std::string_view family, bool bold, bool italic) const;

    /// The stand-in for ``family``, whose own file turned out not to be usable:
    /// says so, as ``find`` does for one that was never there.
    [[nodiscard]] std::optional<FontMatch> stand_in_for(std::string_view family, bool bold,
                                                        bool italic) const;

private:
    [[nodiscard]] std::optional<FontMatch> stand_in_unlocked(bool bold, bool italic) const;

    mutable std::mutex mu_;
    FontIndex library_;
    std::uint64_t library_revision_ = 0;
    FontIndex shipped_;
    std::string stand_in_family_;
    Listener listener_;
};

}  // namespace seventhslide::fonts
