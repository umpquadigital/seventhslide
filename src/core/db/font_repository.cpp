#include "seventhslide/db/font_repository.hpp"

#include "seventhslide/db/query.hpp"
#include "seventhslide/db/schema.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <functional>
#include <utility>

namespace seventhslide::db {
namespace {

constexpr auto k_columns = "filename, display_name, family, style, bold, italic, variable, added_at";
constexpr auto k_order = " ORDER BY family COLLATE NOCASE, bold, italic, added_at DESC, filename";

[[nodiscard]] LibraryFont font_from(const QSqlQuery& q)
{
    LibraryFont f;
    f.filename = q.value(0).toString().toStdString();
    f.display_name = q.value(1).toString().toStdString();
    f.family = q.value(2).toString().toStdString();
    f.style = q.value(3).toString().toStdString();
    f.bold = q.value(4).toInt() != 0;
    f.italic = q.value(5).toInt() != 0;
    f.variable = q.value(6).toInt() != 0;
    f.added_at = q.value(7).toLongLong();
    return f;
}

[[nodiscard]] std::vector<LibraryFont> fonts_from(QSqlQuery q)
{
    std::vector<LibraryFont> out;
    while (q.next()) {
        out.push_back(font_from(q));
    }
    return out;
}

/// The names in the ``<font=Name>`` tags of rich text, as a slide reads them.
void note_font_tags(std::string_view text, std::set<std::string>& out)
{
    for (auto at = text.find('<'); at != std::string_view::npos; at = text.find('<', at + 1)) {
        const auto tag = model::match_markup_tag(text.substr(at));
        if (tag && tag->name == "font" && !tag->value.empty()) {
            out.insert(tag->value);
        }
    }
}

/// The families in a document's JSON, however it is nested. Not recursive: a row
/// that came by sync could nest as deep as it likes.
void note_font_names(const nlohmann::json& document, std::set<std::string>& out)
{
    std::vector<const nlohmann::json*> open{&document};
    while (!open.empty()) {
        const auto* value = open.back();
        open.pop_back();
        if (value->is_string()) {
            note_font_tags(value->get_ref<const std::string&>(), out);
        } else if (value->is_array()) {
            for (const auto& item : *value) {
                open.push_back(&item);
            }
        } else if (value->is_object()) {
            for (const auto& [key, item] : value->items()) {
                if ((key == "font_family" || key == "verse_font_family") && item.is_string()) {
                    if (!item.get_ref<const std::string&>().empty()) {
                        out.insert(item.get<std::string>());
                    }
                } else {
                    open.push_back(&item);
                }
            }
        }
    }
}

/// Each thing in the library that can name a family, with the families it
/// names: the outputs' themes, the outputs, profiles, songs, Freeform items,
/// then service rows, each in the order the library lists it. What names none
/// is passed over.
void for_each_naming(Database& db,
                     const std::function<void(const FontUse&, const std::set<std::string>&)>& take)
{
    const auto parsed = [](const QVariant& text) {
        return nlohmann::json::parse(text.toString().toStdString(), nullptr, false);
    };
    const auto give = [&take](const FontUse& use, const std::set<std::string>& families) {
        if (!families.empty()) {
            take(use, families);
        }
    };

    // An output's themes are in its data: each is told of apart, and the
    // output for what it names outside them, after every output's themes.
    std::vector<std::pair<FontUse, std::set<std::string>>> outputs_named;
    auto outputs = exec(db,
                        QStringLiteral("SELECT data FROM outputs WHERE data LIKE '%font%' "
                                       "ORDER BY order_num, id"),
                        {}, "read which fonts the outputs name");
    while (outputs.next()) {
        auto document = parsed(outputs.value(0));
        if (!document.is_object()) {
            continue;
        }
        const auto output = json::str_or(document, "name");
        for (const auto& list : model::k_output_theme_lists) {
            const auto key = std::string(list.key);
            const auto themes = document.find(key);
            if (themes == document.end() || !themes->is_array()) {
                continue;
            }
            for (const auto& theme : *themes) {
                if (theme.is_object()) {
                    give(FontUse{FontUse::Kind::Theme, json::str_or(theme, "name"), output,
                                 json::str_or(theme, "id"), std::string(list.kind)},
                         font_families_named_in(theme));
                }
            }
            document.erase(key);
        }
        outputs_named.emplace_back(
            FontUse{FontUse::Kind::Output, output, {}, json::str_or(document, "id"), {}},
            font_families_named_in(document));
    }
    for (const auto& [use, families] : outputs_named) {
        give(use, families);
    }

    auto profiles = exec(db,
                         QStringLiteral("SELECT data, name, id FROM style_profiles "
                                        "WHERE data LIKE '%font%' "
                                        "ORDER BY sort_order, name COLLATE NOCASE, id"),
                         {}, "read which fonts the profiles name");
    while (profiles.next()) {
        const auto document = parsed(profiles.value(0));
        if (!document.is_discarded()) {
            give(FontUse{FontUse::Kind::Profile, profiles.value(1).toString().toStdString(), {},
                         profiles.value(2).toString().toStdString(), {}},
                 font_families_named_in(document));
        }
    }

    auto songs = exec(db,
                      QStringLiteral("SELECT lyrics, title, id FROM songs WHERE lyrics LIKE '%<font=%' "
                                     "ORDER BY title COLLATE NOCASE, id"),
                      {}, "read which fonts the songs name");
    while (songs.next()) {
        std::set<std::string> families;
        note_font_tags(songs.value(0).toString().toStdString(), families);
        give(FontUse{FontUse::Kind::Song, songs.value(1).toString().toStdString(), {},
                     songs.value(2).toString().toStdString(), {}},
             families);
    }

    auto freeform = exec(db,
                         QStringLiteral("SELECT document, name, id FROM freeform_items "
                                        "WHERE document LIKE '%font%' "
                                        "ORDER BY name COLLATE NOCASE, id"),
                         {}, "read which fonts the Freeform items name");
    while (freeform.next()) {
        const auto document = parsed(freeform.value(0));
        if (!document.is_discarded()) {
            // A Freeform item's name can carry markup, as the rundown's titles do.
            const auto name = model::strip_markup_tags(freeform.value(1).toString().toStdString());
            give(FontUse{FontUse::Kind::Freeform, util::trim(name), {},
                         freeform.value(2).toString().toStdString(), {}},
                 font_families_named_in(document));
        }
    }

    auto items = exec(db,
                      QStringLiteral("SELECT i.data, i.item_type, s.name, i.id FROM service_items i "
                                     "LEFT JOIN services s ON s.id = i.service_id "
                                     "WHERE i.data LIKE '%font%' "
                                     "ORDER BY s.service_at DESC, s.id, i.order_num, i.id"),
                      {}, "read which fonts the services name");
    while (items.next()) {
        auto document = parsed(items.value(0));
        if (document.is_discarded()) {
            continue;
        }
        auto families = font_families_named_in(document);
        model::ServiceItem item;
        item.item_type = items.value(1).toString().toStdString();
        item.data = std::move(document);
        give(FontUse{FontUse::Kind::ServiceItem, item.title(), items.value(2).toString().toStdString(),
                     items.value(3).toString().toStdString(), item.item_type},
             families);
    }
}

}  // namespace

std::set<std::string> font_families_named_in(const nlohmann::json& document)
{
    std::set<std::string> out;
    note_font_names(document, out);
    return out;
}

void FontRepository::add(const LibraryFont& font)
{
    exec(db_,
         QStringLiteral("INSERT INTO font_files (%1) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(filename) DO UPDATE SET display_name = excluded.display_name, "
                        "family = excluded.family, style = excluded.style, bold = excluded.bold, "
                        "italic = excluded.italic, variable = excluded.variable, "
                        "added_at = excluded.added_at")
             .arg(QLatin1String(k_columns)),
         {sql_text(font.filename), sql_text(font.display_name), sql_text(font.family),
          sql_text(font.style), font.bold ? 1 : 0, font.italic ? 1 : 0, font.variable ? 1 : 0,
          static_cast<qint64>(font.added_at)},
         "add a font");
}

bool FontRepository::remove(std::string_view filename)
{
    auto q = exec(db_, QStringLiteral("DELETE FROM font_files WHERE filename = ?"),
                  {sql_text(filename)}, "remove a font");
    return q.numRowsAffected() > 0;
}

std::optional<LibraryFont> FontRepository::get(std::string_view filename)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT %1 FROM font_files WHERE filename = ?")
                      .arg(QLatin1String(k_columns)),
                  {sql_text(filename)}, "read a font");
    if (!q.next()) {
        return std::nullopt;
    }
    return font_from(q);
}

