#include "seventhslide/freeform/document.hpp"

#include "seventhslide/json/read.hpp"

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::freeform {
namespace {
using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::str_or;

/// The typeface every kind that paints text stores (text, indicator, clock).
/// Weight, slant, underline and line metrics are each kind's own choice.
} // namespace

std::string_view to_string(SlideBackgroundKind kind) noexcept
{
    switch (kind) {
    case SlideBackgroundKind::Color:
        return "color";
    case SlideBackgroundKind::Image:
        return "image";
    case SlideBackgroundKind::Video:
        return "video";
    case SlideBackgroundKind::None:
        break;
    }
    return "none";
}

SlideBackgroundKind slide_background_kind_from_string(std::string_view s) noexcept
{
    if (s == "color") {
        return SlideBackgroundKind::Color;
    }
    if (s == "image") {
        return SlideBackgroundKind::Image;
    }
    if (s == "video") {
        return SlideBackgroundKind::Video;
    }
    return SlideBackgroundKind::None;
}

std::optional<parse::VerseRef> cue_verse_ref(const Cue& cue)
{
    if (cue.chapter <= 0) {
        return std::nullopt;
    }
    return parse::VerseRef{.chapter = cue.chapter, .verse = cue.verse};
}

std::string cue_label_code(const Cue& cue)
{
    if (const auto ref = cue_verse_ref(cue)) {
        return parse::format_verse_ref(*ref);
    }
    return parse::verse_code_from_label(cue.verse_type, cue.verse_number);
}

void to_json(nlohmann::json& j, const Cue& v)
{
    j = nlohmann::json{{"id", v.id}, {"text", v.text}};
    if (!v.verse_type.empty()) {
        j["verse_type"] = v.verse_type;
        j["verse_number"] = v.verse_number;
    }
    if (v.chapter > 0) {
        j["chapter"] = v.chapter;
        j["verse"] = v.verse;
        j["show_ref"] = v.show_ref;
    }
}

void from_json(const nlohmann::json& j, Cue& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.id = str_or(j, "id");
    v.text = str_or(j, "text");
    v.verse_type = parse::canonicalize_verse_type(str_or(j, "verse_type"));
    v.verse_number = parse::clamp_verse_number(int_or(j, "verse_number", parse::k_min_verse_number));
    // A verse out of range is no verse; one in range wins over a song label.
    const int chapter = int_or(j, "chapter", 0);
    const int verse = int_or(j, "verse", 0);
    if (chapter >= 1 && chapter <= parse::k_max_bible_chapter && verse >= 1
        && verse <= parse::k_max_bible_verse) {
        v.chapter = chapter;
        v.verse = verse;
        v.show_ref = bool_or(j, "show_ref", false);
        v.verse_type.clear();
        v.verse_number = parse::k_min_verse_number;
    }
}

void to_json(nlohmann::json& j, const Object& v)
{
    canvas::to_json(j, v);
    if (!v.animations.empty()) {
        j["animations"] = v.animations;
    }
    if (v.type != ObjectType::Text) {
        return;
    }
    if (v.cue_linked) {
        j["cue_linked"] = true;
    }
    if (v.show_upcoming) {
        j["show_upcoming"] = true;
    }
    if (!v.upcoming_color.empty()) {
        j["upcoming_color"] = v.upcoming_color;
    }
    if (v.upcoming_font_size > 0) {
        j["upcoming_font_size"] = v.upcoming_font_size;
    }
    if (v.upcoming_bold) {
        j["upcoming_bold"] = *v.upcoming_bold;
    }
    if (v.upcoming_italic) {
        j["upcoming_italic"] = *v.upcoming_italic;
    }
    if (v.upcoming_underline) {
        j["upcoming_underline"] = *v.upcoming_underline;
    }
    if (v.show_chords) {
        j["show_chords"] = true;
    }
    if (v.chords != layout::ChordStyle{}) {
        j["chord_size_pct"] = v.chords.size_pct;
        j["chord_gap_pct"] = v.chords.gap_pct;
        j["chord_color"] = v.chords.color;
    }
    if (!v.verse_ref_look.empty()) {
        j["verse_ref_look"] = v.verse_ref_look;
    }
    if (!v.cue_ref_looks.empty()) {
        j["cue_ref_looks"] = v.cue_ref_looks;
    }
    if (!v.cue_overlays.empty()) {
        j["cue_overlays"] = v.cue_overlays;
    }
}

void from_json(const nlohmann::json& j, Object& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    canvas::from_json(j, v);
    if (const auto it = j.find("animations"); it != j.end() && it->is_array()) {
        v.animations = it->get<std::vector<canvas::Animation>>();
    }
    // Text layout's clamps, which the stored form does not apply.
    v.line_height = layout::clamp_overlay_line_height(v.line_height);
    v.line_gap = layout::clamp_overlay_line_gap(v.line_gap);
    v.letter_spacing = layout::clamp_overlay_letter_spacing(v.letter_spacing);
    v.cue_linked = bool_or(j, "cue_linked", false);
    v.show_upcoming = bool_or(j, "show_upcoming", false);
    v.upcoming_color = str_or(j, "upcoming_color");
    v.upcoming_font_size = std::max(0, int_or(j, "upcoming_font_size", 0));
    const auto flag = [&j](const char* key) -> std::optional<bool> {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_boolean()) {
            return std::nullopt;
        }
        return it->get<bool>();
    };
    v.upcoming_bold = flag("upcoming_bold");
    v.upcoming_italic = flag("upcoming_italic");
    v.upcoming_underline = flag("upcoming_underline");
    v.show_chords = bool_or(j, "show_chords", false);
    const layout::ChordStyle chords;
    v.chords = layout::clamped(layout::ChordStyle{
        .size_pct = int_or(j, "chord_size_pct", chords.size_pct),
        .gap_pct = int_or(j, "chord_gap_pct", chords.gap_pct),
        .color = str_or(j, "chord_color", chords.color),
    });
    v.verse_ref_look = str_or(j, "verse_ref_look");
    if (j.contains("cue_ref_looks") && j.at("cue_ref_looks").is_object()) {
        for (auto it = j.at("cue_ref_looks").begin(); it != j.at("cue_ref_looks").end(); ++it) {
            if (it.value().is_string()) {
                v.cue_ref_looks[it.key()] = it.value().get<std::string>();
            }
        }
    }
    if (j.contains("cue_overlays") && j.at("cue_overlays").is_object()) {
        for (auto it = j.at("cue_overlays").begin(); it != j.at("cue_overlays").end(); ++it) {
            if (it.value().is_string()) {
                v.cue_overlays[it.key()] = it.value().get<std::string>();
            }
        }
    }
}

