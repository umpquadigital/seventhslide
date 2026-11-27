#include <seventhslide/parse/song_xml.hpp>

#include "song_readers.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/text_encoding.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace seventhslide::parse {
namespace {

[[nodiscard]] bool local_name_is(pugi::xml_node node, std::string_view want)
{
    return xml_local_name(node.name()) == want;
}

/// Elements ``normalize_imported_markup`` turns into formatting or a newline.
[[nodiscard]] bool is_markup_element(std::string_view name)
{
    for (const std::string_view known :
         {"b", "strong", "i", "em", "u", "sup", "super", "br"}) {
        if (util::iequals(name, known)) {
            return true;
        }
    }
    return false;
}

/// What a file's ``<format>`` says each ``<tag name="...">`` of its lyrics
/// opens and closes, by name.
struct TagMarkup {
    std::string open;
    std::string close;
};
using TagFormats = std::map<std::string, TagMarkup, std::less<>>;

/// How ``append_inner_xml`` reads a lyric element.
struct InlineRead {
    const TagFormats& formats;
    /// An OpenLyrics / OpenSong ``<chord>`` (any prefix, any case) as ``[X]``.
    bool chords_as_brackets = false;
    /// White space holding a line end is one space: in ``<lines>`` only
    /// ``<br/>`` ends a line, and the rest is how the file was indented.
    bool fold_line_ends = false;
    /// Where ``<comment>`` text goes (stage notes, never words on a slide);
    /// null drops it.
    std::vector<std::string>* notes = nullptr;
};

/// What OpenLyrics 0.9 ``structure`` shortcodes are written as in a chord
/// name. A chord formula ("3-5-m7-13") is kept as written.
[[nodiscard]] std::string_view chord_structure_suffix(std::string_view structure)
{
    static constexpr std::pair<std::string_view, std::string_view> k_suffixes[] = {
        {"major", ""},        {"power", "5"},      {"min", "m"},         {"aug", "+"},
        {"dim", "dim"},       {"dom7", "7"},       {"maj7", "maj7"},     {"min7", "m7"},
        {"dim7", "dim7"},     {"halfdim7", "m7b5"}, {"minmaj7", "m(maj7)"},
        {"augmaj7", "+maj7"}, {"aug7", "+7"},      {"maj6", "6"},        {"maj6b", "6b"},
        {"min6", "m6"},       {"min6b", "m6b"},    {"dom9", "9"},        {"dom9b", "7b9"},
        {"maj9", "maj9"},     {"min9", "m9"},      {"minmaj9", "m(maj9)"},
        {"aug9", "+9"},       {"halfdim9", "m9b5"}, {"sus4", "sus4"},    {"sus2", "sus2"},
        {"add9", "add9"},
    };
    for (const auto& [code, suffix] : k_suffixes) {
        if (structure == code) {
            return suffix;
        }
    }
    return structure;
}

/// A ``<chord>``'s name: 0.8's ``name``, or 0.9's root, structure and bass
/// ("C#m7/G#"). OpenLyrics writes roots in English letters whatever
/// ``chordNotation`` says, which is only how a program should show them.
[[nodiscard]] std::string chord_name(pugi::xml_node chord)
{
    if (auto name = util::trim(chord.attribute("name").value()); !name.empty()) {
        return name;
    }
    auto name = util::trim(chord.attribute("root").value());
    if (name.empty()) {
        return {};
    }
    name += chord_structure_suffix(util::trim(chord.attribute("structure").value()));
    if (const auto bass = util::trim(chord.attribute("bass").value()); !bass.empty()) {
        name += "/" + bass;
    }
    return name;
}

void append_text(std::string& out, std::string_view text, bool fold_line_ends)
{
    if (!fold_line_ends) {
        out += text;
        return;
    }
    for (std::size_t i = 0; i < text.size();) {
        if (!util::is_space(text[i])) {
            out.push_back(text[i++]);
            continue;
        }
        std::size_t end = i;
        bool line_end = false;
        while (end < text.size() && util::is_space(text[end])) {
            line_end = line_end || text[end] == '\n' || text[end] == '\r';
            ++end;
        }
        if (line_end) {
            out.push_back(' ');
        } else {
            out.append(text.substr(i, end - i));
        }
        i = end;
    }
}

[[nodiscard]] std::string text_of(pugi::xml_node node)
{
    std::string out;
    const auto walk = [&](auto&& self, pugi::xml_node n) -> void {
        if (n.type() == pugi::node_pcdata || n.type() == pugi::node_cdata) {
            out += n.value();
        }
        for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
            self(self, c);
        }
    };
    walk(walk, node);
    return util::collapse_ws(out);
}

