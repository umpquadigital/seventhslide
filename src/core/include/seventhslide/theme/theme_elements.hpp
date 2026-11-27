#pragma once

// Text-theme elements - the freeform object model, applied to themes.
//
// A text theme's design is a list of canvas objects per design context, exactly
// like a Freeform slide, so the designer can add, delete, duplicate and stack
// them freely.
//
// Two things a Freeform object does not need and a theme element does:
//   - ``binding`` - what fills it at showtime (its own text, the song body,
//                      the passage body, the live video rectangle, ...)
//   - ``visibility`` - which slides of the current item it appears on
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/freeform/document.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/overlays.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

/// Style key holding the whole element model (one object, four context lists).
inline constexpr std::string_view k_elements_key = "elements";

/// The stock copyright element's text. ``{-token}`` drops the whole line when
/// that field is blank (same rules as title overlays).
inline constexpr std::string_view k_default_copyright_template =
    "{-authors}\n{-copyright}\nCCLI License #{-ccli-license}";

/// The stock Bible reference element's text.
inline constexpr std::string_view k_default_bible_ref_template =
    "{-reference}\n{-version}";

/// Each content type is designed in a context of its own, with its own
/// elements and settings, and nothing set in one reaches another. Each
/// context belongs to one kind of text theme (theme_kind.hpp): Song and
/// Titles to song themes, Bible to Bible themes, the rest to media themes.
/// An image folder plays as Image. Titles is the design of a song's title
/// slides, which are Song slides for everything but their elements. Audio is
/// not a context: it paints nothing, and its fades are each output's own
/// (``model::AudioFades``).
/// ``bg`` and ``behavior`` are not contexts: the first is the background
/// theme, the second the song design with its layout controls in front.
enum class ElementContext {
    Song,
    Bible,
    Image,
    Video,
    Web,
    Live,
    Titles,
};

inline constexpr ElementContext k_element_contexts[] = {
    ElementContext::Song,
    ElementContext::Bible,
    ElementContext::Image,
    ElementContext::Video,
    ElementContext::Web,
    ElementContext::Live,
    ElementContext::Titles,
};

/// The contexts that are a content type: Titles is the Song's.
inline constexpr ElementContext k_content_contexts[] = {
    ElementContext::Song,
    ElementContext::Bible,
    ElementContext::Image,
    ElementContext::Video,
    ElementContext::Web,
    ElementContext::Live,
};

[[nodiscard]] std::string_view to_string(ElementContext ctx) noexcept;
/// ``behavior`` shares the song design. Unknown names become ``Song``.
[[nodiscard]] ElementContext element_context_from_string(std::string_view s) noexcept;
/// The context whose settings a context uses: Titles uses the Song's, the
/// rest their own.
[[nodiscard]] constexpr ElementContext settings_context(ElementContext ctx) noexcept
{
    return ctx == ElementContext::Titles ? ElementContext::Song : ctx;
}
/**
 * What fills this element at showtime.
 *
 * ``Static`` paints the object's own authored content with ``{token}``s
 * substituted from the live song / passage (a copyright line, a Bible
 * reference). The rest are the handful of things a theme
 * cannot author because only the running service knows them.
 *
 * A context may hold at most one ``Lyrics`` and one ``BibleText`` element: they
 * own the laid-out body, which the layout engine packs one box at a time.
 */
enum class ElementBinding {
    Static,
    Lyrics,      // song body (Text objects)
    BibleText,   // passage body (Text objects)
    Indicator,   // verse indicator (Indicator objects; here for symmetry)
    Countdown,   // remaining time of the playing video (Clock objects)
    VideoArea,   // rectangle the live video is fitted into (Image objects)
    ImageArea,   // rectangle a still image is fitted into (Image objects)
    WebArea,     // rectangle a web page is fitted into (Image objects)
};

[[nodiscard]] std::string_view to_string(ElementBinding binding) noexcept;
[[nodiscard]] ElementBinding element_binding_from_string(std::string_view s) noexcept;

