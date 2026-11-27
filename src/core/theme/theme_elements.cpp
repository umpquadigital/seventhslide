#include "seventhslide/theme/theme_elements.hpp"

#include "seventhslide/json/read.hpp"

#include "seventhslide/freeform/doc_edit.hpp"

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/transition.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::theme {
namespace {
using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::str_or;


/// An empty family or a size of 0 means "not set": use the fallback.
int size_or(const nlohmann::json& style, const char* key, int fallback)
{
    const int v = int_or(style, key, 0);
    return v > 0 ? v : fallback;
}

/// Clocks, indicators and countdowns size to their text, not to a box the
/// designer drew, so a new one gets a box big enough for what it paints and the
/// first drag does not clip anything.
constexpr double k_chrome_width_per_pt = 3.2;
constexpr double k_chrome_height_pad = 10.0;

}  // namespace

std::string_view to_string(ElementContext ctx) noexcept
{
    switch (ctx) {
    case ElementContext::Bible:
        return "bible";
    case ElementContext::Image:
        return "image";
    case ElementContext::Video:
        return "video";
    case ElementContext::Web:
        return "web";
    case ElementContext::Live:
        return "live";
    case ElementContext::Titles:
        return "titles";
    case ElementContext::Song:
        break;
    }
    return "song";
}

ElementContext element_context_from_string(std::string_view s) noexcept
{
    for (const auto ctx : k_element_contexts) {
        if (s == to_string(ctx)) {
            return ctx;
        }
    }
    return ElementContext::Song;
}

std::string_view to_string(ElementBinding binding) noexcept
{
    switch (binding) {
    case ElementBinding::Lyrics:
        return "lyrics";
    case ElementBinding::BibleText:
        return "bible_text";
    case ElementBinding::Indicator:
        return "indicator";
    case ElementBinding::Countdown:
        return "countdown";
    case ElementBinding::VideoArea:
        return "video_area";
    case ElementBinding::ImageArea:
        return "image_area";
    case ElementBinding::WebArea:
        return "web_area";
    case ElementBinding::Static:
        break;
    }
    return "static";
}

ElementBinding element_binding_from_string(std::string_view s) noexcept
{
    if (s == "lyrics") {
        return ElementBinding::Lyrics;
    }
    if (s == "bible_text") {
        return ElementBinding::BibleText;
    }
    if (s == "indicator") {
        return ElementBinding::Indicator;
    }
    if (s == "countdown") {
        return ElementBinding::Countdown;
    }
    if (s == "video_area") {
        return ElementBinding::VideoArea;
    }
    if (s == "image_area") {
        return ElementBinding::ImageArea;
    }
    if (s == "web_area") {
        return ElementBinding::WebArea;
    }
    return ElementBinding::Static;
}

bool binding_is_exclusive(ElementBinding binding) noexcept
{
    switch (binding) {
    case ElementBinding::Lyrics:
    case ElementBinding::BibleText:
    case ElementBinding::VideoArea:
    case ElementBinding::ImageArea:
    case ElementBinding::WebArea:
        return true;
    case ElementBinding::Static:
    case ElementBinding::Indicator:
    case ElementBinding::Countdown:
        break;
    }
    return false;
}

bool binding_fits_context(ElementContext ctx, ElementBinding binding) noexcept
{
    switch (binding) {
    case ElementBinding::Static:
        return true;
    case ElementBinding::Lyrics:
    case ElementBinding::Indicator:
        return ctx == ElementContext::Song;
    case ElementBinding::BibleText:
        return ctx == ElementContext::Bible;
    case ElementBinding::Countdown:
        return ctx == ElementContext::Video;
    case ElementBinding::VideoArea:
        return ctx == ElementContext::Video || ctx == ElementContext::Live;
    case ElementBinding::ImageArea:
        return ctx == ElementContext::Image;
    case ElementBinding::WebArea:
        return ctx == ElementContext::Web;
    }
    return false;
}

ObjectControls object_controls(freeform::ObjectType type, ElementBinding binding) noexcept
{
    using freeform::ObjectType;
    const bool text = type == ObjectType::Text;
    const bool clock = type == ObjectType::Clock;
    const bool indicator = type == ObjectType::Indicator;
    // The areas paint what the service puts there, not a frame of their own.
    const bool own_frame = binding == ElementBinding::Static
                           || binding == ElementBinding::Indicator
                           || binding == ElementBinding::Countdown
                           || binding == ElementBinding::Lyrics
                           || binding == ElementBinding::BibleText;
    const bool typed = text || indicator || clock || type == ObjectType::Table;
    ObjectControls out;
    out.runs = text;
    out.weight = text || clock;
    out.font = typed;
    out.color = typed;
    out.align = typed;
    out.fit = (type == ObjectType::Image || type == ObjectType::Video)
              && binding != ElementBinding::VideoArea;
    out.shape = type == ObjectType::Shape;
    out.clock = clock && binding != ElementBinding::Countdown;
    out.indicator = indicator;
    out.fill = own_frame && (type == ObjectType::Shape || text || indicator || clock);
    out.effects = own_frame
                  && (out.fill || type == ObjectType::Image || type == ObjectType::Video);
    return out;
}

std::string_view to_string(SlideRule rule) noexcept
{
    switch (rule) {
    case SlideRule::First:
        return "first";
    case SlideRule::Last:
        return "last";
    case SlideRule::ExceptFirst:
        return "except_first";
    case SlideRule::ExceptLast:
        return "except_last";
    case SlideRule::All:
        break;
    }
    return "all";
}

SlideRule slide_rule_from_string(std::string_view s) noexcept
{
    if (s == "first") {
        return SlideRule::First;
    }
    if (s == "last") {
        return SlideRule::Last;
    }
    if (s == "except_first") {
        return SlideRule::ExceptFirst;
    }
    if (s == "except_last") {
        return SlideRule::ExceptLast;
    }
    return SlideRule::All;
}

bool rule_covers_slide(const ElementVisibility& vis, int slide_index, int slide_count) noexcept
{
    if (vis.rule == SlideRule::All) {
        return true;
    }
    if (slide_count <= 0 || slide_index < 0) {
        return true;
    }
    const int n = std::clamp(vis.count, 1, 99);
    const bool in_first = slide_index < n;
    const bool in_last = slide_index >= slide_count - n;
    switch (vis.rule) {
    case SlideRule::First:
        return in_first;
    case SlideRule::Last:
        return in_last;
    case SlideRule::ExceptFirst:
        return !in_first;
    case SlideRule::ExceptLast:
        return !in_last;
    case SlideRule::All:
        break;
    }
    return true;
}

std::string_view to_string(ShowWhen when) noexcept
{
    switch (when) {
    case ShowWhen::SongCopyrightOn:
        return "song_copyright";
    case ShowWhen::Always:
        break;
    }
    return "always";
}

ShowWhen show_when_from_string(std::string_view s) noexcept
{
    if (s == "song_copyright") {
        return ShowWhen::SongCopyrightOn;
    }
    return ShowWhen::Always;
}

bool condition_met(ShowWhen when, const ElementConditions& cond) noexcept
{
    switch (when) {
    case ShowWhen::SongCopyrightOn:
        return cond.song_copyright;
    case ShowWhen::Always:
        break;
    }
    return true;
}

void to_json(nlohmann::json& j, const Element& v)
{
    freeform::to_json(j, v.object);
    if (v.binding != ElementBinding::Static) {
        j["binding"] = std::string(to_string(v.binding));
    }
    if (!v.label.empty()) {
        j["label"] = v.label;
    }
    if (v.visibility.rule != SlideRule::All) {
        j["slide_rule"] = std::string(to_string(v.visibility.rule));
        j["slide_count"] = v.visibility.count;
    }
    if (v.show_when != ShowWhen::Always) {
        j["show_when"] = std::string(to_string(v.show_when));
    }
    if (v.binding == ElementBinding::Lyrics || v.binding == ElementBinding::BibleText) {
        nlohmann::json body = nlohmann::json::object();
        if (v.binding == ElementBinding::Lyrics) {
            body["active_line"] = v.body.active_line;
            body["other_lines"] = v.body.other_lines;
            body["show_chords"] = v.body.show_chords;
            body["chord_size_pct"] = v.body.chords.size_pct;
            body["chord_gap_pct"] = v.body.chords.gap_pct;
            body["chord_color"] = v.body.chords.color;
        } else {
            body["verse_numbers"] = v.body.verse_numbers;
            body["verse_font_family"] = v.body.verse_font_family;
            body["verse_font_size"] = v.body.verse_font_size;
            body["verse_font_bold"] = v.body.verse_font_bold;
            body["verse_font_italic"] = v.body.verse_font_italic;
            body["verse_color"] = v.body.verse_color;
            body["verse_valign"] = v.body.verse_valign;
            body["verse_margin_right_pct"] = v.body.verse_margin_right_pct;
        }
        j["body"] = std::move(body);
    }
}

void from_json(const nlohmann::json& j, Element& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    freeform::from_json(j, v.object);
    v.binding = element_binding_from_string(str_or(j, "binding", "static"));
    v.label = str_or(j, "label");
    v.visibility.rule = slide_rule_from_string(str_or(j, "slide_rule", "all"));
    v.visibility.count = std::clamp(int_or(j, "slide_count", 1), 1, 99);
    v.show_when = show_when_from_string(str_or(j, "show_when", "always"));

    const BodyOptions defaults;
    const auto& body = j.contains("body") && j.at("body").is_object() ? j.at("body")
                                                                     : nlohmann::json::object();
    if (const auto* type = seventhslide::json::find(body, "active_line")) {
        v.body.active_line = type->get<canvas::LineType>();
    }
    if (const auto* type = seventhslide::json::find(body, "other_lines")) {
        v.body.other_lines = type->get<canvas::LineType>();
    }
    v.body.show_chords = bool_or(body, "show_chords", false);
    v.body.chords = layout::clamped(layout::ChordStyle{
        .size_pct = int_or(body, "chord_size_pct", defaults.chords.size_pct),
        .gap_pct = int_or(body, "chord_gap_pct", defaults.chords.gap_pct),
        .color = str_or(body, "chord_color", defaults.chords.color),
    });
    v.body.verse_numbers = bool_or(body, "verse_numbers", false);
    v.body.verse_font_family = str_or(body, "verse_font_family", defaults.verse_font_family);
    v.body.verse_font_size = size_or(body, "verse_font_size", defaults.verse_font_size);
    v.body.verse_font_bold = bool_or(body, "verse_font_bold", false);
    v.body.verse_font_italic = bool_or(body, "verse_font_italic", false);
    v.body.verse_color = str_or(body, "verse_color", defaults.verse_color);
    v.body.verse_valign = str_or(body, "verse_valign", defaults.verse_valign);
    v.body.verse_margin_right_pct =
        int_or(body, "verse_margin_right_pct", defaults.verse_margin_right_pct);
}

const std::vector<Element>& ElementSet::at(ElementContext ctx) const noexcept
{
    switch (ctx) {
    case ElementContext::Bible:
        return bible;
    case ElementContext::Image:
        return image;
    case ElementContext::Video:
        return video;
    case ElementContext::Web:
        return web;
    case ElementContext::Live:
        return live;
    case ElementContext::Titles:
        return titles;
    case ElementContext::Song:
        break;
    }
    return song;
}

std::vector<Element>& ElementSet::at(ElementContext ctx) noexcept
{
    return const_cast<std::vector<Element>&>(std::as_const(*this).at(ctx));
}

std::string ElementSet::mint_id()
{
    return "te" + std::to_string(next_id++);
}

nlohmann::json ElementSet::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    j["canvas"] = {{"width", canvas.width}, {"height", canvas.height}};
    for (const auto ctx : k_element_contexts) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& el : at(ctx)) {
            nlohmann::json ej;
            theme::to_json(ej, el);
            arr.push_back(std::move(ej));
        }
        j[std::string(to_string(ctx))] = std::move(arr);
    }
    j["next_id"] = next_id;
    return j;
}

