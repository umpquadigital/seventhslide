#pragma once

// Per-output configuration: canvas, theme libraries and the few settings that
// belong to the output rather than to a theme. The painted look is a
// ``theme::ResolvedTheme``, produced from those libraries.

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/model/audio_fades.hpp>
#include <seventhslide/model/theme_keys.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

/// The glyphs an output may show beside its name (Settings > Outputs, the
/// Output window). Values are stable ids; the art is the operator UI's.
inline constexpr std::string_view k_output_icons[] = {
    "display", "projector", "screen", "tv", "pulpit", "person", "mic",
    "users", "church", "laptop", "tablet", "phone", "video", "ndi",
    "bmd", "cast", "globe", "captions", "languages", "target",
};
inline constexpr std::string_view k_default_output_icon = "display";

/// ``icon`` when it is one of ``k_output_icons``, otherwise the default.
[[nodiscard]] std::string normalize_output_icon(std::string_view icon);

/// One entry in one of an output's theme libraries (song, Bible, media or
/// background themes).
struct ThemeEntry {
    std::string id;
    std::string name;
    nlohmann::json style = nlohmann::json::object();
    std::vector<std::string> tags;
    /// When it was made (local time, ``YYYY-MM-DDTHH:MM:SS``); empty when
    /// not known. Not written when empty.
    std::string created_at;
};

/// One of the theme lists an output stores: its key in the output's stored
/// data, and the name of the kind of theme it holds (``theme::ThemeKind``'s
/// ``to_string``).
struct OutputThemeList {
    std::string_view key;
    std::string_view kind;
};

/// Every theme list an output stores, in ``theme::k_theme_kinds`` order. For
/// what reads stored outputs without the theme module (a library-wide scan).
inline constexpr OutputThemeList k_output_theme_lists[] = {
    {"song_themes", "song"},
    {"bible_themes", "bible"},
    {"media_themes", "media"},
    {"bg_themes", "bg"},
};

/**
 * One audience output: identity, canvas, and the theme libraries it draws from.
 *
 * What it paints (boxes, fonts, fades, background) lives on the resolved text
 * and background themes, not on this struct. Everything here is saved; what the
 * operator does to the output during a show (blank, freeze) is live state that
 * ``session::Session`` holds by ``id``, as is whether the output is on, which
 * each computer keeps for itself (library::DestinationStore).
 */
struct OutputConfig {
    /// Stable identity. Everything that stores a reference to an output (song,
    /// service and item theme maps, style profiles, Freeform decks) keys it by
    /// this, never by ``name``, so renaming an output breaks nothing.
    std::string id;
    /// Operator-facing label; unique among outputs but free to change.
    std::string name = "";
    int canvas_width = geom::k_reference_canvas.width;
    int canvas_height = geom::k_reference_canvas.height;
    /// This output stays up when the operator blanks or freezes everything else.
    bool exempt_from_global_blank = false;
    bool exempt_from_global_freeze = false;
    /// One of ``k_output_icons``.
    std::string icon{k_default_output_icon};
    /// How an audio item's sound comes and goes here, unless its service row
    /// says otherwise (``resolve_audio_fades``). Not a theme's: sound has no
    /// look.
    AudioFades audio_fades{};
    /// The text themes of each category, and the background themes. Which
    /// list a theme is in is its kind (``theme::ThemeKind``).
    std::vector<ThemeEntry> song_themes = {};
    std::vector<ThemeEntry> bible_themes = {};
    std::vector<ThemeEntry> media_themes = {};
    std::vector<ThemeEntry> bg_themes = {};
    nlohmann::json category_defaults = nlohmann::json::object();
    /// Empty = all stacks enabled (Looks checklist). Order is the app-wide stack list.
    std::vector<std::string> enabled_stack_ids = {};
    /// The ``k_stack_type_keys`` this output leaves out, on whichever stack they
    /// play: they add no layer here and their sound is not carried. A
    /// theme's background is part of the song or slide it is behind, so a video
    /// background still shows with Videos left out. Empty = it shows them all.
    std::vector<std::string> disabled_content_types = {};

    [[nodiscard]] nlohmann::json to_persist_dict() const;

    [[nodiscard]] static OutputConfig from_dict(const nlohmann::json& d);
};

void to_json(nlohmann::json& j, const ThemeEntry& t);
void from_json(const nlohmann::json& j, ThemeEntry& t);

} // namespace seventhslide::model
