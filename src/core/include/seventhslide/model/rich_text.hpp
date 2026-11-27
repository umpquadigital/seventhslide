#pragma once

// Rich-text runs - structured spans at the core boundary.
// Songs are stored as inline markup (<b>/<i>/<u>/<size=NN>, ...); these
// converters read and write it. Chords ([C]) and forced breaks stay plain text here.
// Seam: plain C++20 (no Qt).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::model {

/// Forced slide-break marker (entire logical line). Layout consumes this later.
inline constexpr std::string_view k_forced_split = "[---][---]";

/// True when ``trimmed`` is the forced-break marker.
[[nodiscard]] inline bool is_forced_split_marker(std::string_view trimmed) noexcept
{
    return trimmed == k_forced_split;
}

/// Relative size clamp used at display / measure edges.
[[nodiscard]] int clamp_size_pct(int raw) noexcept;
/// Superscript rise clamp (0-100 percent of the box font size).
[[nodiscard]] int clamp_rise_pct(int raw) noexcept;
/// CSS ``vertical-align: super`` analogue used by ``<super>`` / Bible citations.
inline constexpr int k_super_rise_pct = 40;
/// Most a ``<gap=NN>`` asks for, as a theme's verse number can.
inline constexpr int k_max_gap_pct = 400;

/// One paragraph's own settings, over its box's: a line of markup's leading
/// ``<p=...>``. Lengths are percents of the box's font size, as ``<gap=NN>``
/// is, so they follow it through a canvas change and shrink-to-fit.
struct LineFormat {
    /// left, center or right; empty = the box's.
    std::string align;
    /// Space before and after it, and its first row's indent; -1 = the box's.
    int before_pct = -1;
    int after_pct = -1;
    int indent_pct = -1;
    /// Its line height, percent (115 = 1.15); 0 = the box's.
    int line_pct = 0;

    [[nodiscard]] bool operator==(const LineFormat&) const noexcept = default;
    [[nodiscard]] bool any() const noexcept { return *this != LineFormat{}; }
};

/// Most a line format's lengths take, percent of the font size.
inline constexpr int k_max_line_format_pct = 2000;

/// ``format`` as the ``p`` tag's value ("align:center,before:50"); empty for
/// none.
[[nodiscard]] std::string format_line_format(const LineFormat& format);
/// A ``p`` tag's value back; nullopt when any of it is not one.
[[nodiscard]] std::optional<LineFormat> parse_line_format(std::string_view value);

struct TextStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    /// Percent of surrounding font size (100 = default).
    int size_pct = 100;
    /// The exact size in px when one is known, which a whole percent may not
    /// hold (a following line's 36 px under a 54 px box is 66.67%); 0 = go by
    /// ``size_pct``. Not written to markup, which reads back ``size_pct``.
    double size_px = 0.0;
    /// Raise the run above the line baseline by this percent of the box font size
    /// (Bible verse numbers: ``k_super_rise_pct`` ~ CSS ``vertical-align: super``).
    int baseline_rise_pct = 0;
    /// Empty = inherit line/box color at paint time.
    std::string color;
    /// Empty = inherit box font family at paint time.
    std::string font_family;
    /// Extra advance after this span, as percent of the span's painted font size.
    int margin_right_pct = 0;
    /// That margin, even of nothing, is the space after the span: where a line
    /// is wrapped any space there is not drawn, and the row may break there.
    bool margin_for_space = false;
    /// The paragraph's own settings; the same on every run of a line.
    LineFormat line;

    [[nodiscard]] bool operator==(const TextStyle&) const noexcept = default;

    /// True when this run differs from the box default (needs inline markup).
    /// Not the line format: that is the paragraph's, not the run's.
    [[nodiscard]] bool has_inline_style() const noexcept;
};

/// One contiguous run of plain UTF-8 text with a single style.
struct TextSpan {
    std::string text;
    TextStyle style;

    [[nodiscard]] bool operator==(const TextSpan&) const noexcept = default;
};

using RichLine = std::vector<TextSpan>;

// Inline markup
// -------------
// Stored text is plain text plus a closed set of exact tags:
//   <b> <i> <u> <super>  (aliases <strong> <em> <sup>)   and their closers
//   <size=NN> <rise=NN> <gap=NN>  (1-3 digits)           </size> </rise> </gap>
//   <color=#hex>         (3/4/6/8 hex digits)            </color>
//   <font=Name>          (1-64 chars, quotes optional)   </font>
//   <p=align:center,before:50,after:50,indent:100,line:120>   at a line's
//                        start only: that paragraph's own settings (LineFormat)
// Tag names are case-insensitive, but for <p=...>: it is written only by the
// formatter, in lowercase, and is matched exactly, its keys and values too.
// Anything else containing '<' or '>' is ordinary text and is stored as typed
// ("I <3 you", "a < b and c > d").
//
// Only three entities are decoded in text: &lt; &gt; &amp;. The formatter
// writes them only where literal text would otherwise read as a tag or an
// entity, so ordinary lyrics never contain one.

/// One tag at the start of ``text`` (which begins with '<').
struct MarkupTag {
    bool close = false;
    /// Canonical name: b, i, u, super, size, rise, gap, color or font.
    std::string name;
    /// size / rise / gap value.
    int number = 0;
    /// color (``#rrggbb``-style, lowercase) or font family.
    std::string value;
    /// Bytes the tag occupies in ``text``.
    std::size_t length = 0;
};

/// The tag ``text`` starts with, or nullopt when it is ordinary text.
[[nodiscard]] std::optional<MarkupTag> match_markup_tag(std::string_view text);