ElementSet ElementSet::from_json(const nlohmann::json& j)
{
    ElementSet set;
    if (!j.is_object()) {
        return set;
    }
    if (const auto* c = seventhslide::json::find(j, "canvas"); c != nullptr && c->is_object()) {
        set.canvas = geom::canvas_or_reference(int_or(*c, "width", 0), int_or(*c, "height", 0));
    }
    int highest = 0;
    const auto read_list = [&highest](const nlohmann::json& arr) {
        std::vector<Element> list;
        for (const auto& ej : arr) {
            Element el;
            theme::from_json(ej, el);
            if (el.object.id.empty()) {
                continue;
            }
            if (el.object.id.rfind("te", 0) == 0) {
                // A hand-edited id need not parse; ids only have to be unique.
                if (const auto n = util::parse_leading_int(el.object.id.substr(2))) {
                    highest = std::max(highest, *n);
                }
            }
            list.push_back(std::move(el));
        }
        return list;
    };
    for (const auto ctx : k_element_contexts) {
        const auto key = std::string(to_string(ctx));
        if (!j.contains(key) || !j.at(key).is_array()) {
            continue;
        }
        set.at(ctx) = read_list(j.at(key));
    }
    set.next_id = std::max(int_or(j, "next_id", 1), highest + 1);
    return set;
}

