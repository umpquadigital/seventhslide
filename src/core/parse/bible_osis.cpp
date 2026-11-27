// OSIS Bible files; see bible_readers.hpp.
//
// Verses come two ways: a container (<verse osisID="Gen.1.1">text</verse>) or
// milestones around text that may cross paragraphs and poetry lines
// (<verse sID=".." osisID="Gen.1.1"/>text<verse eID=".."/>). The text is taken
// in document order, into whichever verse is open. Book ids are read as the
// English names the reference parser knows.

#include "bible_readers.hpp"

#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/strings.hpp>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seventhslide::parse {
namespace {

// The SWORD / CrossWire book ids. An id not here keeps the id as its name.
const std::unordered_map<std::string_view, std::string_view> k_books{
    {"Gen", "Genesis"},          {"Exod", "Exodus"},          {"Lev", "Leviticus"},
    {"Num", "Numbers"},          {"Deut", "Deuteronomy"},     {"Josh", "Joshua"},
    {"Judg", "Judges"},          {"Ruth", "Ruth"},            {"1Sam", "1 Samuel"},
    {"2Sam", "2 Samuel"},        {"1Kgs", "1 Kings"},         {"2Kgs", "2 Kings"},
    {"1Chr", "1 Chronicles"},    {"2Chr", "2 Chronicles"},    {"Ezra", "Ezra"},
    {"Neh", "Nehemiah"},         {"Esth", "Esther"},          {"Job", "Job"},
    {"Ps", "Psalms"},            {"Prov", "Proverbs"},        {"Eccl", "Ecclesiastes"},
    {"Song", "Song of Solomon"}, {"Isa", "Isaiah"},           {"Jer", "Jeremiah"},
    {"Lam", "Lamentations"},     {"Ezek", "Ezekiel"},         {"Dan", "Daniel"},
    {"Hos", "Hosea"},            {"Joel", "Joel"},            {"Amos", "Amos"},
    {"Obad", "Obadiah"},         {"Jonah", "Jonah"},          {"Mic", "Micah"},
    {"Nah", "Nahum"},            {"Hab", "Habakkuk"},         {"Zeph", "Zephaniah"},
    {"Hag", "Haggai"},           {"Zech", "Zechariah"},       {"Mal", "Malachi"},
    {"Matt", "Matthew"},         {"Mark", "Mark"},            {"Luke", "Luke"},
    {"John", "John"},            {"Acts", "Acts"},            {"Rom", "Romans"},
    {"1Cor", "1 Corinthians"},   {"2Cor", "2 Corinthians"},   {"Gal", "Galatians"},
    {"Eph", "Ephesians"},        {"Phil", "Philippians"},     {"Col", "Colossians"},
    {"1Thess", "1 Thessalonians"}, {"2Thess", "2 Thessalonians"},
    {"1Tim", "1 Timothy"},       {"2Tim", "2 Timothy"},       {"Titus", "Titus"},
    {"Phlm", "Philemon"},        {"Heb", "Hebrews"},          {"Jas", "James"},
    {"1Pet", "1 Peter"},         {"2Pet", "2 Peter"},         {"1John", "1 John"},
    {"2John", "2 John"},         {"3John", "3 John"},         {"Jude", "Jude"},
    {"Rev", "Revelation"},
    {"Tob", "Tobit"},            {"Jdt", "Judith"},           {"EsthGr", "Greek Esther"},
    {"AddEsth", "Additions to Esther"}, {"Wis", "Wisdom"},    {"Sir", "Sirach"},
    {"Bar", "Baruch"},           {"EpJer", "Letter of Jeremiah"},
    {"PrAzar", "Prayer of Azariah"}, {"Sus", "Susanna"},      {"Bel", "Bel and the Dragon"},
    {"1Macc", "1 Maccabees"},    {"2Macc", "2 Maccabees"},    {"3Macc", "3 Maccabees"},
    {"4Macc", "4 Maccabees"},    {"PrMan", "Prayer of Manasseh"},
    {"1Esd", "1 Esdras"},        {"2Esd", "2 Esdras"},        {"AddPs", "Psalm 151"},
};

struct VerseId {
    std::string book;
    int chapter = 0;
    int verse = 0;
};

// ``Gen.1.1``. Verses joined into one (``Rom.16.25 Rom.16.26``) are kept under
// the first; a work prefix (``KJV:Gen.1.1``) and a sub-verse mark
// (``Gen.1.1!a``) are passed over.
[[nodiscard]] std::optional<VerseId> parse_verse_id(std::string_view id)
{
    id = util::trim_view(id);
    id = id.substr(0, id.find_first_of(" \t\r\n"));
    if (const auto colon = id.find(':'); colon != std::string_view::npos) {
        id = id.substr(colon + 1);
    }
    const auto dot1 = id.find('.');
    if (dot1 == std::string_view::npos || dot1 == 0) {
        return std::nullopt;
    }
    const auto dot2 = id.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos) {
        return std::nullopt;
    }
    const auto chapter = util::parse_leading_int(id.substr(dot1 + 1, dot2 - dot1 - 1));
    const auto verse = util::parse_leading_int(id.substr(dot2 + 1));
    if (!chapter || !verse) {
        return std::nullopt;
    }
    const auto book_id = id.substr(0, dot1);
    const auto it = k_books.find(book_id);
    return VerseId{
        .book = std::string(it != k_books.end() ? it->second : book_id),
        .chapter = *chapter,
        .verse = *verse,
    };
}