/// Emit descendants with **local** element names (no xmlns / prefixes).
/// Default-xmlns OpenLyrics files would otherwise serialize ``<b>`` as ``ns0:b``, which the
/// allowlist then escapes. Local names keep ``<b>/<i>/<u>`` importable.
///
/// A chord (see ``InlineRead``) becomes ``[X]`` followed by whatever it
/// wraps. An OpenLyrics ``<tag>`` that ``formats`` defines wraps its text in
/// the markup it opens and closes.
void append_inner_xml(pugi::xml_node node, std::string& out, const InlineRead& read)
{
    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        const auto type = child.type();
        if (type == pugi::node_pcdata || type == pugi::node_cdata) {
            append_text(out, child.value(), read.fold_line_ends);
            continue;
        }
        if (type != pugi::node_element) {
            continue;
        }
        const auto name = xml_local_name(child.name());
        if (util::iequals(name, "comment")) {
            if (read.notes != nullptr) {
                if (auto note = text_of(child); !note.empty()) {
                    read.notes->push_back(std::move(note));
                }
            }
            continue;
        }
        if (read.chords_as_brackets && util::iequals(name, "chord")) {
            if (const auto chord = chord_name(child); !chord.empty()) {
                out.push_back('[');
                out += chord;
                out.push_back(']');
            }
            append_inner_xml(child, out, read);
            continue;
        }
        if (name == "tag") {
            const auto format = read.formats.find(std::string_view(child.attribute("name").value()));
            if (format != read.formats.end()) {
                out += format->second.open;
                append_inner_xml(child, out, read);
                out += format->second.close;
                continue;
            }
        }
        if (!is_markup_element(name)) {
            // Structure the lyrics cannot show (an image, a vendor tag):
            // keep whatever text it wraps, drop the element.
            append_inner_xml(child, out, read);
            continue;
        }
        out.push_back('<');
        out.append(name);
        for (pugi::xml_attribute attr : child.attributes()) {
            out.push_back(' ');
            out += attr.name();
            out += "=\"";
            out += util::xml_escape(attr.value(), true);
            out.push_back('"');
        }
        if (!child.first_child()) {
            out += " />";
            continue;
        }
        out.push_back('>');
        append_inner_xml(child, out, read);
        out += "</";
        out.append(name);
        out.push_back('>');
    }
}

[[nodiscard]] std::string inner_xml_of(pugi::xml_node elem, const InlineRead& read)
{
    std::string out;
    append_inner_xml(elem, out, read);
    return out;
}

/// Lyric blocks separated by one blank line.
[[nodiscard]] std::string join_paragraphs(const std::vector<std::string>& parts)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += "\n\n";
        }
        out += parts[i];
    }
    return out;
}

[[nodiscard]] std::string clean_inner_xml(std::string_view inner)
{
    // Normalize the imported markup, then trim surrounding whitespace.
    return util::trim(model::normalize_imported_markup(inner));
}

