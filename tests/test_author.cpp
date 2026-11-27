#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/author.hpp>

#include <string>
#include <vector>

using seventhslide::model::Author;
using seventhslide::model::AuthorRole;
using seventhslide::model::author_roles_from_type_string;
using seventhslide::model::authors_from_json;
using seventhslide::model::authors_to_json;
using seventhslide::model::join_author_names;
using seventhslide::model::k_author_role_arrangement;
using seventhslide::model::k_author_role_music;
using seventhslide::model::k_author_role_other;
using seventhslide::model::k_author_role_translation;
using seventhslide::model::k_author_role_words;
using seventhslide::model::normalize_author_role_type;

namespace {

AuthorRole role(std::string_view type, std::string label = {}, std::string lang = {})
{
    return AuthorRole{std::string(type), std::move(label), std::move(lang)};
}

}  // namespace

TEST_CASE("Role names normalise across the vocabularies other apps use", "[model][author]")
{
    REQUIRE(normalize_author_role_type("Words") == k_author_role_words);
    REQUIRE(normalize_author_role_type("lyrics") == k_author_role_words);
    REQUIRE(normalize_author_role_type("Composer") == k_author_role_music);
    REQUIRE(normalize_author_role_type("translator") == k_author_role_translation);
    REQUIRE(normalize_author_role_type("Arranged") == k_author_role_arrangement);
    REQUIRE(normalize_author_role_type("arr.") == k_author_role_arrangement);
    REQUIRE(normalize_author_role_type("").empty());
    REQUIRE(normalize_author_role_type("paraphrase").empty());
}

TEST_CASE("wordsMusic expands to the two roles it always meant", "[model][author]")
{
    REQUIRE(author_roles_from_type_string("wordsMusic")
            == std::vector<AuthorRole>{role(k_author_role_words), role(k_author_role_music)});
    REQUIRE(author_roles_from_type_string("words and music")
            == std::vector<AuthorRole>{role(k_author_role_words), role(k_author_role_music)});
    REQUIRE(author_roles_from_type_string("music") == std::vector<AuthorRole>{role(k_author_role_music)});
    REQUIRE(author_roles_from_type_string("").empty());
}

TEST_CASE("An unknown role keeps its wording instead of being dropped", "[model][author]")
{
    const auto roles = author_roles_from_type_string("Paraphrase");
    REQUIRE(roles == std::vector<AuthorRole>{role(k_author_role_other, "Paraphrase")});
}

TEST_CASE("add_role normalises, de-duplicates and drops empty credits", "[model][author]")
{
    Author a{"Ada"};
    a.add_role(role("Words"));
    a.add_role(role("lyrics"));  // same credit, spelled differently
    a.add_role(role(""));
    a.add_role(role(k_author_role_other));  // no wording: says nothing
    REQUIRE(a.roles == std::vector<AuthorRole>{role(k_author_role_words)});

    a.add_role(role(k_author_role_other, "Adapted by"));
    a.add_role(role(k_author_role_other, "Paraphrased by"));
    a.add_role(role(k_author_role_other, "Adapted by"));
    REQUIRE(a.roles.size() == 3);
    REQUIRE(a.roles[1].type == k_author_role_other);

    // ``lang`` belongs to a translation and is cleared off anything else.
    Author b{"Bo"};
    b.add_role(role(k_author_role_music, {}, "es"));
    b.add_role(role(k_author_role_translation, {}, "es"));
    REQUIRE(b.roles[0].lang.empty());
    REQUIRE(b.roles[1].lang == "es");
}

TEST_CASE("Authors round-trip through their one stored shape", "[model][author]")
{
    const std::vector<Author> authors{
        Author{"Ada", {role(k_author_role_words), role(k_author_role_music)}},
        Author{"Bo", {role(k_author_role_translation, {}, "es")}},
        Author{"Cy"}};
    const auto j = authors_to_json(authors);
    REQUIRE(j[0] == nlohmann::json({{"name", "Ada"},
                                    {"roles", {{{"type", "words"}}, {{"type", "music"}}}}}));
    REQUIRE(j[1]["roles"][0] == nlohmann::json({{"type", "translation"}, {"lang", "es"}}));
    REQUIRE(j[2] == nlohmann::json({{"name", "Cy"}, {"roles", nlohmann::json::array()}}));
    REQUIRE(authors_from_json(j) == authors);
}

TEST_CASE("Reading authors tolerates malformed entries", "[model][author]")
{
    const auto read = authors_from_json(nlohmann::json::array({
        {{"name", "  Ada  "}, {"roles", {{{"type", "Lyrics"}}, "music", {{"type", 3}}}}},
        {{"name", "Bo"}, {"roles", "words"}},
    }));
    REQUIRE(read.size() == 2);
    REQUIRE(read[0].name == "Ada");
    // Types are normalised like any added role; entries that are not role
    // objects, or name no type, are skipped.
    REQUIRE(read[0].roles == std::vector<AuthorRole>{role(k_author_role_words)});
    REQUIRE(read[1].roles.empty());
    // Nameless and non-object entries are skipped, not turned into blank authors.
    REQUIRE(authors_from_json(nlohmann::json::array({"Ada", {{"roles", {}}}, 7})).empty());
    REQUIRE(authors_from_json(nlohmann::json::object()).empty());
    REQUIRE(authors_from_json(nullptr).empty());
}

TEST_CASE("join_author_names skips blanks", "[model][author]")
{
    const std::vector<Author> authors{"Ada", "", "Bo"};
    REQUIRE(join_author_names(authors) == "Ada, Bo");
    REQUIRE(join_author_names(authors, " • ") == "Ada • Bo");
    REQUIRE(join_author_names({}).empty());
}
