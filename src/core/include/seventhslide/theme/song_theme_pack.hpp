#pragma once

// A song's theme arrangements, which are the whole of its ``theme_map``:
//
//   {"active": "a1",
//    "arrangements": [{"id": "a1", "name": "Default", "order": "TH1 V1 TH2 C1",
//                      "chips": {"TH1": {<output id>: {"song": {"theme": id,
//                                                                "bg": id}}}}}]}
//
// Theme chips (TH1, TH2, ...) in an arrangement's play order switch themes from
// that point on; each chip maps outputs to the themes it selects. ``{}`` means
// the song picks no themes of its own.

#include "seventhslide/theme/theme_picks.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

/// The name a song's first arrangement is stored under until the operator
/// names it. Stored as is in every language: the editor shows it translated.
inline constexpr std::string_view k_default_arrangement_name = "Default";

/// One run of the play order under a single theme chip (or inherit before the first).
struct ThemeChipSegment {
    /// Canonical ``TH1`` / empty when this run inherits (no chip yet).
    std::string chip_id;
    /// Verse-order substring without theme tokens (may be empty).
    std::string verse_order;
};

/// One of a song's arrangements: a named play order and the themes its chips pick.
struct SongArrangement {
    /// Never empty.
    std::string id;
    std::string name;
    std::string order;
    /// By canonical chip id (``TH1``), each holding song picks only. A chip
    /// that picks nothing is kept, empty.
    std::map<std::string, OutputThemeMap, std::less<>> chips;

    [[nodiscard]] bool operator==(const SongArrangement&) const = default;
};

/**
 * A song's ``theme_map``, read.
 *
 * Arrangements without an id are dropped, chip ids are canonical, chip maps are
 * read as ``OutputThemeMap``, and ``active`` names an arrangement that exists.
 * Empty (no arrangements) when the song picks no themes of its own.
 */
class SongThemeMap {
public:
    SongThemeMap() = default;

    /// ``j`` read as above. Anything malformed is dropped, not refused: a song
    /// with an unreadable theme map plays with the themes it inherits.
    [[nodiscard]] static SongThemeMap from_json(const nlohmann::json& j);
    /// ``{}`` when empty.
    [[nodiscard]] nlohmann::json to_json() const;

    [[nodiscard]] bool empty() const noexcept { return arrangements_.empty(); }
    [[nodiscard]] const std::string& active() const noexcept { return active_; }
    [[nodiscard]] const std::vector<SongArrangement>& arrangements() const noexcept
    {
        return arrangements_;
    }

    /// The picks chip ``chip_id`` (any case) makes in the active arrangement.
    /// Empty (inherit) for an empty or unknown chip, or a chip that picks nothing.
    [[nodiscard]] OutputThemeMap chip(std::string_view chip_id) const;

    [[nodiscard]] bool operator==(const SongThemeMap&) const = default;

private:
    std::string active_;
    std::vector<SongArrangement> arrangements_;
};

/// The map the song editor's ``arrangements`` (``[{id, name, order, chips}]``,
/// the active one already up to date) store, with ``active`` the one selected.
/// Empty when there is only the default arrangement, unnamed, and its order has
/// no theme chips: such a song picks no themes of its own. Otherwise they are
/// read as ``SongThemeMap::from_json`` reads them, an arrangement without an id
/// numbered by its place (``a1``, ``a2``, ...) and one without a name given
/// ``k_default_arrangement_name``.
[[nodiscard]] SongThemeMap pack_song_theme_map(const nlohmann::json& arrangements,
                                               std::string_view active);

[[nodiscard]] bool verse_order_has_theme_chips(std::string_view verse_order) noexcept;

/// Split ``TH1 T1 V1 TH2 C1`` into ``[{TH1,"t1 v1"},{TH2,"c1"}]``.
[[nodiscard]] std::vector<ThemeChipSegment> split_order_by_theme_chips(
    std::string_view verse_order);

/// ``th1`` / ``TH1`` -> ``TH1``; anything else -> empty.
[[nodiscard]] std::string canonical_theme_chip_id(std::string_view token);

}  // namespace seventhslide::theme