/// ``clean_inner_xml``, each line trimmed and blank lines dropped: a verse
/// has none, and taking a ``<comment>`` out can leave one.
[[nodiscard]] std::string clean_lines(std::string_view inner)
{
    const auto text = model::normalize_imported_markup(inner);
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const auto end = nl == std::string::npos ? text.size() : nl;
        if (const auto line = util::trim_view(std::string_view(text).substr(start, end - start));
            !line.empty()) {
            if (!out.empty()) {
                out.push_back('\n');
            }
            out += line;
        }
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

void collect_by_local_name(pugi::xml_node root,
                           std::string_view want,
                           std::vector<pugi::xml_node>& out)
{
    for (pugi::xml_node n = root.first_child(); n; n = n.next_sibling()) {
        if (n.type() != pugi::node_element) {
            continue;
        }
        if (local_name_is(n, want)) {
            out.push_back(n);
        }
        collect_by_local_name(n, want, out);
    }
}

[[nodiscard]] std::vector<pugi::xml_node> find_all_local(pugi::xml_node root,
                                                         std::string_view want)
{
    std::vector<pugi::xml_node> out;
    collect_by_local_name(root, want, out);
    return out;
}

[[nodiscard]] pugi::xml_node find_first_local(pugi::xml_node root,
                                              std::string_view want)
{
    if (local_name_is(root, want)) {
        return root;
    }
    for (pugi::xml_node n = root.first_child(); n; n = n.next_sibling()) {
        if (n.type() != pugi::node_element) {
            continue;
        }
        if (auto hit = find_first_local(n, want)) {
            return hit;
        }
    }
    return {};
}

[[nodiscard]] std::string find_text_local(pugi::xml_node root,
                                          std::string_view want)
{
    if (auto n = find_first_local(root, want)) {
        return n.child_value();
    }
    return {};
}

/// Formatting markup that is only formatting: what it opens shows no text
/// of its own, once read as the app's markup. HTML this app cannot read as
/// formatting (a ``<span style>``) would otherwise show as words.
[[nodiscard]] bool is_formatting_only(std::string_view markup)
{
    return model::plain_text(model::parse_inline_markup(model::normalize_imported_markup(markup)))
        .empty();
}

/// The formatting tags ``song`` defines in its ``<format>`` (OpenLyrics
/// 0.8+), whatever program wrote them. A tag whose markup is not formatting
/// alone is left out, so its text is kept plain.
[[nodiscard]] TagFormats tag_formats(pugi::xml_node song)
{
    TagFormats formats;
    for (auto format : find_all_local(song, "format")) {
        for (auto tag : find_all_local(format, "tag")) {
            const std::string name = tag.attribute("name").value();
            if (name.empty() || formats.contains(name)) {
                continue;
            }
            TagMarkup markup{find_text_local(tag, "open"), find_text_local(tag, "close")};
            if (is_formatting_only(markup.open) && is_formatting_only(markup.close)) {
                formats.emplace(name, std::move(markup));
            }
        }
    }
    return formats;
}

/// Times a ``<lines repeat="N">`` is shown, within reason.
[[nodiscard]] int lines_repeat(pugi::xml_node lines)
{
    const auto repeat = util::parse_leading_int(util::trim(lines.attribute("repeat").value()));
    return repeat ? std::clamp(*repeat, 1, 10) : 1;
}

[[nodiscard]] std::string build_block_text(pugi::xml_node v_node,
                                           const TagFormats& formats,
                                           std::vector<std::string>& notes)
{
    const auto lines_nodes = find_all_local(v_node, "lines");
    std::vector<std::string> lines;
    if (!lines_nodes.empty()) {
        const InlineRead read{formats, true, true, &notes};
        for (auto ln : lines_nodes) {
            auto text = clean_lines(inner_xml_of(ln, read));
            if (text.empty()) {
                continue;
            }
            // A part (men, women) is sung by some; the words say who.
            if (const auto part = util::trim(ln.attribute("part").value()); !part.empty()) {
                text = "(" + model::escape_markup_text(part) + ")\n" + text;
            }
            for (int i = lines_repeat(ln); i > 0; --i) {
                lines.push_back(text);
            }
        }
    } else {
        // Flattened text fallback.
        std::string flat;
        const auto walk = [&](auto&& self, pugi::xml_node n) -> void {
            if (n.type() == pugi::node_pcdata || n.type() == pugi::node_cdata) {
                flat += n.value();
            }
            for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
                self(self, c);
            }
        };
        walk(walk, v_node);
        flat = util::trim(flat);
        if (!flat.empty()) {
            lines.push_back(util::trim(model::normalize_imported_markup(flat)));
        }
    }
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) {
            joined.push_back('\n');
        }
        joined += lines[i];
    }
    return joined;
}

