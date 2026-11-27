#include "seventhslide/freeform/tokens.hpp"

#include "seventhslide/util/strings.hpp"

#include <concepts>
#include <unordered_set>
#include <utility>
#include <type_traits>

namespace seventhslide::freeform {
namespace {

bool is_name_char(unsigned char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/// The one brace walk. Calls ``fn(start, end, name, hide)`` for every well-formed
/// ``{name}`` / ``{-name}``; ``[start, end)`` spans the braces. Anything else -
/// an unbalanced brace, a nested one, a newline inside, an illegal name - is
/// skipped silently and stays literal text for every caller.
void for_each_token(std::string_view text, auto&& fn)
{
    for (auto span = util::next_brace_span(text, 0); span;
         span = util::next_brace_span(text, span->end)) {
        auto inner = span->inner(text);
        bool hide = false;
        if (!inner.empty() && inner.front() == '-') {
            hide = true;
            inner.remove_prefix(1);
        }
        if (is_valid_field_name(inner)) {
            fn(span->begin, span->end, inner, hide);
        }
    }
}

/// Every text string a token can live in: text objects on every deck's slides,
/// plus the cue list while line control owns the lines.
/// ``Doc`` is ``Document`` or ``const Document``; ``fn`` gets the text with
/// the same constness.
template <class Doc>
    requires std::same_as<std::remove_const_t<Doc>, Document>
void for_each_document_text(Doc& doc, auto&& fn)
{
    for (auto& deck : doc.decks) {
        for (auto& slide : deck.slides) {
            for (auto& obj : slide.objects) {
                if (obj.type == ObjectType::Text) {
                    fn(obj.text);
                } else if (obj.type == ObjectType::Table) {
                    for (auto& cell : obj.table.cells) {
                        fn(cell);
                    }
                }
            }
        }
    }
    if (doc.line_control) {
        for (auto& cue : doc.cues) {
            fn(cue.text);
        }
    }
}

struct LineHit {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string name;
    bool hide = false;
};

} // namespace

bool is_valid_field_name(std::string_view name) noexcept
{
    if (name.empty() || name.size() > static_cast<std::size_t>(k_max_field_name)) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(name[0]);
    if (first < 'a' || first > 'z') {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!is_name_char(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

std::string sanitize_field_name(std::string_view raw)
{
    const auto trimmed = util::trim_view(raw);
    std::string out;
    out.reserve(trimmed.size());
    for (const char ch : trimmed) {
        const char c = util::to_lower(ch);
        if (is_name_char(static_cast<unsigned char>(c))) {
            out.push_back(c);
        } else if (util::is_space(c)) {
            out.push_back('_');
        }
    }
    while (!out.empty() && (out.front() < 'a' || out.front() > 'z')) {
        out.erase(out.begin());
    }
    if (out.size() > static_cast<std::size_t>(k_max_field_name)) {
        out.resize(static_cast<std::size_t>(k_max_field_name));
    }
    if (!is_valid_field_name(out)) {
        return {};
    }
    return out;
}

std::string substitute_fields(std::string_view text,
                              const std::map<std::string, std::string>& values)
{
    std::string result;
    std::size_t line_start = 0;
    std::size_t emitted = 0;
    std::vector<LineHit> hits;
    while (line_start <= text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        const std::string_view line = text.substr(line_start, line_end - line_start);

        hits.clear();
        for_each_token(line, [&](std::size_t b, std::size_t e, std::string_view name, bool hide) {
            hits.push_back(LineHit{b, e, std::string(name), hide});
        });

        // A ``{-token}`` whose value is set but blank takes its whole line away.
        bool drop = false;
        for (const auto& hit : hits) {
            if (!hit.hide) {
                continue;
            }
            const auto it = values.find(hit.name);
            if (it != values.end() && util::trim_view(it->second).empty()) {
                drop = true;
                break;
            }
        }

        if (!drop) {
            std::string out_line;
            std::size_t pos = 0;
            for (const auto& hit : hits) {
                out_line.append(line.substr(pos, hit.start - pos));
                const auto it = values.find(hit.name);
                if (it != values.end()) {
                    out_line.append(util::trim_view(it->second));
                } else {
                    // No value declared for this name: leave the token alone.
                    out_line.append(line.substr(hit.start, hit.end - hit.start));
                }
                pos = hit.end;
            }
            out_line.append(line.substr(pos));

            if (emitted > 0) {
                result.push_back('\n');
            }
            result += out_line;
            ++emitted;
        }

        if (line_end >= text.size()) {
            break;
        }
        line_start = line_end + 1;
    }
    return result;
}

std::vector<TokenHit> collect_document_tokens(const Document& doc)
{
    std::vector<TokenHit> out;
    std::unordered_set<std::string> seen;
    for_each_document_text(doc, [&](std::string_view text) {
        for_each_token(text, [&](std::size_t, std::size_t, std::string_view name, bool hide) {
            if (seen.insert(std::string(name)).second) {
                out.push_back(TokenHit{std::string(name), hide});
                return;
            }
            if (!hide) {
                return;
            }
            for (auto& hit : out) {
                if (hit.name == name) {
                    hit.hide_line_if_empty = true;
                    return;
                }
            }
        });
    });
    return out;
}

std::vector<std::string> collect_field_names(const Document& doc)
{
    std::vector<std::string> names;
    for (auto& hit : collect_document_tokens(doc)) {
        names.push_back(std::move(hit.name));
    }
    return names;
}

int count_field_usage(const Document& doc, std::string_view name)
{
    int count = 0;
    for_each_document_text(doc, [&](std::string_view text) {
        for_each_token(text, [&](std::size_t, std::size_t, std::string_view hit, bool) {
            if (hit == name) {
                ++count;
            }
        });
    });
    return count;
}

int rewrite_field_token(Document& doc, std::string_view old_name, std::string_view new_name)
{
    if (!is_valid_field_name(old_name)) {
        return 0;
    }
    const bool erase = new_name.empty();
    if (!erase && !is_valid_field_name(new_name)) {
        return 0;
    }
    int changed = 0;
    std::vector<LineHit> hits;
    for_each_document_text(doc, [&](std::string& text) {
        hits.clear();
        for_each_token(text, [&](std::size_t b, std::size_t e, std::string_view hit, bool hide) {
            if (hit == old_name) {
                hits.push_back(LineHit{b, e, std::string(hit), hide});
            }
        });
        // Back to front, so the offsets ahead of each edit stay valid.
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            std::string replacement;
            if (!erase) {
                replacement.push_back('{');
                if (it->hide) {
                    replacement.push_back('-');
                }
                replacement.append(new_name);
                replacement.push_back('}');
            }
            text.replace(it->start, it->end - it->start, replacement);
            ++changed;
        }
    });
    return changed;
}

void prune_invalid_fields(Document& doc)
{
    for (auto it = doc.fields.begin(); it != doc.fields.end();) {
        if (!is_valid_field_name(it->first)) {
            it = doc.fields.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace seventhslide::freeform
