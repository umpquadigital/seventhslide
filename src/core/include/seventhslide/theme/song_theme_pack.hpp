#pragma once

// A song's theme arrangements, which are the whole of its ``theme_map``:
//
//   {"active": "a1",
//    "arrangements": [{"id": "a1", "name": "Default", "order": "TH1 V1 TH2 C1",
//                      "chips": {"TH1": {<output id>: {"song": {"theme": id,
//                                                                "bg": id}}}}}]}
//
// Theme chips (TH1, TH2, ...) in an arrangement's play order switch themes from
// that point on; each chip maps outputs to the themes it selects. A theme a
// chip leaves unset is the one the chip before it in the play order played
// with ("Previous"); ``k_inherit_above`` leaves it to the next tier in the
// theme priority ("Above"), as does an unset theme on the first chip. ``{}``
// means the song picks no themes of its own.

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

/// One run of the play order and the picks it plays with.
struct ThemeRun {
    ThemeChipSegment segment;
    /// Its chip's picks with Previous and Above settled
    /// (``OutputThemeMap::following``): what the content tier picks while it
    /// plays. Empty for the run before the first chip.
    OutputThemeMap picks;
};

/// A song's theme chips, by canonical chip id (``TH1``), each holding song
/// picks only, with ``k_inherit_above`` where it says Above. A chip that
/// picks nothing is kept, empty.
using ChipMaps = std::map<std::string, OutputThemeMap, std::less<>>;

/// ``j`` (``{TH1: {<output id>: picks}}``) read as a song stores its chips:
/// any case of a chip id is the one chip, anything that is not one is dropped.
[[nodiscard]] ChipMaps chip_maps_from_json(const nlohmann::json& j);

/// ``verse_order`` split at its theme chips (``split_order_by_theme_chips``),
/// each run with what it plays with under ``chips``: its chip's picks laid
/// over those of the chip before it in the order.
[[nodiscard]] std::vector<ThemeRun> theme_runs(const ChipMaps& chips, std::string_view verse_order);

/// What the theme change at token ``place`` of ``verse_order`` takes for
/// Previous under ``chips``: what the change before it played with. Empty
/// when no change comes before it.
[[nodiscard]] OutputThemeMap previous_chip_picks(const ChipMaps& chips,
                                                 std::string_view verse_order,
                                                 std::size_t place);

/// One of a song's arrangements: a named play order and the themes its chips pick.
struct SongArrangement {
    /// Never empty.
    std::string id;
    std::string name;
    std::string order;
    ChipMaps chips;

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

    /// The picks chip ``chip_id`` (any case) makes in the active arrangement,
    /// as stored. Not what it plays with: it holds ``k_inherit_above`` for
    /// Above and nothing for Previous, which ``runs`` settles. Empty for an
    /// empty or unknown chip, or a chip that picks nothing.
    [[nodiscard]] OutputThemeMap stored_chip(std::string_view chip_id) const;

    /// ``theme_runs`` of ``verse_order`` under the active arrangement's chips.
    [[nodiscard]] std::vector<ThemeRun> runs(std::string_view verse_order) const;

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