const nlohmann::json& element_model(const nlohmann::json& style) noexcept
{
    static const nlohmann::json k_none = nlohmann::json::object();
    if (!style.is_object()) {
        return k_none;
    }
    const auto it = style.find(std::string(k_elements_key));
    return it != style.end() && it->is_object() ? *it : k_none;
}

bool has_elements(const nlohmann::json& style) noexcept
{
    // An output carries an empty ``{}`` here (only text themes have a design),
    // and that must not count as having a design.
    return !element_model(style).empty();
}

std::vector<Element> visible_elements(const std::vector<Element>& all,
                                      int slide_index,
                                      int slide_count,
                                      const ElementConditions& cond)
{
    std::vector<Element> out;
    out.reserve(all.size());
    for (const auto& el : all) {
        if (!rule_covers_slide(el.visibility, slide_index, slide_count)) {
            continue;
        }
        if (!condition_met(el.show_when, cond)) {
            continue;
        }
        out.push_back(el);
    }
    std::stable_sort(out.begin(), out.end(), [](const Element& a, const Element& b) {
        return a.object.z < b.object.z;
    });
    return out;
}

bool has_clock(const ElementSet& set)
{
    for (const auto ctx : k_element_contexts) {
        for (const auto& el : set.at(ctx)) {
            if (el.object.type == freeform::ObjectType::Clock && !el.object.id.empty()) {
                return true;
            }
        }
    }
    return false;
}