std::vector<LibraryFont> FontRepository::list_all()
{
    return fonts_from(exec(db_,
                           QStringLiteral("SELECT %1 FROM font_files%2")
                               .arg(QLatin1String(k_columns), QLatin1String(k_order)),
                           {}, "list the fonts"));
}

std::vector<LibraryFont> FontRepository::list_faces()
{
    std::vector<LibraryFont> out;
    for (auto& font : list_all()) {
        const bool same_slot = !out.empty() && util::to_lower(out.back().family) == util::to_lower(font.family)
                               && out.back().bold == font.bold && out.back().italic == font.italic;
        if (!same_slot) {
            out.push_back(std::move(font));
        }
    }
    return out;
}

std::vector<LibraryFont> FontRepository::list_family(std::string_view family)
{
    return fonts_from(exec(db_,
                           QStringLiteral("SELECT %1 FROM font_files "
                                          "WHERE family = ? COLLATE NOCASE%2")
                               .arg(QLatin1String(k_columns), QLatin1String(k_order)),
                           {sql_text(family)}, "list a font family"));
}

std::set<std::string> FontRepository::families_named()
{
    std::set<std::string> out;
    for_each_naming(db_, [&out](const FontUse& /*use*/, const std::set<std::string>& families) {
        out.insert(families.begin(), families.end());
    });
    return out;
}

std::vector<FontUse> FontRepository::uses_of(std::string_view family)
{
    const auto wanted = util::to_lower(family);
    std::vector<FontUse> out;
    for_each_naming(db_, [&](const FontUse& use, const std::set<std::string>& families) {
        const auto is_wanted = [&wanted](const std::string& named) {
            return util::to_lower(named) == wanted;
        };
        if (std::ranges::any_of(families, is_wanted)) {
            out.push_back(use);
        }
    });
    return out;
}

bool FontRepository::bundled_taken()
{
    return exec(db_, QStringLiteral("SELECT 1 FROM library_meta WHERE key = ?"),
                {sql_text(k_bundled_fonts_key)}, "read whether the shipped fonts were copied in")
        .next();
}

void FontRepository::note_bundled_taken()
{
    exec(db_,
         QStringLiteral("INSERT INTO library_meta (key, value) VALUES (?, '1') "
                        "ON CONFLICT(key) DO NOTHING"),
         {sql_text(k_bundled_fonts_key)}, "note that the shipped fonts were copied in");
}

}  // namespace seventhslide::db
