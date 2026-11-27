#include "seventhslide/layout/editor_html.hpp"

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace seventhslide::layout {
namespace {

/// ``value`` as it can stand in a ``style="..."`` attribute, where a ``"``
/// would end the attribute and a ``<`` start a tag. Qt's HTML reader turns
/// the entities back into the characters.
[[nodiscard]] std::string css_escape(std::string_view value)
{
    return util::xml_escape(value, true);
}

/// A family inside ``font-family:'...'``, which a ``'`` would end.
[[nodiscard]] std::string css_escape_family(std::string_view family)
{
    std::string out;
    out.reserve(family.size());
    for (char c : family) {
        if (c != '\'') {
            out.push_back(c);
        }
    }
    return css_escape(out);
}

[[nodiscard]] int run_px(int box_size, int size_pct, double scale)
{
    const int pct = model::clamp_size_pct(size_pct);
    const double raw = static_cast<double>(box_size) * (static_cast<double>(pct) / 100.0) * scale;
    return std::max(8, static_cast<int>(std::lround(raw)));
}

} // namespace

std::string editor_html(const std::vector<model::RichLine>& lines, const EditorHtmlOptions& opts)
{
    const int fs = std::max(8, opts.font_size);
    const std::string color = opts.color.empty() ? std::string(canvas::k_default_font_color) : opts.color;
    const std::string family = opts.family.empty() ? std::string(canvas::k_default_font_family) : opts.family;
    const auto css_family = [&](const std::string& name) {
        return css_escape_family(opts.painted_family ? opts.painted_family(name) : name);
    };
    const double sc = opts.stage_scale > 0.0 && std::isfinite(opts.stage_scale)
                          ? opts.stage_scale
                          : 1.0;
    const double lh = opts.line_height > 0.0 ? opts.line_height : canvas::k_default_line_height;
    const double tracking =
        (std::isfinite(opts.letter_spacing) ? opts.letter_spacing : 0.0) * sc;
    const int gap_pct = std::max(0, opts.line_gap);
    // One block per line and nothing round them: a wrapping block would be a
    // paragraph of its own when the first line is empty, an extra line typed
    // into.
    std::ostringstream html;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        model::RichLine spans = lines[i];
        if (spans.empty()) {
            spans.push_back(model::TextSpan{});
        }
        // A margin that is the space after its run (``margin_for_space``) cannot
        // be an inline margin here: the space itself is drawn that wide, so a place
        // in the text stays the same place.
        std::vector<double> space_px(spans.size(), -1.0);
        for (std::size_t j = 0; j + 1 < spans.size(); ++j) {
            if (spans[j].style.margin_for_space && spans[j + 1].text.starts_with(' ')) {
                model::TextSpan space = spans[j + 1];
                space.text = " ";
                spans[j + 1].text.erase(0, 1);
                const double gap = run_px(fs, spans[j].style.size_pct, sc)
                    * (spans[j].style.margin_right_pct / 100.0);
                spans.insert(spans.begin() + static_cast<std::ptrdiff_t>(j) + 1, std::move(space));
                space_px.insert(space_px.begin() + static_cast<std::ptrdiff_t>(j) + 1, gap);
                ++j;
            }
        }
        int max_px = std::max(8, static_cast<int>(std::lround(static_cast<double>(fs) * sc)));
        for (const auto& sp : spans) {
            max_px = std::max(max_px, run_px(fs, sp.style.size_pct, sc));
        }
        // What this paragraph sets for itself, and what the one before left.
        const model::LineFormat format = spans.front().style.line;
        const int line_lh = css_line_height_px(static_cast<double>(max_px),
                                               format.line_pct > 0 ? format.line_pct / 100.0 : lh);
        const auto pct_px = [&](int pct) { return std::max(0, pct) / 100.0 * fs * sc; };
        const double own_gap =
            i > 0 ? pct_px(format.before_pct)
                        + (lines[i - 1].empty() ? 0.0 : pct_px(lines[i - 1].front().style.line.after_pct))
                  : 0.0;
        const int box_gap = (i > 0) ? gap_pct : 0;
        const int margin_top = static_cast<int>(std::lround(
            (static_cast<double>(box_gap) / 100.0) * static_cast<double>(fs) * sc + own_gap));
        std::ostringstream block;
        block << "padding:0; line-height:" << line_lh << "px;";
        if (!format.align.empty()) {
            block << " text-align:" << css_escape(format.align) << ";";
        }
        if (format.indent_pct > 0) {
            block << " text-indent:" << std::lround(pct_px(format.indent_pct)) << "px;";
        }
        if (margin_top > 0) {
            block << " margin:" << margin_top << "px 0 0 0;";
        } else {
            block << " margin:0;";
        }
        std::string plain;
        for (const auto& sp : spans) {
            plain += sp.text;
        }
        if (plain.empty()) {
            const auto& sp0 = spans.front();
            const std::string empty_face =
                !sp0.style.font_family.empty() ? sp0.style.font_family : family;
            html << "<div style=\"" << block.str() << " -qt-paragraph-type:empty;"
                 << " font-family:'" << css_family(empty_face) << "';"
                 << " font-size:" << run_px(fs, sp0.style.size_pct, sc) << "px;"
                 << (sp0.style.bold ? " font-weight:700;" : " font-weight:400;")
                 << (sp0.style.italic ? " font-style:italic;" : " font-style:normal;")
                 << "\"><br/></div>";
            continue;
        }
        html << "<div style=\"" << block.str() << "\">";
        html << "<span style=\"white-space:pre-wrap;\">";
        for (std::size_t j = 0; j < spans.size(); ++j) {
            const auto& sp = spans[j];
            const int px = space_px[j] >= 0.0 ? 1 : run_px(fs, sp.style.size_pct, sc);
            const std::string col = sp.style.color.empty() ? color : sp.style.color;
            const std::string face =
                sp.style.font_family.empty() ? family : sp.style.font_family;
            std::ostringstream style;
            style << "font-family:'" << css_family(face) << "';";
            style << " font-size:" << px << "px; color:" << css_escape(col) << ";";
            style << (sp.style.bold ? " font-weight:700;" : " font-weight:400;");
            style << (sp.style.italic ? " font-style:italic;" : " font-style:normal;");
            style << (sp.style.underline ? " text-decoration:underline;"
                                         : " text-decoration:none;");
            if (space_px[j] >= 0.0) {
                style << " letter-spacing:" << space_px[j] << "px;";
            } else if (tracking != 0.0) {
                style << " letter-spacing:" << tracking << "px;";
            }
            if (sp.style.baseline_rise_pct > 0) {
                style << " vertical-align:super;";
            }
            if (sp.style.margin_right_pct != 0 && !sp.style.margin_for_space) {
                style << " margin-right:"
                      << (static_cast<double>(px) * static_cast<double>(sp.style.margin_right_pct)
                          / 100.0)
                      << "px;";
            }
            if (j > 0) {
                html << "<!-- -->";
            }
            html << "<span style=\"" << style.str() << "\">" << util::xml_escape(sp.text)
                 << "</span>";
        }
        html << "</span></div>";
    }
    return html.str();
}

} // namespace seventhslide::layout