/// Escape plain text for storage as markup (see the rules above).
[[nodiscard]] std::string escape_markup_text(std::string_view text);

/// Import path for song files: ``<br>`` -> newline, HTML entities decoded,
/// known HTML formatting elements (``<b class="x">``, ``<strong>``...) mapped
/// to their tags. Everything else is kept as text.
[[nodiscard]] std::string normalize_imported_markup(std::string_view text);

/// Parse one logical lyric line into styled spans.
/// Chord markers and ``k_forced_split`` remain inside plain text of spans.
[[nodiscard]] RichLine parse_inline_markup(std::string_view text);

/// Emit canonical inline markup from spans (bare tags + size/color/font).
/// Round-trips through ``parse_inline_markup`` to the same spans.
[[nodiscard]] std::string format_inline_markup(const RichLine& line);

/// Split on ``\\n``, parse each line. Empty input -> one empty line.
[[nodiscard]] std::vector<RichLine> parse_markup_lines(std::string_view text);

/// Join lines with ``\\n`` after formatting each.
[[nodiscard]] std::string format_markup_lines(const std::vector<RichLine>& lines);

/// Concatenate span text (no tags).
[[nodiscard]] std::string plain_text(const RichLine& line);

/// Re-fit one line's runs to ``next`` after an edit. Only the bytes that differ
/// are replaced; the runs on either side keep their styling, and the new text
/// inherits the run it lands in (first replaced character, else the preceding
/// one). Always returns at least one span.
[[nodiscard]] RichLine reconcile_rich_line(const RichLine& prev, std::string_view next);

/// Re-fit ``prev`` to ``text`` (plain, ``\n``-separated) after an edit. Lines are
/// paired from both ends first, so inserting or deleting a line does not slide
/// every later line's styling by one; each paired line goes through
/// ``reconcile_rich_line``. Empty input -> one empty line.
[[nodiscard]] std::vector<RichLine> reconcile_rich_lines(const std::vector<RichLine>& prev,
                                                          std::string_view text);

/// Plain text of markup: tags removed, entities decoded, newlines kept.
[[nodiscard]] std::string strip_markup_tags(std::string_view text);

/// True when the plain text of the line (trimmed) is a forced-break marker.
[[nodiscard]] bool is_forced_break_line(const RichLine& line);

/// Join lines with ``\\n`` (empty input -> empty string).
[[nodiscard]] std::string plain_text_lines(const std::vector<RichLine>& lines);

/// Optional overrides applied to a run. Unset fields stay as they are.
struct SpanAttrs {
    std::optional<int> size_pct;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<std::string> color;
    std::optional<std::string> font_family;
    std::optional<int> baseline_rise_pct;
};

/// Line format fields to set on paragraphs. Unset fields stay as they are.
struct LineFormatAttrs {
    std::optional<std::string> align;
    std::optional<int> before_pct;
    std::optional<int> after_pct;
    std::optional<int> indent_pct;
    std::optional<int> line_pct;
};

/// ``attrs`` on each paragraph the UTF-8 range ``start`` to ``end`` of
/// ``plain_text_lines`` touches: the one the caret is in when they are equal.
[[nodiscard]] std::vector<RichLine> apply_line_format(std::vector<RichLine> lines,
                                                      std::size_t start,
                                                      std::size_t end,
                                                      const LineFormatAttrs& attrs);
/// The format of the paragraph at UTF-8 ``offset`` of ``plain_text_lines``.
[[nodiscard]] LineFormat line_format_at_offset(const std::vector<RichLine>& lines,
                                               std::size_t offset);

/// Caret / selection sample at a UTF-8 byte offset into ``plain_text_lines``.
struct RunStyle {
    int size_pct = 100;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::string color;
    std::string font_family;
    int baseline_rise_pct = 0;
};

/// Append ``text`` in ``style`` to ``line``, extending the last run when its
/// style matches, so a line built this way never has two neighbouring runs of
/// one style. Empty text adds nothing. Inline: the wrapper calls it per word.
inline void append_styled(RichLine& line, std::string_view text, const TextStyle& style)
{
    if (text.empty()) {
        return;
    }
    if (!line.empty() && line.back().style == style) {
        line.back().text.append(text);
        return;
    }
    line.push_back(TextSpan{std::string(text), style});
}

[[nodiscard]] std::vector<RichLine> apply_attrs_to_all(std::vector<RichLine> lines,
                                                       const SpanAttrs& attrs);

/// Style a caret-range of a multi-line document. ``start``/``end`` are UTF-8
/// byte offsets into ``plain_text_lines`` (newlines count as one byte).
[[nodiscard]] std::vector<RichLine> apply_attrs_to_selection(std::vector<RichLine> lines,
                                                             std::size_t start, std::size_t end,
                                                             const SpanAttrs& attrs);

[[nodiscard]] RunStyle style_at_offset(const std::vector<RichLine>& lines, std::size_t offset);

/// Parse markup for an in-canvas editor. When the markup has no inline style,
/// every run takes ``box_*`` so the caret inherits the object's flags.
[[nodiscard]] std::vector<RichLine> parse_editor_lines(std::string_view markup, bool box_bold,
                                                       bool box_italic, bool box_underline);

/// Persist an editor document: mixed inline style -> markup and cleared flags;
/// uniform B/I/U -> plain text plus those flags.
struct EditorObjectText {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool mixed = false;
};

[[nodiscard]] EditorObjectText format_editor_object(const std::vector<RichLine>& lines);

} // namespace seventhslide::model
