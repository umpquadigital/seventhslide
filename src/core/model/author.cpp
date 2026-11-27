#include <seventhslide/model/author.hpp>

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::model {
namespace {

/// Lower-case, letters and digits only - so "Words & Music", "words_music" and
/// "wordsMusic" all fold to the same key.
[[nodiscard]] std::string fold(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (util::is_alnum(c)) {
            out.push_back(util::to_lower(c));
        }
    }
    return out;
}

[[nodiscard]] std::string str_field(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key) || !j[key].is_string()) {
        return {};
    }
    return util::trim(j[key].get<std::string>());
}

}  // namespace

void Author::add_role(AuthorRole role)
{
    role.type = normalize_author_role_type(role.type);
    if (role.type.empty()) {
        return;
    }
    if (role.type != k_author_role_other) {
        role.label.clear();
    } else if (role.label.empty()) {
        return;  // "other" says nothing without its wording.
    }
    if (role.type != k_author_role_translation) {
        role.lang.clear();
    }
    const auto same = [&role](const AuthorRole& existing) {
        // One credit per type, except ``other``, which is distinguished by its
        // wording (a song may credit both an adapter and a paraphraser).
        return existing.type == role.type
            && (role.type != k_author_role_other || existing.label == role.label);
    };
    if (std::any_of(roles.begin(), roles.end(), same)) {
        return;
    }
    roles.push_back(std::move(role));
}

std::string normalize_author_role_type(std::string_view raw)
{
    const auto key = fold(raw);
    if (key.empty()) {
        return {};
    }
    if (key == "words" || key == "word" || key == "lyrics" || key == "lyricist"
        || key == "text") {
        return std::string(k_author_role_words);
    }
    if (key == "music" || key == "composer" || key == "composition" || key == "tune"
        || key == "melody") {
        return std::string(k_author_role_music);
    }
    if (key == "translation" || key == "translator" || key == "translated") {
        return std::string(k_author_role_translation);
    }
    if (key == "arrangement" || key == "arranger" || key == "arranged" || key == "arr") {
        return std::string(k_author_role_arrangement);
    }
    if (key == "other" || key == "custom") {
        return std::string(k_author_role_other);
    }
    return {};
}

std::vector<AuthorRole> author_roles_from_type_string(std::string_view raw)
{
    const auto key = fold(raw);
    if (key.empty()) {
        return {};
    }
    if (key == "wordsmusic" || key == "wordsandmusic") {
        return {AuthorRole{std::string(k_author_role_words), {}, {}},
                AuthorRole{std::string(k_author_role_music), {}, {}}};
    }
    if (auto type = normalize_author_role_type(raw); !type.empty()) {
        return {AuthorRole{std::move(type), {}, {}}};
    }
    // Unknown vocabulary from another program: keep the wording rather than the
    // credit being silently lost.
    return {AuthorRole{std::string(k_author_role_other), util::trim(raw), {}}};
}

std::vector<Author> authors_from_json(const nlohmann::json& value)
{
    std::vector<Author> out;
    if (!value.is_array()) {
        return out;
    }
    for (const auto& el : value) {
        auto name = str_field(el, "name");
        if (name.empty()) {
            continue;
        }
        Author author(std::move(name));
        if (const auto roles = el.find("roles"); roles != el.end() && roles->is_array()) {
            for (const auto& role : *roles) {
                author.add_role(AuthorRole{
                    str_field(role, "type"), str_field(role, "label"), str_field(role, "lang")});
            }
        }
        out.push_back(std::move(author));
    }
    return out;
}

nlohmann::json authors_to_json(const std::vector<Author>& authors)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& author : authors) {
        if (author.name.empty()) {
            continue;
        }
        nlohmann::json roles = nlohmann::json::array();
        for (const auto& role : author.roles) {
            nlohmann::json role_j{{"type", role.type}};
            if (!role.label.empty()) {
                role_j["label"] = role.label;
            }
            if (!role.lang.empty()) {
                role_j["lang"] = role.lang;
            }
            roles.push_back(std::move(role_j));
        }
        arr.push_back(nlohmann::json{{"name", author.name}, {"roles", std::move(roles)}});
    }
    return arr;
}

std::string join_author_names(const std::vector<Author>& authors, std::string_view separator)
{
    std::string out;
    for (const auto& author : authors) {
        if (author.name.empty()) {
            continue;
        }
        if (!out.empty()) {
            out.append(separator);
        }
        out.append(author.name);
    }
    return out;
}

}  // namespace seventhslide::model