[[nodiscard]] std::string strip_verse_part_suffix(std::string rest)
{
    if (rest.size() >= 2) {
        const char last = rest.back();
        const char prev = rest[rest.size() - 2];
        if (util::is_alpha(last)
            && util::is_digit(prev)) {
            rest.pop_back();
        }
    }
    return rest;
}

[[nodiscard]] std::string verse_label(std::string_view vname)
{
    if (vname.empty()) {
        return "Misc";
    }
    if (const auto* type = verse_type_for_code(util::to_lower(vname.front()))) {
        if (vname.size() == 1) {
            return std::string(type->name) + ":1";
        }
        if (util::is_digit(vname[1])) {
            return std::string(type->name) + ":"
                + strip_verse_part_suffix(std::string(vname.substr(1)));
        }
        // Spelled out: "chorus", "verse2".
        if (const auto heading = heading_label(vname)) {
            return heading->type + ":" + std::to_string(std::max(heading->number, 1));
        }
    }
    return std::string(vname);
}

void append_labeled_block(std::vector<std::string>& parts,
                          const std::string& label,
                          const std::string& block)
{
    const std::string header = "---[" + label + "]---\n";
    if (!parts.empty() && parts.back().compare(0, header.size(), header) == 0) {
        parts.back().push_back('\n');
        parts.back().append(model::k_forced_split);
        parts.back().push_back('\n');
        parts.back().append(block);
        return;
    }
    parts.push_back(header + block);
}

/// A language attribute: ``lang``, or ``xml:lang`` as some files have it.
[[nodiscard]] std::string lang_of(pugi::xml_node node)
{
    auto lang = util::trim(node.attribute("lang").value());
    return lang.empty() ? util::trim(node.attribute("xml:lang").value()) : lang;
}

/// Which language a verse is in: ``de``, or ``he/en`` for Hebrew written out
/// for English readers. A verse naming none is in the song's language.
[[nodiscard]] std::string verse_language(pugi::xml_node verse, std::string_view song_lang)
{
    auto lang = lang_of(verse);
    if (lang.empty()) {
        lang = song_lang;
    }
    const auto translit = util::trim(verse.attribute("translit").value());
    if (translit.empty()) {
        return lang;
    }
    return (lang.empty() ? std::string("?") : lang) + "/" + translit;
}

/// The verses of the song's own language: the song's ``xml:lang`` when a
/// verse is in it, otherwise the first verse's. Translations and
/// transliterations are named in ``left_out``.
[[nodiscard]] std::vector<pugi::xml_node> verses_in_one_language(
    const std::vector<pugi::xml_node>& verses,
    std::string_view song_lang,
    std::vector<std::string>& left_out)
{
    if (verses.empty()) {
        return {};
    }
    std::string keep = verse_language(verses.front(), song_lang);
    for (const auto verse : verses) {
        if (verse_language(verse, song_lang) == song_lang) {
            keep = song_lang;
            break;
        }
    }
    std::vector<pugi::xml_node> out;
    for (const auto verse : verses) {
        auto language = verse_language(verse, song_lang);
        if (language == keep) {
            out.push_back(verse);
            continue;
        }
        if (language.empty()) {
            language = "unknown";
        }
        if (std::find(left_out.begin(), left_out.end(), language) == left_out.end()) {
            left_out.push_back(std::move(language));
        }
    }
    return out;
}