Element make_element(freeform::ObjectType type,
                     ElementBinding binding,
                     std::string id,
                     geom::CanvasSize canvas)
{
    // Designed on the reference canvas, then mapped onto ``canvas``.
    const double cw = geom::k_reference_canvas.width;
    const double ch = geom::k_reference_canvas.height;

    Element el;
    el.binding = binding;
    el.object.id = std::move(id);
    el.object.type = type;

    switch (binding) {
    case ElementBinding::Lyrics:
    case ElementBinding::BibleText:
        el.object.w = cw * 2.0 / 3.0;
        el.object.h = ch * 0.37;
        el.object.font_size = 48;
        el.object.text_align = "center";
        el.object.vertical_align = "middle";
        break;
    case ElementBinding::VideoArea:
    case ElementBinding::ImageArea:
    case ElementBinding::WebArea:
        el.object.x = 0;
        el.object.y = 0;
        el.object.w = cw;
        el.object.h = ch;
        el.object.object_fit = "contain";
        break;
    case ElementBinding::Countdown:
        el.object.font_size = 30;
        el.object.w = 30 * k_chrome_width_per_pt;
        el.object.h = 30 + k_chrome_height_pad;
        break;
    case ElementBinding::Indicator:
    case ElementBinding::Static:
        break;
    }

    if (binding == ElementBinding::Static || binding == ElementBinding::Indicator) {
        switch (type) {
        case freeform::ObjectType::Text:
            el.object.w = cw / 3.0;
            el.object.h = ch / 8.0;
            el.object.text = "Text";
            break;
        case freeform::ObjectType::Shape:
            el.object.fill.color = freeform::k_default_shape_fill;
            el.object.line.color = freeform::k_default_shape_stroke;
            el.object.w = cw / 4.0;
            el.object.h = ch / 4.0;
            break;
        case freeform::ObjectType::Image:
        case freeform::ObjectType::Video:
            el.object.w = cw / 3.0;
            el.object.h = ch / 3.0;
            break;
        case freeform::ObjectType::Table:
            el.object.w = cw / 2.0;
            el.object.h = ch / 4.0;
            break;
        case freeform::ObjectType::Clock:
            el.object.font_size = 48;
            el.object.w = 48 * k_chrome_width_per_pt;
            el.object.h = 48 + k_chrome_height_pad;
            el.object.text_align = "left";
            el.object.vertical_align = "top";
            break;
        case freeform::ObjectType::Indicator:
            el.object.font_size = 30;
            el.object.w = 30 * k_chrome_width_per_pt;
            el.object.h = 30 + k_chrome_height_pad;
            el.object.text_align = "left";
            el.object.vertical_align = "top";
            break;
        }
    }

    if (binding != ElementBinding::VideoArea && binding != ElementBinding::ImageArea
        && binding != ElementBinding::WebArea) {
        el.object.x = std::round((cw - el.object.w) / 2.0);
        el.object.y = std::round((ch - el.object.h) / 2.0);
    }
    resize_element(el, geom::k_reference_canvas, canvas);
    return el;
}