/// Lyrics / BibleText / VideoArea / ImageArea / WebArea: one per context.
[[nodiscard]] bool binding_is_exclusive(ElementBinding binding) noexcept;
/// Insert menu rules: lyrics and indicators on song, passage on bible, the
/// picture area on image, the video area and countdown on video, the web
/// area on web, the video area on live. ``Static`` goes anywhere.
[[nodiscard]] bool binding_fits_context(ElementContext ctx, ElementBinding binding) noexcept;

/// Which of the slide editor's controls change how an object paints, by its
/// kind and what fills it at showtime. A control that would change nothing is
/// not offered. A Freeform object's binding is ``Static``.
struct ObjectControls {
    /// Bold, italic, underline and superscript per run, and line spacing:
    /// text bodies only.
    bool runs = false;
    /// Bold and italic on the whole object: a clock has no runs to split.
    bool weight = false;
    bool font = false;
    bool color = false;
    bool align = false;
    /// How a picture or video fills its frame.
    bool fit = false;
    bool shape = false;
    /// A clock's 24-hour time and seconds.
    bool clock = false;
    bool indicator = false;
    /// A shadow, a glow, a soft edge.
    bool effects = false;
    /// A fill and a line: a shape's, or the frame behind a box's words.
    bool fill = false;

    [[nodiscard]] bool operator==(const ObjectControls&) const noexcept = default;
};

[[nodiscard]] ObjectControls object_controls(freeform::ObjectType type,
                                             ElementBinding binding) noexcept;



/// Which slides of the current item show the element.
enum class SlideRule {
    All,
    First,        // first ``count`` slides
    Last,         // last ``count`` slides
    ExceptFirst,
    ExceptLast,
};

[[nodiscard]] std::string_view to_string(SlideRule rule) noexcept;
[[nodiscard]] SlideRule slide_rule_from_string(std::string_view s) noexcept;

struct ElementVisibility {
    SlideRule rule = SlideRule::All;
    /// Slides the rule counts. Clamped to 1-99 on write.
    int count = 1;

    [[nodiscard]] bool operator==(const ElementVisibility&) const noexcept = default;
};

/// True when ``slide_index`` (0-based, of ``slide_count``) is inside the rule.
[[nodiscard]] bool rule_covers_slide(const ElementVisibility& vis,
                                     int slide_index,
                                     int slide_count) noexcept;

/**
 * A live condition the element paints under, beside its slide rule.
 *
 * The per-song "Display copyright on output" toggle governs an *element* (the
 * copyright notice) rather than the ``{authors}`` / ``{copyright}`` tokens in
 * it: a title design may use the same tokens without being a copyright notice,
 * and hiding on token name would take it with it.
 */
enum class ShowWhen {
    Always,
    SongCopyrightOn,
};

[[nodiscard]] std::string_view to_string(ShowWhen when) noexcept;
[[nodiscard]] ShowWhen show_when_from_string(std::string_view s) noexcept;

/**
 * The showtime facts ``ShowWhen`` tests.
 *
 * Default-constructed grants nothing, so a caller that forgets to plumb the live
 * values hides a conditioned element rather than showing a copyright notice for
 * a song that opted out.
 */
struct ElementConditions {
    /// The live song's "Display copyright on output" toggle.
    bool song_copyright = false;

    [[nodiscard]] bool operator==(const ElementConditions&) const noexcept = default;
};

/// True when ``when`` is satisfied by ``cond``.
[[nodiscard]] bool condition_met(ShowWhen when, const ElementConditions& cond) noexcept;

/// What follow-lines paints the lines other than the active one in.
inline constexpr std::string_view k_default_dim_color = "#888888";

/**
 * Paint settings that exist only for the dynamic body bindings.
 *
 * These are the parts of a laid-out body a theme styles but a static text box
 * has no use for: the follow-lines highlight/dim pair (Lyrics) and the inline
 * verse-number run (BibleText).
 */