/// The song's title, and its other titles for ``also_known_as``: the
/// original first, then one in the song's language, then the first.
[[nodiscard]] std::string pick_title(pugi::xml_node s,
                                     std::string_view song_lang,
                                     std::vector<std::string>& also_known_as)
{
    std::vector<pugi::xml_node> titles;
    for (auto list : find_all_local(s, "titles")) {
        for (auto title : find_all_local(list, "title")) {
            if (!util::trim(title.child_value()).empty()) {
                titles.push_back(title);
            }
        }
    }
    if (titles.empty()) {
        auto title = util::trim(find_text_local(s, "title"));
        if (title.empty()) {
            title = util::trim(find_text_local(s, "name"));
        }
        return title;
    }
    auto chosen = std::find_if(titles.begin(), titles.end(), [](pugi::xml_node t) {
        return util::iequals(util::trim(t.attribute("original").value()), "true");
    });
    if (chosen == titles.end() && !song_lang.empty()) {
        chosen = std::find_if(titles.begin(), titles.end(),
                              [&](pugi::xml_node t) { return lang_of(t) == song_lang; });
    }
    if (chosen == titles.end()) {
        chosen = titles.begin();
    }
    auto title = util::trim(chosen->child_value());
    for (const auto other : titles) {
        auto text = util::trim(other.child_value());
        if (text != title
            && std::find(also_known_as.begin(), also_known_as.end(), text) == also_known_as.end()) {
            also_known_as.push_back(std::move(text));
        }
    }
    return title;
}