namespace {

/// The canvas the portrait stock design is authored on.
constexpr geom::CanvasSize k_portrait_reference_canvas{1080, 1920};

/// Where the stock boxes sit on their reference canvas.
struct StockLayout {
    geom::CanvasSize canvas;
    /// The lyrics box and the Bible passage.
    canvas::BoxRect body;
    /// The copyright notice.
    canvas::BoxRect copyright;
    /// The Bible reference.
    canvas::BoxRect reference;
};

[[nodiscard]] StockLayout stock_layout(geom::CanvasSize canvas)
{
    if (canvas.valid() && canvas.height > canvas.width) {
        // Tall: the boxes span the width, the notice and reference along the
        // foot.
        return {.canvas = k_portrait_reference_canvas,
                .body = {90, 640, 900, 640},
                .copyright = {60, 1800, 960, 80},
                .reference = {60, 1700, 960, 100}};
    }
    return {.canvas = geom::k_reference_canvas,
            .body = {320, 340, 1280, 400},
            .copyright = {100, 980, 1720, 80},
            .reference = {100, 900, 800, 100}};
}

}  // namespace

ElementSet default_elements(geom::CanvasSize canvas)
{
    const StockLayout at = stock_layout(canvas);
    const double cw = at.canvas.width;
    const double ch = at.canvas.height;

    const auto text_box = [](std::string id, const canvas::BoxRect& frame, int font_size,
                             std::string align) {
        Element el;
        el.object.id = std::move(id);
        el.object.type = freeform::ObjectType::Text;
        el.object.z = 0;
        static_cast<canvas::BoxRect&>(el.object) = frame;
        el.object.font_family = canvas::k_default_font_family;
        el.object.font_size = font_size;
        el.object.bold = false;
        el.object.italic = false;
        el.object.font_color = "#ffffff";
        el.object.text_align = std::move(align);
        el.object.vertical_align = "middle";
        el.object.opacity = 1.0;
        return el;
    };
    const auto media_area = [cw, ch](std::string id, ElementBinding binding, int z) {
        Element el;
        el.object.id = std::move(id);
        el.object.type = freeform::ObjectType::Image;
        el.binding = binding;
        el.object.z = z;
        el.object.x = 0;
        el.object.y = 0;
        el.object.w = cw;
        el.object.h = ch;
        el.object.object_fit = "contain";
        return el;
    };

    ElementSet set;

    // Song: the lyrics box, and the copyright notice the per-song toggle governs.
    Element lyrics = text_box("te_lyrics", at.body, 48, "center");
    lyrics.binding = ElementBinding::Lyrics;
    lyrics.object.line_height = layout::clamp_overlay_line_height(1.2);
    lyrics.object.letter_spacing = layout::clamp_overlay_letter_spacing(0.0);
    set.song.push_back(std::move(lyrics));

    Element copyright = text_box("te_copyright", at.copyright, 20, "left");
    copyright.object.z = 20;
    copyright.label = "Copyright";
    copyright.object.text = std::string(k_default_copyright_template);
    copyright.show_when = ShowWhen::SongCopyrightOn;
    set.song.push_back(copyright);

    // Bible: the passage and its reference.
    Element passage = text_box("te_bible_text", at.body, 48, "center");
    passage.binding = ElementBinding::BibleText;
    passage.object.line_height = layout::clamp_overlay_line_height(1.2);
    passage.object.letter_spacing = layout::clamp_overlay_letter_spacing(0.0);
    set.bible.push_back(std::move(passage));

    Element reference = text_box("te_bible_ref", at.reference, 30, "left");
    reference.object.z = 20;
    reference.label = "Reference";
    reference.object.text = std::string(k_default_bible_ref_template);
    set.bible.push_back(std::move(reference));

    // Pictures, videos, web pages and live sources each fill the canvas.
    set.image.push_back(media_area("te_image_area", ElementBinding::ImageArea, 0));
    set.video.push_back(media_area("te_video_area", ElementBinding::VideoArea, 0));
    set.web.push_back(media_area("te_web_area", ElementBinding::WebArea, 0));
    set.live.push_back(media_area("te_live_area", ElementBinding::VideoArea, 0));

    // Titles: the same notice, under whatever title boxes the designer adds.
    copyright.object.id = "te_title_copyright";
    copyright.object.z = 0;
    set.titles.push_back(std::move(copyright));

    set.next_id = 1;
    set.canvas = at.canvas;
    if (canvas.valid() && canvas != at.canvas) {
        for (const auto ctx : k_element_contexts) {
            for (auto& el : set.at(ctx)) {
                resize_element(el, at.canvas, canvas);
            }
        }
        set.canvas = canvas;
    }
    return set;
}

