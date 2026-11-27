// OpenLyrics export; see song_xml.hpp. The reader is song_xml.cpp, and every
// choice here is one it reads back.

#include <seventhslide/parse/song_xml.hpp>

#include <seventhslide/model/author.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/util/strings.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace seventhslide::parse {
namespace {

constexpr const char* k_openlyrics_namespace = "http://openlyrics.info/namespace/2009/song";
/// 0.8: the version whose ``<chord name="...">`` the reader takes.
constexpr const char* k_openlyrics_version = "0.8";
/// The ``application`` this file's formatting tags belong to.
constexpr const char* k_format_application = "SeventhSlide";

/// One formatting tag the lyrics use: the markup it opens and closes.
struct FormatTag {
    std::string open;
    std::string close;
};

/// The formatting tags a song's lyrics use, by name, for its ``<format>``.
class FormatTags {
public:
    /// The tags ``style`` needs, outermost first, in the order the app's own
    /// markup nests them (see ``model::format_inline_markup``).
    [[nodiscard]] std::vector<std::string> tags_for(const model::TextStyle& style)
    {
        std::vector<std::string> names;
        const auto use = [&](std::string name, const model::TextStyle& only) {
            if (!tags_.contains(name)) {
                auto tag = markup_around(only);
                if (tag.open.empty()) {
                    return;  // a value the markup cannot carry (a malformed colour)
                }
                tags_.emplace(name, std::move(tag));
            }
            names.push_back(std::move(name));
        };
        if (!style.color.empty()) {
            model::TextStyle only;
            only.color = style.color;
            std::string name = "color-";
            for (const char ch : style.color) {
                if (util::is_alnum(ch)) {
                    name.push_back(util::to_lower(ch));
                }
            }
            use(std::move(name), only);
        }
        if (!style.font_family.empty()) {
            model::TextStyle only;
            only.font_family = style.font_family;
            auto [it, added] = fonts_.try_emplace(style.font_family, fonts_.size() + 1);
            use("font-" + std::to_string(it->second), only);
        }
        if (style.size_pct != 100) {
            model::TextStyle only;
            only.size_pct = style.size_pct;
            use("size-" + std::to_string(style.size_pct), only);
        }
        if (style.baseline_rise_pct != 0) {
            model::TextStyle only;
            only.baseline_rise_pct = style.baseline_rise_pct;
            use(style.baseline_rise_pct == model::k_super_rise_pct
                    ? std::string("super")
                    : "rise-" + std::to_string(style.baseline_rise_pct),
                only);
        }
        if (style.bold) {
            model::TextStyle only;
            only.bold = true;
            use("b", only);
        }
        if (style.italic) {
            model::TextStyle only;
            only.italic = true;
            use("i", only);
        }
        if (style.underline) {
            model::TextStyle only;
            only.underline = true;
            use("u", only);
        }
        return names;
    }

    [[nodiscard]] bool empty() const noexcept { return tags_.empty(); }

    void write(pugi::xml_node format) const
    {
        auto tags = format.append_child("tags");
        tags.append_attribute("application") = k_format_application;
        for (const auto& [name, tag] : tags_) {
            auto node = tags.append_child("tag");
            node.append_attribute("name") = name.c_str();
            node.append_child("open").text() = tag.open.c_str();
            if (!tag.close.empty()) {
                node.append_child("close").text() = tag.close.c_str();
            }
        }
    }

private:
    /// The app's markup for ``style`` on either side of a run's text.
    [[nodiscard]] static FormatTag markup_around(const model::TextStyle& style)
    {
        constexpr char k_mark = '\x1f';  // never in lyrics, and markup leaves it be
        const auto wrapped = model::format_inline_markup({model::TextSpan{std::string(1, k_mark), style}});
        const auto at = wrapped.find(k_mark);
        if (at == std::string::npos) {
            return {};
        }
        return {wrapped.substr(0, at), wrapped.substr(at + 1)};
    }