struct BodyOptions {
    // Lyrics. Follow-lines sets the active line and the other lines each in a
    // type of its own over the box's (``object``'s); a field a type leaves
    // empty is the box's. The other lines start in ``k_default_dim_color``.
    canvas::LineType active_line;
    canvas::LineType other_lines{.color = std::string(k_default_dim_color)};
    bool show_chords = false;
    /// How chords look when they are shown.
    layout::ChordStyle chords;

    // BibleText
    bool verse_numbers = false;
    std::string verse_font_family{canvas::k_default_font_family};
    int verse_font_size = 24;
    bool verse_font_bold = false;
    bool verse_font_italic = false;
    std::string verse_color = "#ffffff";
    /// ``super`` | ``middle`` | ``baseline``.
    std::string verse_valign = "super";
    int verse_margin_right_pct = 40;

    [[nodiscard]] bool operator==(const BodyOptions&) const noexcept = default;
};

/**
 * One thing on the canvas in one design context.
 *
 * ``object`` is the Freeform object schema verbatim, so geometry, typography,
 * shape outlines, image fit and clock format all mean exactly what they mean in
 * the Freeform editor, and both editors can share ribbon code.
 */
struct Element {
    freeform::Object object;
    ElementBinding binding = ElementBinding::Static;
    ElementVisibility visibility;
    /// Live condition on top of ``visibility`` (the per-song copyright toggle).
    ShowWhen show_when = ShowWhen::Always;
    /// Rail caption. Empty -> derived from binding / type / text.
    std::string label;
    BodyOptions body;

    [[nodiscard]] const std::string& id() const noexcept { return object.id; }
    [[nodiscard]] freeform::ObjectType type() const noexcept { return object.type; }

    [[nodiscard]] bool operator==(const Element&) const noexcept = default;
};

/// Lets the shared canvas list operations (``canvas/object_list.hpp``) work
/// on a design context.
[[nodiscard]] constexpr freeform::Object& object_of(Element& el) noexcept
{
    return el.object;
}

[[nodiscard]] constexpr const freeform::Object& object_of(const Element& el) noexcept
{
    return el.object;
}

/// The element model of a text theme: as stored, only its kind's contexts
/// hold elements (``filter_theme_style``); the rest are empty.
struct ElementSet {
    /// The canvas every element below is laid out on: the output's own, the
    /// set carried over once when that changes (``resize_elements``). An
    /// output paints a set on another canvas mapped onto its own all the same
    /// (``fit_elements_to_canvas``).
    geom::CanvasSize canvas = geom::k_reference_canvas;
    std::vector<Element> song;
    std::vector<Element> bible;
    std::vector<Element> image;
    std::vector<Element> video;
    std::vector<Element> web;
    std::vector<Element> live;
    std::vector<Element> titles;
    /// Monotonic source for ``te<n>`` ids; never reused within a theme.
    int next_id = 1;

    [[nodiscard]] std::vector<Element>& at(ElementContext ctx) noexcept;
    [[nodiscard]] const std::vector<Element>& at(ElementContext ctx) const noexcept;

    [[nodiscard]] std::string mint_id();

    [[nodiscard]] nlohmann::json to_json() const;
    static ElementSet from_json(const nlohmann::json& j);

    [[nodiscard]] bool operator==(const ElementSet&) const noexcept = default;
};

void to_json(nlohmann::json& j, const Element& v);
void from_json(const nlohmann::json& j, Element& v);

/// ``style``'s element model (``style["elements"]``: one list per context), or
/// an empty object when it has none. ``OutputConfig::elements`` holds the same.
[[nodiscard]] const nlohmann::json& element_model(const nlohmann::json& style) noexcept;

/// Elements of ``all`` that paint on ``slide_index`` of ``slide_count``, z-sorted.
/// ``cond`` carries the live facts an element's ``show_when`` tests; the default
/// grants nothing (see ``ElementConditions``).
[[nodiscard]] std::vector<Element> visible_elements(const std::vector<Element>& all,
                                                    int slide_index,
                                                    int slide_count,
                                                    const ElementConditions& cond = {});