void map_element(Element& el, const geom::CanvasMapping& mapping)
{
    freeform::map_object(el.object, mapping);
    for (auto* line : {&el.body.active_line, &el.body.other_lines}) {
        if (line->size > 0) {
            line->size = std::max(1, mapping.font_size(line->size));
        }
    }
    el.body.verse_font_size = mapping.font_size(el.body.verse_font_size);
}

void resize_element(Element& el, geom::CanvasSize from, geom::CanvasSize to)
{
    const canvas::BoxRect before = el.object;
    map_element(el, geom::CanvasMapping::resize(from, to));
    canvas::keep_proportions(el.object, before, from, to);
}

namespace {

[[nodiscard]] geom::CanvasSize stored_canvas(const nlohmann::json& blob)
{
    const auto* c = seventhslide::json::find(blob, "canvas");
    if (c == nullptr || !c->is_object()) {
        return geom::k_reference_canvas;
    }
    return geom::canvas_or_reference(int_or(*c, "width", 0), int_or(*c, "height", 0));
}

[[nodiscard]] nlohmann::json canvas_json(geom::CanvasSize canvas)
{
    return {{"width", canvas.width}, {"height", canvas.height}};
}

/// ``map_element`` for the body options of a stored element.
void map_body_json(nlohmann::json& el, const geom::CanvasMapping& mapping)
{
    const auto body = el.find("body");
    if (body == el.end() || !body->is_object()) {
        return;
    }
    if (const auto it = body->find("verse_font_size"); it != body->end() && it->is_number()) {
        *it = mapping.font_size(static_cast<int>(std::lround(it->get<double>())));
    }
    for (const char* key : {"active_line", "other_lines"}) {
        const auto line = body->find(key);
        if (line == body->end() || !line->is_object()) {
            continue;
        }
        const auto it = line->find("size");
        if (it != line->end() && it->is_number() && it->get<double>() > 0) {
            *it = std::max(
                1, mapping.font_size(static_cast<int>(std::lround(it->get<double>()))));
        }
    }
}

/// ``fn(element)`` for every stored element of ``blob``, then the model is
/// stamped ``canvas`` and ``style``'s verse gap carried by ``mapping``: the gap
/// between verses is part of the same design.
template <class Fn>
void move_elements(nlohmann::json& style, nlohmann::json& blob, geom::CanvasSize canvas,
                   const geom::CanvasMapping& mapping, Fn&& fn)
{
    const auto each = [&blob, &fn](std::string_view key) {
        const auto list = blob.find(std::string(key));
        if (list == blob.end() || !list->is_array()) {
            return;
        }
        for (auto& el : *list) {
            fn(el);
        }
    };
    for (const auto ctx : k_element_contexts) {
        each(to_string(ctx));
    }
    blob["canvas"] = canvas_json(canvas);
    if (const auto gap = style.find("verse_gap"); gap != style.end() && gap->is_number()) {
        *gap = mapping.pixels(static_cast<int>(std::lround(gap->get<double>())));
    }
}

}  // namespace