    std::map<std::string, FormatTag> tags_;
    std::map<std::string, std::size_t> fonts_;
};

/// Run text into ``parent``: words as text, each ``[X]`` (a ``[`` with a
/// ``]`` after it, as chord layout reads them) as ``<chord name="X"/>``.
void append_run_text(pugi::xml_node parent, std::string_view text)
{
    std::size_t start = 0;
    const auto words = [&](std::size_t end) {
        if (end > start) {
            parent.append_child(pugi::node_pcdata).set_value(std::string(text.substr(start, end - start)).c_str());
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '[') {
            continue;
        }
        const auto close = text.find(']', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        words(i);
        parent.append_child("chord").append_attribute("name") =
            std::string(text.substr(i + 1, close - i - 1)).c_str();
        i = close;
        start = close + 1;
    }
    words(text.size());
}

/// One lyric line (stored markup) into ``lines``, after a ``<br/>`` unless first.
void append_line(pugi::xml_node lines, std::string_view markup, bool first, FormatTags& formats)
{
    if (!first) {
        lines.append_child("br");
    }
    for (const auto& span : model::parse_inline_markup(markup)) {
        if (span.text.empty()) {
            continue;
        }
        pugi::xml_node into = lines;
        for (const auto& name : formats.tags_for(span.style)) {
            into = into.append_child("tag");
            into.append_attribute("name") = name.c_str();
        }
        append_run_text(into, span.text);
    }
}

/// Keep pugixml's indentation out of ``node``'s mixed content. It starts an
/// element on a new, indented line when the element follows the tag that
/// opens its parent or another element, which inside a lyric line adds words
/// of white space. An empty text node writes nothing and stops that, so one
/// goes wherever an element would otherwise start or end a parent's content
/// or follow another element.
void keep_inline(pugi::xml_node node)
{
    if (!node.first_child()) {
        return;
    }
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const auto before = child.previous_sibling();
        if (!before || before.type() == pugi::node_element) {
            node.insert_child_before(pugi::node_pcdata, child);
        }
        keep_inline(child);
    }
    if (node.last_child().type() == pugi::node_element) {
        node.append_child(pugi::node_pcdata);
    }
}

/// ``content``'s parts: the runs of lines between forced breaks.
[[nodiscard]] std::vector<std::vector<std::string_view>> verse_parts(std::string_view content)
{
    std::vector<std::vector<std::string_view>> parts(1);
    std::size_t start = 0;
    while (start <= content.size()) {
        const auto nl = content.find('\n', start);
        const auto end = nl == std::string_view::npos ? content.size() : nl;
        const auto line = content.substr(start, end - start);
        if (model::is_forced_split_marker(util::trim_view(line))) {
            parts.emplace_back();
        } else {
            parts.back().push_back(line);
        }
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    std::erase_if(parts, [](const auto& lines) { return lines.empty(); });
    return parts;
}

/// ``label`` as an XML name token (what OpenLyrics' schema takes for a verse
/// name): letters, digits, ``.``, ``-``, ``_`` and any non-ASCII letter.
[[nodiscard]] std::string name_token(std::string_view label)
{
    std::string out;
    for (const char ch : label) {
        if (util::is_alnum(ch) || ch == '.' || ch == '-' || ch == '_'
            || static_cast<unsigned char>(ch) >= 0x80) {
            out.push_back(ch);
        }
    }
    return out;
}

/// The OpenLyrics name of ``verse``: its code (``v1``, ``c2``). A label that
/// names no verse type keeps its own name, as a name token, where the reader
/// cannot take it for one (it reads a name by its first letter); otherwise
/// it is Other.
[[nodiscard]] std::string verse_name(const LabelledVerse& verse, int& unnamed)
{
    auto code = label_to_code(verse.label);
    if (code != "misc") {
        return code;
    }
    const auto label = util::trim(verse.label);
    if (label.find_first_of(" \t:") == std::string::npos) {
        auto name = name_token(label);
        if (!name.empty() && verse_type_for_code(util::to_lower(name.front())) == nullptr) {
            return name;
        }
    }
    return "o" + std::to_string(++unnamed);
}

/// The verse order as OpenLyrics names verses, without theme chips.
[[nodiscard]] std::string verse_order_names(std::string_view order)
{
    std::string out;
    for (const auto token : util::split_ws(order)) {
        const auto code = util::to_lower(token);
        if (is_theme_code(code)) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += code;
    }
    return out;
}

void append_text_child(pugi::xml_node parent, const char* name, const std::string& text)
{
    if (!text.empty()) {
        parent.append_child(name).text() = text.c_str();
    }
}

/// ``lang`` as XML Schema's ``language`` takes it (``es``, ``pt-BR``).
[[nodiscard]] bool is_language_tag(std::string_view lang)
{
    std::size_t start = 0;
    bool first = true;
    while (true) {
        const auto dash = lang.find('-', start);
        const auto part = lang.substr(start, dash == std::string_view::npos ? lang.npos : dash - start);
        if (part.empty() || part.size() > 8) {
            return false;
        }
        for (const char ch : part) {
            if (first ? !util::is_alpha(ch) : !util::is_alnum(ch)) {
                return false;
            }
        }
        if (dash == std::string_view::npos) {
            return true;
        }
        first = false;
        start = dash + 1;
    }
}

void write_authors(pugi::xml_node properties, const std::vector<model::Author>& authors)
{
    pugi::xml_node list;
    const auto add = [&](const std::string& name) {
        if (!list) {
            list = properties.append_child("authors");
        }
        auto node = list.append_child("author");
        node.text() = name.c_str();
        return node;
    };
    for (const auto& author : authors) {
        if (author.name.empty()) {
            continue;
        }
        // OpenLyrics 0.8 credits one role per element (the reader folds them
        // back into one author), and knows three: words, music, and a
        // translation into a language. A writer credited for none of those
        // (an arranger, a role in its own words) is credited plainly.
        bool credited = false;
        for (const auto& role : author.roles) {
            const bool translation =
                role.type == model::k_author_role_translation && is_language_tag(role.lang);
            if (role.type != model::k_author_role_words && role.type != model::k_author_role_music
                && !translation) {
                continue;
            }
            auto node = add(author.name);
            node.append_attribute("type") = role.type.c_str();
            if (translation) {
                node.append_attribute("lang") = role.lang.c_str();
            }
            credited = true;
        }
        if (!credited) {
            (void)add(author.name);
        }
    }
}

/// A CCLI song number as OpenLyrics' ``<ccliNo>`` takes it: digits, not all
/// of them zero.
[[nodiscard]] bool is_ccli_number(std::string_view number)
{
    return !number.empty() && std::all_of(number.begin(), number.end(), util::is_digit)
        && number.find_first_not_of('0') != std::string_view::npos;
}

void write_properties(pugi::xml_node song_node, const model::Song& song)
{
    auto properties = song_node.append_child("properties");
    properties.append_child("titles").append_child("title").text() =
        (song.title.empty() ? std::string("Untitled") : song.title).c_str();
    write_authors(properties, song.authors);
    append_text_child(properties, "copyright", song.copyright);
    const bool ccli_number = is_ccli_number(song.ccli_song_number);
    if (ccli_number) {
        append_text_child(properties, "ccliNo", song.ccli_song_number);
    }
    // A songbook needs a name; an entry alone says nothing to another program.
    if (!song.songbook_name.empty()) {
        auto book = properties.append_child("songbooks").append_child("songbook");
        book.append_attribute("name") = song.songbook_name.c_str();
        if (!song.songbook_entry.empty()) {
            book.append_attribute("entry") = song.songbook_entry.c_str();
        }
    }
    append_text_child(properties, "key", song.key);
    append_text_child(properties, "verseOrder", verse_order_names(song.verse_order));
    if (!song.topics.empty()) {
        auto themes = properties.append_child("themes");
        for (const auto& topic : song.topics) {
            themes.append_child("theme").text() = topic.c_str();
        }
    }
    // A CCLI "number" that is not one is kept where any text may go.
    const auto ccli_text = util::trim(song.ccli_song_number);
    if (!song.comments.empty() || (!ccli_number && !ccli_text.empty())) {
        auto comments = properties.append_child("comments");
        if (!song.comments.empty()) {
            comments.append_child("comment").text() = song.comments.c_str();
        }
        if (!ccli_number && !ccli_text.empty()) {
            comments.append_child("comment").text() = ("CCLI: " + ccli_text).c_str();
        }
    }
}

/// Now, as OpenLyrics' ``modifiedDate`` takes it (ISO 8601, UTC).
[[nodiscard]] std::string now_iso8601()
{
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return text;
}

}  // namespace

std::string write_song_xml(const model::Song& song,
                           std::string_view created_in,
                           std::string_view modified_date)
{
    pugi::xml_document doc;
    auto decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    auto song_node = doc.append_child("song");
    song_node.append_attribute("xmlns") = k_openlyrics_namespace;
    song_node.append_attribute("version") = k_openlyrics_version;
    // OpenLyrics 0.8's schema asks for all three.
    const std::string program = created_in.empty() ? std::string(k_format_application)
                                                   : std::string(created_in);
    song_node.append_attribute("createdIn") = program.c_str();
    song_node.append_attribute("modifiedIn") = program.c_str();
    song_node.append_attribute("modifiedDate") =
        (modified_date.empty() ? now_iso8601() : std::string(modified_date)).c_str();
    write_properties(song_node, song);

    FormatTags formats;
    auto lyrics = song_node.append_child("lyrics");
    int unnamed = 0;
    for (const auto& verse : labelled_verses(song.lyrics)) {
        const auto name = verse_name(verse, unnamed);
        const auto parts = verse_parts(verse.content);
        for (std::size_t p = 0; p < parts.size(); ++p) {
            auto node = lyrics.append_child("verse");
            // Past 26 parts the letters run out; the rest join the last.
            const auto part_name =
                parts.size() == 1 ? name
                                  : name + static_cast<char>('a' + std::min<std::size_t>(p, 25));
            node.append_attribute("name") = part_name.c_str();
            auto lines = node.append_child("lines");
            for (std::size_t l = 0; l < parts[p].size(); ++l) {
                append_line(lines, parts[p][l], l == 0, formats);
            }
            keep_inline(lines);
        }
    }
    if (!formats.empty()) {
        formats.write(song_node.insert_child_before("format", lyrics));
    }

    std::ostringstream out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

}  // namespace seventhslide::parse
