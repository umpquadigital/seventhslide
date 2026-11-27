#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace seventhslide::layout {

model::RichLine styled_line(const model::RichLine& parsed, bool bold, bool italic, bool underline)
{
    const bool styled = std::any_of(parsed.begin(), parsed.end(), [](const model::TextSpan& sp) {
        return sp.style.has_inline_style();
    });
    if (!styled) {
        model::TextSpan run;
        run.text = model::plain_text(parsed);
        if (!parsed.empty()) {
            run.style.line = parsed.front().style.line;
        }
        run.style.bold = bold;
        run.style.italic = italic;
        run.style.underline = underline;
        return {std::move(run)};
    }
    model::RichLine out;
    out.reserve(parsed.size());
    for (const auto& sp : parsed) {
        if (!sp.text.empty()) {
            out.push_back(sp);
        }
    }
    if (out.empty()) {
        out.push_back({});
    }
    return out;
}

namespace {

[[nodiscard]] bool value_nonempty(const std::string& v) noexcept
{
    return !util::trim_view(v).empty();
}

/// One ``{name}`` or ``{-name}`` (required: its line drops when the value is
/// empty) in a line of template text.
struct Token {
    std::size_t begin = 0;  ///< the '{'
    std::size_t end = 0;    ///< one past the '}'
    std::string key;
    bool required = false;
};

/// The first token at or after ``from`` (see ``util::next_brace_span``).
[[nodiscard]] std::optional<Token> next_token(std::string_view line, std::size_t from)
{
    const auto span = util::next_brace_span(line, from);
    if (!span) {
        return std::nullopt;
    }
    Token t;
    t.begin = span->begin;
    t.end = span->end;
    auto inner = util::trim_view(span->inner(line));
    if (!inner.empty() && inner.front() == '-') {
        t.required = true;
        inner.remove_prefix(1);
    }
    t.key = token_key(inner);
    return t;
}

[[nodiscard]] bool line_has_required_empty_token(
    std::string_view line,
    const std::map<std::string, std::string>& values)
{
    for (auto t = next_token(line, 0); t; t = next_token(line, t->end)) {
        if (!t->required) {
            continue;
        }
        const auto it = values.find(t->key);
        if (it != values.end() && !value_nonempty(it->second)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string token_key(std::string_view name)
{
    return util::to_lower(util::trim_view(name));
}

double clamp_overlay_line_height(double raw) noexcept
{
    if (!std::isfinite(raw)) {
        return canvas::k_default_line_height;
    }
    return std::round(std::clamp(raw, k_min_line_height, k_max_line_height) * 1000.0) / 1000.0;
}

int clamp_overlay_line_gap(int raw) noexcept
{
    return std::clamp(raw, 0, k_max_line_gap);
}

double clamp_overlay_letter_spacing(double raw) noexcept
{
    return std::round(clamp_letter_spacing_px(raw) * 1000.0) / 1000.0;
}

std::string substitute_variables(
    std::string_view text,
    const std::map<std::string, std::string>& values)
{
    std::string result;
    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        auto line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        const auto line = text.substr(line_start, line_end - line_start);
        line_start = line_end + 1;

        if (line_has_required_empty_token(line, values)) {
            continue;
        }

        // Substitute known tokens (including empty -> ""); leave unknown braces.
        std::string out_line;
        std::size_t pos = 0;
        for (auto t = next_token(line, 0); t; t = next_token(line, pos)) {
            out_line.append(line.substr(pos, t->begin - pos));
            if (const auto it = values.find(t->key); it != values.end()) {
                out_line += util::trim_view(it->second);
            } else {
                out_line.append(line.substr(t->begin, t->end - t->begin));
            }
            pos = t->end;
        }
        out_line.append(line.substr(pos));

        if (!result.empty()) {
            result.push_back('\n');
        }
        result += out_line;
    }
    return result;
}

std::map<std::string, std::string> song_token_values(
    const std::map<std::string, std::string>& song_vars)
{
    std::map<std::string, std::string> ctx;
    for (const auto name : k_template_variables) {
        ctx[token_key(name)] = "";
    }
    for (const auto& [name, val] : song_vars) {
        ctx[token_key(name)] = val;
    }
    return ctx;
}

std::map<std::string, std::string> song_template_vars(const model::Song& song,
                                                     std::string_view ccli_licence)
{
    std::map<std::string, std::string> vars;
    vars["song-title"] = song.title;
    vars["songbook"] = song.songbook_name;
    vars["songbook-number"] = song.songbook_entry;
    vars["authors"] = model::join_author_names(song.authors);
    vars["key"] = song.key;
    vars["copyright"] = song.copyright;
    vars["ccli-number"] = song.ccli_song_number;
    vars["ccli-license"] = std::string(ccli_licence);
    return vars;
}

OverlayScene build_overlay_scene(
    const std::vector<OverlayBox>& templates,
    const std::map<std::string, std::string>& token_values)
{
    OverlayScene scene;
    for (const auto& tpl : templates) {
        OverlayBox ob = tpl;
        ob.lines.clear();
        canvas::fill_type_defaults(ob);
        ob.line_height = clamp_overlay_line_height(tpl.line_height);
        ob.line_gap = clamp_overlay_line_gap(tpl.line_gap);
        ob.letter_spacing = clamp_overlay_letter_spacing(tpl.letter_spacing);

        for (const auto& ln : tpl.lines) {
            if (line_has_required_empty_token(model::plain_text(ln), token_values)) {
                continue;
            }
            model::RichLine out;
            out.reserve(ln.size());
            std::string joined;
            for (const auto& sp : ln) {
                model::TextSpan run = sp;
                run.text = substitute_variables(sp.text, token_values);
                if (run.style.size_pct <= 0) {
                    run.style.size_pct = 100;
                }
                joined += run.text;
                if (!run.text.empty()) {
                    out.push_back(std::move(run));
                }
            }
            if (!value_nonempty(joined)) {
                continue;
            }
            if (out.empty()) {
                model::TextSpan run;
                run.text = joined;
                if (!ln.empty()) {
                    run.style = ln.front().style;
                }
                out.push_back(std::move(run));
            }
            ob.lines.push_back(std::move(out));
        }
        if (!ob.lines.empty()) {
            scene.boxes.push_back(std::move(ob));
        }
    }
    return scene;
}

OverlayScene build_title_overlay_scene(
    const std::vector<OverlayBox>& templates,
    const std::map<std::string, std::string>& song_vars)
{
    return build_overlay_scene(templates, song_token_values(song_vars));
}

std::vector<OverlayScene> assign_overlay_to_title_slides(
    const std::vector<SlideGroup>& groups,
    const OverlayScene& overlay)
{
    std::vector<OverlayScene> out;
    out.reserve(groups.size());
    for (const auto& g : groups) {
        if (g.is_title) {
            out.push_back(overlay);
        } else {
            out.push_back(OverlayScene{});
        }
    }
    return out;
}

}  // namespace seventhslide::layout