void to_json(nlohmann::json& j, const SlideBackground& v)
{
    j = nlohmann::json{{"kind", std::string(to_string(v.kind))}};
    if (!v.color.empty()) {
        j["color"] = v.color;
    }
    if (!v.image.empty()) {
        j["image"] = v.image;
    }
    if (!v.video.empty()) {
        j["video"] = v.video;
    }
}

void from_json(const nlohmann::json& j, SlideBackground& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.kind = slide_background_kind_from_string(str_or(j, "kind", "none"));
    v.color = str_or(j, "color");
    v.image = str_or(j, "image");
    v.video = str_or(j, "video");
}

void to_json(nlohmann::json& j, const Slide& v)
{
    j = nlohmann::json{{"id", v.id}, {"objects", v.objects}};
    if (!v.background.is_none()) {
        j["background"] = v.background;
    }
    model::optional_transition_to_json(j, "transition", v.transition);
    model::optional_transition_to_json(j, "background_transition", v.background_transition);
    if (!v.notes.empty()) {
        j["notes"] = v.notes;
    }
}

void from_json(const nlohmann::json& j, Slide& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.id = str_or(j, "id");
    v.notes = str_or(j, "notes");
    if (j.contains("objects") && j["objects"].is_array()) {
        v.objects = j["objects"].get<std::vector<Object>>();
    }
    if (j.contains("background") && j["background"].is_object()) {
        v.background = j["background"].get<SlideBackground>();
    }
    if (j.contains("transition")) {
        v.transition = model::optional_transition_from_json(j["transition"]);
    }
    if (j.contains("background_transition")) {
        v.background_transition = model::optional_transition_from_json(j["background_transition"]);
    }
}