// Where a dropped note or heading was, until the text is tidied. Taken out of
// the file's own text, which XML forbids it in anyway.
constexpr char k_gap = '\x01';

// What a gap and the space before it are dropped before.
constexpr std::array<std::string_view, 21> k_closing_punct{
    ",", ".", ";", ":", "!", "?", ")", "]",
    "\xE2\x80\x9D", "\xE2\x80\x99", "\xC2\xBB",              // ” ’ »
    "\xE3\x80\x81", "\xE3\x80\x82", "\xEF\xBC\x8C",         // 、 。 ，
    "\xEF\xBC\x9B", "\xEF\xBC\x9A", "\xEF\xBC\x81", "\xEF\xBC\x9F",  // ； ： ！ ？
    "\xD8\x8C", "\xD8\x9B", "\xD8\x9F",                      // ، ؛ ؟
};

[[nodiscard]] bool starts_closing_punct(std::string_view s)
{
    for (const auto p : k_closing_punct) {
        if (s.starts_with(p)) {
            return true;
        }
    }
    return false;
}

// Whitespace runs to one space, trimmed. A run with a gap in it goes before
// closing punctuation (``Sivan <note>..</note>, am``), and a gap with no space
// beside it leaves none (``word<note>..</note>s``).
[[nodiscard]] std::string tidy(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] != k_gap && !util::is_space(raw[i])) {
            out.push_back(raw[i]);
            ++i;
            continue;
        }
        bool space = false;
        bool gap = false;
        while (i < raw.size() && (raw[i] == k_gap || util::is_space(raw[i]))) {
            (raw[i] == k_gap ? gap : space) = true;
            ++i;
        }
        const bool punct_next = starts_closing_punct(raw.substr(i));
        if (space && !(gap && punct_next) && !out.empty() && i < raw.size()) {
            out.push_back(' ');
        }
    }
    return out;
}

[[nodiscard]] pugi::xml_node child_named(pugi::xml_node node, std::string_view tag)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_element && xml_local_name(c.name()) == tag) {
            return c;
        }
    }
    return {};
}

// All the text in ``node``, markup and all (``King James <hi>Version</hi>``).
[[nodiscard]] std::string all_text(pugi::xml_node node)
{
    struct Collect : pugi::xml_tree_walker {
        std::string text;
        bool for_each(pugi::xml_node& n) override
        {
            if (n.type() == pugi::node_pcdata || n.type() == pugi::node_cdata) {
                text += n.value();
            }
            return true;
        }
    } collect;
    node.traverse(collect);
    return util::collapse_ws(collect.text);
}

// The title and rights of the header's ``work`` that the text names
// (``osisIDWork``), else of its first.
void read_header(pugi::xml_node osis_text, ParsedBible& out)
{
    const std::string_view work_id = osis_text.attribute("osisIDWork").value();
    pugi::xml_node work;
    for (pugi::xml_node w = child_named(osis_text, "header").first_child(); w;
         w = w.next_sibling()) {
        if (w.type() != pugi::node_element || xml_local_name(w.name()) != "work") {
            continue;
        }
        if (!work) {
            work = w;
        }
        if (!work_id.empty() && work_id == w.attribute("osisWork").value()) {
            work = w;
            break;
        }
    }
    out.name = all_text(child_named(work, "title"));
    if (out.name.empty()) {
        out.name = work_id.empty() ? "Unknown Bible" : std::string(work_id);
    }
    out.copyright = all_text(child_named(work, "rights"));
}

// Elements whose text is not verse text, though verse markers in them count.
// A canonical title (a Psalm's heading, where it is numbered as a verse) is
// verse text; other titles are headings.
[[nodiscard]] bool drops_text(pugi::xml_node n, std::string_view tag)
{
    if (tag == "title") {
        return std::string_view(n.attribute("canonical").value()) != "true";
    }
    return tag == "note" || tag == "speaker";
}

// Elements whose edges part words where the file may have no space.
[[nodiscard]] bool breaks_words(std::string_view tag)
{
    return tag == "p" || tag == "l" || tag == "lg" || tag == "lb" || tag == "div"
           || tag == "chapter" || tag == "list" || tag == "item" || tag == "title";
}