void fit_elements_to_canvas(nlohmann::json& style, geom::CanvasSize canvas)
{
    if (!style.is_object() || !canvas.valid()) {
        return;
    }
    if (!has_elements(style)) {
        style[std::string(k_elements_key)] = default_elements(canvas).to_json();
    }
    auto& blob = style[std::string(k_elements_key)];
    const auto from = stored_canvas(blob);
    if (from == canvas) {
        return;
    }
    const auto mapping = geom::CanvasMapping::stretch(from, canvas);
    move_elements(style, blob, canvas, mapping, [&mapping](nlohmann::json& el) {
        freeform::map_object_json(el, mapping);
        map_body_json(el, mapping);
    });
}

bool resize_elements(nlohmann::json& style, geom::CanvasSize canvas)
{
    if (!style.is_object() || !canvas.valid() || !has_elements(style)) {
        return false;
    }
    auto& blob = style[std::string(k_elements_key)];
    const auto from = stored_canvas(blob);
    if (from == canvas) {
        return false;
    }
    const auto mapping = geom::CanvasMapping::resize(from, canvas);
    move_elements(style, blob, canvas, mapping, [&](nlohmann::json& el) {
        freeform::resize_object_json(el, from, canvas);
        map_body_json(el, mapping);
    });
    return true;
}

void set_elements_canvas(nlohmann::json& style, geom::CanvasSize canvas)
{
    if (!canvas.valid() || !has_elements(style)) {
        return;
    }
    style[std::string(k_elements_key)]["canvas"] = canvas_json(canvas);
}

std::vector<layout::OverlayBox> title_boxes(const ElementSet& set, const ElementConditions& cond)
{
    std::vector<layout::OverlayBox> out;
    for (const auto& el : set.titles) {
        if (el.object.type != freeform::ObjectType::Text
            || el.binding != ElementBinding::Static
            || !condition_met(el.show_when, cond)) {
            continue;
        }
        const auto& o = el.object;
        layout::OverlayBox box;
        static_cast<canvas::BoxRect&>(box) = o;
        static_cast<canvas::BoxType&>(box) = o;
        box.rotation = o.rotation;
        box.z = o.z;
        for (const auto& rich : model::parse_markup_lines(o.text)) {
            box.lines.push_back(layout::styled_line(rich, o.bold, o.italic, o.underline));
        }
        out.push_back(std::move(box));
    }
    return out;
}

}  // namespace seventhslide::theme