/// ``order`` without the names of instrumental parts, which have no slide.
[[nodiscard]] std::string order_without(std::string_view order,
                                        const std::vector<std::string>& instruments,
                                        bool& removed)
{
    std::string out;
    for (const auto token : util::split_ws(order)) {
        const auto code = util::to_lower(token);
        const bool instrument =
            std::any_of(instruments.begin(), instruments.end(), [&](const std::string& name) {
                return code == name || base_verse_code(code) == name;
            });
        if (instrument) {
            removed = true;
            continue;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += token;
    }
    return out;
}

/// What a song's properties hold that the library has no field for, as
/// lines of its comments ("Tempo: 90 bpm").
void note_extra_properties(pugi::xml_node s, std::vector<std::string>& comments)
{
    for (auto tempo : find_all_local(s, "tempo")) {
        auto value = util::trim(tempo.child_value());
        if (value.empty()) {
            continue;
        }
        if (util::iequals(util::trim(tempo.attribute("type").value()), "bpm")) {
            value += " bpm";
        }
        comments.push_back("Tempo: " + value);
        break;
    }
    for (const auto& [element, label] : {std::pair{"variant", "Variant"},
                                         std::pair{"publisher", "Publisher"},
                                         std::pair{"released", "Released"}}) {
        if (auto value = util::trim(find_text_local(s, element)); !value.empty()) {
            comments.push_back(std::string(label) + ": " + value);
        }
    }
}

/// Newer than the OpenLyrics this reader was written for.
[[nodiscard]] bool newer_than_known(std::string_view version)
{
    const auto dot = version.find('.');
    const auto major = util::parse_leading_int(version.substr(0, dot));
    if (!major) {
        return false;
    }
    if (*major != 0) {
        return *major > 0;
    }
    const auto minor = dot == std::string_view::npos ? std::optional<int>(0)
                                                     : util::parse_leading_int(version.substr(dot + 1));
    return minor && *minor > 9;
}

[[nodiscard]] ImportedSong parse_openlyrics_song(pugi::xml_node s)
{
    ImportedSong imported;
    const std::string_view created_in = s.attribute("createdIn").value();
    imported.format = util::starts_with_ci(created_in, "SeventhSlide") ? SongFormat::SeventhSlide
                                                                       : SongFormat::OpenLyrics;
    auto& warnings = imported.warnings;
    model::Song& song = imported.song;
    const auto version = util::trim(s.attribute("version").value());
    if (newer_than_known(version)) {
        warnings.push_back("Newer OpenLyrics: " + version + ", read as 0.9");
    }
    const auto song_lang = lang_of(s);

    std::vector<std::string> comments;
    std::vector<std::string> also_known_as;
    song.title = pick_title(s, song_lang, also_known_as);
    if (song.title.empty()) {
        song.title = "Untitled";
    }

    const auto formats = tag_formats(s);
    std::vector<std::string> lyrics_parts;
    std::vector<std::string> left_out;
    const auto verse_nodes = verses_in_one_language(find_all_local(s, "verse"), song_lang, left_out);
    std::vector<std::string> lyric_notes;
    if (!verse_nodes.empty()) {
        for (auto v : verse_nodes) {
            const char* raw_name = v.attribute("name").value();
            const std::string vname = raw_name && *raw_name ? raw_name : "Misc";
            const auto label = verse_label(vname);
            std::vector<std::string> notes;
            auto block = build_block_text(v, formats, notes);
            if (!block.empty()) {
                append_labeled_block(lyrics_parts, label, block);
            }
            for (auto& note : notes) {
                lyric_notes.push_back(verse_display_name(label) + ": " + note);
            }
        }
    } else {
        const InlineRead read{formats};
        for (auto ln : find_all_local(s, "lines")) {
            lyrics_parts.push_back(clean_inner_xml(inner_xml_of(ln, read)));
        }
    }
    song.lyrics = join_paragraphs(lyrics_parts);
    if (!left_out.empty()) {
        std::string list;
        for (const auto& language : left_out) {
            list += (list.empty() ? "" : ", ") + util::to_upper(language);
        }
        warnings.push_back("Dropped languages: " + list);
    }

    // Instrumental parts (0.9) have no words, so no slide: an order naming
    // them keeps the rest.
    std::vector<std::string> instruments;
    for (auto instrument : find_all_local(s, "instrument")) {
        if (auto name = util::to_lower(util::trim(instrument.attribute("name").value()));
            !name.empty()) {
            instruments.push_back(std::move(name));
        }
    }
    bool removed_instruments = false;
    song.verse_order = order_without(util::collapse_ws(find_text_local(s, "verseOrder")),
                                     instruments, removed_instruments);
    if (removed_instruments) {
        warnings.push_back("Dropped instrumentals from order");
    }
    // An order naming verses the lyrics do not have would play nothing for
    // them; the song plays in lyric order instead.
    if (validate_verse_order(song.verse_order, song.lyrics)) {
        song.verse_order.clear();
        warnings.push_back("Dropped invalid verse order");
    }

    // Authors: .//authors/author (direct children of an authors node), with the
    // OpenLyrics ``type`` ("words" / "music" / "translation") and, for a
    // translation, its ``lang``. The format credits one role per element, so a
    // writer of both words and music appears twice; fold repeats of a name into
    // a single author carrying both roles instead of listing it twice.
    for (auto authors : find_all_local(s, "authors")) {
        for (pugi::xml_node a = authors.first_child(); a; a = a.next_sibling()) {
            if (!local_name_is(a, "author")) {
                continue;
            }
            auto name = util::trim(a.child_value());
            if (name.empty()) {
                continue;
            }
            auto roles = model::author_roles_from_type_string(util::trim(a.attribute("type").value()));
            const auto lang = lang_of(a);
            for (auto& role : roles) {
                if (role.type == model::k_author_role_translation) {
                    role.lang = lang;
                }
            }
            const auto match = std::find_if(
                song.authors.begin(), song.authors.end(),
                [&name](const model::Author& existing) { return existing.name == name; });
            model::Author& target = match != song.authors.end()
                                        ? *match
                                        : song.authors.emplace_back(std::move(name));
            for (auto& role : roles) {
                target.add_role(std::move(role));
            }
        }
    }

    // First songbook only.
    for (auto songbooks : find_all_local(s, "songbooks")) {
        for (pugi::xml_node sb = songbooks.first_child(); sb;
             sb = sb.next_sibling()) {
            if (!local_name_is(sb, "songbook")) {
                continue;
            }
            song.songbook_name = util::trim(sb.attribute("name").value());
            song.songbook_entry = util::trim(sb.attribute("entry").value());
            break;
        }
        if (!song.songbook_name.empty() || !song.songbook_entry.empty()) {
            break;
        }
    }

    song.key = util::trim(find_text_local(s, "key"));
    song.copyright = util::trim(find_text_local(s, "copyright"));
    song.ccli_song_number = util::trim(find_text_local(s, "ccliNo"));

    std::vector<std::string> topics;
    for (auto themes : find_all_local(s, "themes")) {
        for (auto theme : find_all_local(themes, "theme")) {
            topics.push_back(theme.child_value());
        }
    }
    song.topics = model::normalize_song_topics(topics);

    for (auto list : find_all_local(s, "comments")) {
        for (auto comment : find_all_local(list, "comment")) {
            if (auto text = util::trim(comment.child_value()); !text.empty()) {
                comments.push_back(std::move(text));
            }
        }
    }
    if (!also_known_as.empty()) {
        std::string list;
        for (const auto& title : also_known_as) {
            list += (list.empty() ? "" : "; ") + title;
        }
        comments.push_back("Also known as: " + list);
    }
    note_extra_properties(s, comments);
    comments.insert(comments.end(), lyric_notes.begin(), lyric_notes.end());
    for (std::size_t i = 0; i < comments.size(); ++i) {
        song.comments += (i > 0 ? "\n" : "") + comments[i];
    }
    return imported;
}

[[nodiscard]] std::vector<ImportedSong> parse_document(pugi::xml_document& doc,
                                                       std::string_view fallback_title)
{
    const auto root = doc.document_element();
    if (!root) {
        throw SongXmlError("Failed to parse XML: empty document");
    }

    std::vector<pugi::xml_node> song_nodes;
    if (local_name_is(root, "song")) {
        song_nodes.push_back(root);
    } else {
        song_nodes = find_all_local(root, "song");
    }

    if (!song_nodes.empty()) {
        std::vector<ImportedSong> out;
        out.reserve(song_nodes.size());
        for (auto s : song_nodes) {
            out.push_back(parse_openlyrics_song(s));
        }
        return out;
    }

    // Single-song fallback (non-song root).
    ImportedSong imported;
    model::Song& song = imported.song;
    song.title = util::trim(find_text_local(root, "title"));
    if (song.title.empty()) {
        song.title = util::trim(fallback_title);
    }
    std::vector<std::string> parts;
    const TagFormats no_formats;
    const InlineRead read{no_formats};
    for (auto ln : find_all_local(root, "lines")) {
        parts.push_back(clean_inner_xml(inner_xml_of(ln, read)));
    }
    song.lyrics = join_paragraphs(parts);
    return {std::move(imported)};
}

[[nodiscard]] std::vector<ImportedSong> parse_bytes(std::string_view data,
                                                    std::string_view fallback_title)
{
    reject_doctype<SongXmlError>(data);

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(
        data.data(),
        data.size(),
        // Keep whitespace-only text nodes between elements (spaces around
        // <chord>/<b>/...). Default flags strip them.
        pugi::parse_default | pugi::parse_ws_pcdata,
        // Already UTF-8 (read_song_bytes decodes files first), whatever the
        // declaration says.
        pugi::encoding_utf8);
    if (!result) {
        throw SongXmlError(std::string("Failed to parse XML: ") + result.description());
    }
    reject_deep_nesting<SongXmlError>(doc);
    return parse_document(doc, fallback_title);
}

}  // namespace

std::vector<ImportedSong> read_openlyrics(std::string_view xml, std::string_view fallback_title)
{
    return with_xml_errors<SongXmlError>([&] { return parse_bytes(xml, fallback_title); });
}

std::vector<model::Song> parse_song_xml(std::string_view xml,
                                        std::string_view fallback_title)
{
    std::vector<model::Song> out;
    for (auto& imported : read_openlyrics(xml, fallback_title)) {
        out.push_back(std::move(imported.song));
    }
    return out;
}

std::vector<model::Song> parse_song_file(const std::filesystem::path& path)
{
    const auto stem = path.stem().string();
    return with_xml_errors<SongXmlError>(
        [&] {
            return parse_song_xml(util::decode_text(read_file_bytes(path)),
                                  stem.empty() ? "Untitled" : stem);
        });
}

}  // namespace seventhslide::parse