/**
 * True when any context of ``set`` holds a clock element.
 *
 * The clock is the one element whose pixels change on their own, so the app arms
 * a per-second repaint only when a theme actually places one somewhere.
 */
[[nodiscard]] bool has_clock(const ElementSet& set);

/// True when the style carries a non-empty element model.
[[nodiscard]] bool has_elements(const nlohmann::json& style) noexcept;

/// Carry one element's geometry and every pixel size it holds (object type
/// sizes, strokes, gaps, tracking; body highlight and verse sizes) through
/// ``mapping``.
void map_element(Element& el, const geom::CanvasMapping& mapping);

/**
 * Lay ``style``'s element model out on ``canvas`` as paint does.
 *
 * A model is kept on its output's canvas (``resize_elements`` when the output
 * changes); one that is on another all the same is mapped here onto the
 * canvas that paints it (``geom::CanvasMapping::stretch``), together with the
 * theme's ``verse_gap``, and restamped. Calling it again for the same canvas
 * does nothing. Stored JSON is updated in place, so keys the C++ model does
 * not know are kept.
 *
 * A style with no element model is given the stock design for ``canvas``.
 */
void fit_elements_to_canvas(nlohmann::json& style, geom::CanvasSize canvas);

/**
 * Declare that ``style``'s elements are laid out on ``canvas`` without moving
 * them: for a design that arrives already in that canvas's pixels (from the
 * theme designer, which edits what is stored, on the output's canvas). No-op
 * without a model.
 */
void set_elements_canvas(nlohmann::json& style, geom::CanvasSize canvas);

/**
 * The stock design a brand-new text theme starts from, laid out on ``canvas``.
 *
 * Song: the lyrics box and the copyright notice. Bible: the passage and its
 * reference. Media: video and image areas filling the canvas. Titles: the
 * copyright notice. Everything else the designer inserts on purpose.
 *
 * A canvas taller than it is wide gets the portrait design, authored on
 * 1080x1920: the boxes span the width and the type is the landscape's. Any
 * other gets the landscape design, authored on ``geom::k_reference_canvas``.
 * Each is carried onto ``canvas`` as a resize is (``resize_element``).
 */
[[nodiscard]] ElementSet default_elements(geom::CanvasSize canvas = geom::k_reference_canvas);

/// Element the insert menu creates: a ``type`` object with id ``id`` bound to
/// ``binding``, placed and sized for that binding on ``canvas``.
[[nodiscard]] Element make_element(freeform::ObjectType type,
                                   ElementBinding binding,
                                   std::string id,
                                   geom::CanvasSize canvas);

/// Carry ``el`` from ``from`` onto ``to`` as an output resize does
/// (``freeform::resize_object``, with the body's sizes).
void resize_element(Element& el, geom::CanvasSize from, geom::CanvasSize to);

/**
 * Carry ``style``'s element model onto ``canvas`` the way an output resize
 * does: every element by ``resize_element`` from the canvas the model is
 * stamped with, the theme's ``verse_gap`` with them, and the model restamped.
 * Stored JSON is updated in place, so keys the C++ model does not know are
 * kept. False, and nothing moved, when there is no model or it is on
 * ``canvas`` already.
 */
bool resize_elements(nlohmann::json& style, geom::CanvasSize canvas);


/**
 * The Titles context's visible text elements, as overlay boxes.
 *
 * Title boxes keep travelling through the overlay path rather than the object
 * path because layout consults them: a song only dims its opening lyric lines
 * onto the title slide on outputs whose theme has *no* title design of its own.
 *
 * Elements whose ``show_when`` ``cond`` does not meet are left out, the same as
 * a box whose tokens all come out empty (``layout::build_overlay_scene``).
 */
[[nodiscard]] std::vector<layout::OverlayBox> title_boxes(const ElementSet& set,
                                                          const ElementConditions& cond);


}  // namespace seventhslide::theme