// A verse never runs on past the end of its chapter or book, nor into the next.
[[nodiscard]] bool ends_verses(pugi::xml_node n, std::string_view tag)
{
    return tag == "chapter"
           || (tag == "div" && std::string_view(n.attribute("type").value()) == "book");
}

[[nodiscard]] bool is_milestone(pugi::xml_node n)
{
    return n.attribute("sID") || n.attribute("eID");
}

class VerseReader {
public:
    [[nodiscard]] std::vector<model::BibleVerse> read(pugi::xml_node osis_text)
    {
        // Walks without recursion. The header is passed over.
        pugi::xml_node node = osis_text.first_child();
        while (node) {
            if (enter(node) && node.first_child()) {
                node = node.first_child();
                continue;
            }
            while (node != osis_text) {
                leave(node);
                if (node.next_sibling()) {
                    node = node.next_sibling();
                    break;
                }
                node = node.parent();
            }
            if (node == osis_text) {
                break;
            }
        }
        for (auto& row : verses_) {
            row.text = tidy(row.text);
        }
        std::erase_if(verses_, [](const model::BibleVerse& row) { return row.text.empty(); });
        return std::move(verses_);
    }

private:
    // True to walk into ``n``'s children.
    bool enter(pugi::xml_node n)
    {
        if (n.type() == pugi::node_pcdata || n.type() == pugi::node_cdata) {
            if (dropping_.empty()) {
                append_file_text(n.value());
            }
            return false;
        }
        if (n.type() != pugi::node_element) {
            return false;
        }
        const auto tag = xml_local_name(n.name());
        if (tag == "header") {
            return false;
        }
        if (tag == "verse") {
            if (n.attribute("eID")) {
                open_.reset();
            } else {
                open_verse(n);
            }
            return true;
        }
        if (ends_verses(n, tag)) {
            open_.reset();
        }
        if (drops_text(n, tag)) {
            if (dropping_.empty()) {
                append(std::string_view(&k_gap, 1));
            }
            dropping_.push_back(n);
            return true;
        }
        if (tag == "q") {
            append_marker(n);
        } else if (breaks_words(tag)) {
            append(" ");
        }
        return true;
    }

    void leave(pugi::xml_node n)
    {
        if (n.type() != pugi::node_element) {
            return;
        }
        const auto tag = xml_local_name(n.name());
        if (tag == "verse") {
            // A container ends its verse; a start milestone leaves it open.
            if (!is_milestone(n)) {
                open_.reset();
            }
            return;
        }
        if (!dropping_.empty() && dropping_.back() == n) {
            dropping_.pop_back();
            return;
        }
        if (ends_verses(n, tag)) {
            open_.reset();
        } else if (tag == "q" && !is_milestone(n)) {
            append_marker(n);
        } else if (breaks_words(tag)) {
            append(" ");
        }
    }

    void open_verse(pugi::xml_node v)
    {
        open_.reset();
        const auto id = parse_verse_id(v.attribute("osisID").value());
        if (!id) {
            return;
        }
        const auto [it, added] =
            seen_.try_emplace({id->book, id->chapter, id->verse}, verses_.size());
        if (added) {
            model::BibleVerse row;
            row.book = id->book;
            row.chapter = id->chapter;
            row.verse_num = id->verse;
            verses_.push_back(std::move(row));
        } else {
            // A verse numbered twice keeps both texts, as the other readers do.
            verses_[it->second].text += ' ';
        }
        open_ = it->second;
    }

    void append(std::string_view text)
    {
        if (open_) {
            verses_[*open_].text.append(text);
        }
    }

    void append_file_text(std::string_view text)
    {
        if (!open_) {
            return;
        }
        auto& out = verses_[*open_].text;
        for (const char c : text) {
            if (c != k_gap) {
                out.push_back(c);
            }
        }
    }

    // A quotation's marks, where the file gives them only as ``marker``.
    void append_marker(pugi::xml_node q)
    {
        if (dropping_.empty()) {
            append_file_text(q.attribute("marker").value());
        }
    }

    std::vector<model::BibleVerse> verses_;
    std::map<std::tuple<std::string, int, int>, std::size_t> seen_;
    std::optional<std::size_t> open_;
    // The elements being walked whose text is dropped, innermost last.
    std::vector<pugi::xml_node> dropping_;
};

[[nodiscard]] pugi::xml_node find_osis_text(pugi::xml_node root)
{
    if (const auto text = child_named(root, "osisText")) {
        return text;
    }
    return child_named(child_named(root, "osisCorpus"), "osisText");
}

}  // namespace

ParsedBible read_osis(pugi::xml_node root)
{
    ParsedBible out;
    const auto text = find_osis_text(root);
    read_header(text, out);
    if (text) {
        out.verses = VerseReader{}.read(text);
    }
    return out;
}

}  // namespace seventhslide::parse
