#pragma once

// ThemeResolver - which text and background theme a slide uses: the item's
// (or song's), the service's or the output's category default, each for the
// category the content takes it from, tried in the operator's theme priority
// order.
// Seam: plain C++20.

#include "seventhslide/model/content_kind.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/model/theme_keys.hpp"
#include "seventhslide/theme/theme_picks.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

/// Explicit selection inputs for cascade. Session fills these; no global state.
/// What is playing (``kind``) says whose picks it takes (``theme_needs``).
struct ThemeSelection {
    model::ContentKind kind = model::ContentKind::Empty;
    /// Played from the open service's rundown.
    bool from_service = false;
    /// The open service's picks.
    OutputThemeMap service_themes;
    /// The content's own picks: a rundown item's (a Bible passage, a picture),
    /// or a song's for the part of it being resolved, what the theme chip it
    /// plays under plays with (``SongThemeMap::runs``). A song's are set only
    /// by the build, per run of the play order, from the song it lays out
    /// (``build::SlideBuildInput::song``).
    OutputThemeMap content_themes;
    /// Highest->lowest priority permutation of content/service/global (empty -> default).
    std::vector<std::string> theme_priority;
};

/// Resolved theme library ids for one output (empty string = unset).
struct ThemeIds {
    /// The text theme, of the category the content takes its theme from.
    std::string theme_id;
    /// The background theme, of the category the content takes its background from.
    std::string bg_id;

    [[nodiscard]] bool operator==(const ThemeIds&) const noexcept = default;
};

/// The themes ``sel`` takes on ``oc``: for each of ``theme_needs(sel.kind)``,
/// the first tier in the theme priority that picks one. Empty for what the
/// content needs none of.
[[nodiscard]] ThemeIds resolve_theme_ids(const model::OutputConfig& oc,
                                         const ThemeSelection& sel);

/// Find theme by id in a library list (nullptr if missing / empty id).
[[nodiscard]] const model::ThemeEntry* find_theme(
    const std::vector<model::ThemeEntry>& themes,
    std::string_view theme_id);

[[nodiscard]] const model::ThemeEntry* resolve_text_theme(const model::OutputConfig& oc,
                                                          const ThemeSelection& sel);

} // namespace seventhslide::theme