void to_json(nlohmann::json& j, const Deck& v)
{
    j = nlohmann::json{
        {"output", v.output_id},
        {"canvas", {{"width", v.canvas.width}, {"height", v.canvas.height}}},
        {"slides", v.slides},
        {"line_to_slide", v.line_to_slide},
    };
    if (v.hide_verse_refs) {
        j["hide_verse_refs"] = true;
    }
    if (!v.cue_flow.empty()) {
        auto& flow = j["cue_flow"] = nlohmann::json::array();
        for (const auto& cue : v.cue_flow) {
            nlohmann::json entry{{"id", cue.id}};
            if (cue.runs_on) {
                entry["runs_on"] = true;
            }
            if (cue.in_word) {
                entry["in_word"] = true;
            }
            if (cue.gap_above > 0) {
                entry["gap_above"] = cue.gap_above;
            }
            flow.push_back(std::move(entry));
        }
    }
}

void from_json(const nlohmann::json& j, Deck& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.output_id = str_or(j, "output");
    const auto& canvas = j.contains("canvas") && j.at("canvas").is_object() ? j.at("canvas")
                                                                           : nlohmann::json::object();
    v.canvas = geom::canvas_or_reference(int_or(canvas, "width", 0), int_or(canvas, "height", 0));
    // A deck stored as another's mirror ("mirror_of") holds the copy it was
    // kept in step with; it is read as that deck's own.
    if (j.contains("slides") && j["slides"].is_array()) {
        v.slides = j["slides"].get<std::vector<Slide>>();
    }
    if (j.contains("line_to_slide") && j["line_to_slide"].is_array()) {
        for (const auto& el : j["line_to_slide"]) {
            if (el.is_number()) {
                v.line_to_slide.push_back(static_cast<int>(std::lround(el.get<double>())));
            }
        }
    }
    v.hide_verse_refs = bool_or(j, "hide_verse_refs", false);
    if (j.contains("cue_flow") && j["cue_flow"].is_array()) {
        for (const auto& el : j["cue_flow"]) {
            if (el.is_object() && el.contains("id") && el["id"].is_string()) {
                v.cue_flow.push_back({el["id"].get<std::string>(),
                                      bool_or(el, "runs_on", false),
                                      bool_or(el, "in_word", false),
                                      std::max(0, int_or(el, "gap_above", 0))});
            }
        }
    }
}

nlohmann::json Document::to_json() const
{
    nlohmann::json fields_json = nlohmann::json::object();
    for (const auto& [k, val] : fields) {
        fields_json[k] = val;
    }
    nlohmann::json out = nlohmann::json{
        {"schema", std::string(k_schema)},
        {"schema_version", k_schema_version},
        {"name", name},
        {"line_control", line_control},
        {"next_id", next_id},
        {"cues", cues},
        {"fields", std::move(fields_json)},
        {"decks", decks},
    };
    model::optional_transition_to_json(out, "background_transition", background_transition);
    return out;
}

Document Document::from_json(const nlohmann::json& j)
{
    Document d;
    if (!j.is_object()) {
        return d;
    }
    d.name = str_or(j, "name");
    d.line_control = bool_or(j, "line_control", false);
    d.next_id = std::max(1, int_or(j, "next_id", 1));
    if (j.contains("cues") && j["cues"].is_array()) {
        d.cues = j["cues"].get<std::vector<Cue>>();
    }
    if (j.contains("fields") && j["fields"].is_object()) {
        for (auto it = j["fields"].begin(); it != j["fields"].end(); ++it) {
            if (it.value().is_string()) {
                d.fields[it.key()] = it.value().get<std::string>();
            } else if (it.value().is_number() || it.value().is_boolean()) {
                d.fields[it.key()] = it.value().dump();
            }
        }
    }
    if (j.contains("decks") && j["decks"].is_array()) {
        d.decks = j["decks"].get<std::vector<Deck>>();
    }
    if (j.contains("background_transition")) {
        d.background_transition = model::optional_transition_from_json(j["background_transition"]);
    }
    return d;
}

void to_json(nlohmann::json& j, const Document& v)
{
    j = v.to_json();
}

void from_json(const nlohmann::json& j, Document& v)
{
    v = Document::from_json(j);
}

} // namespace seventhslide::freeform
