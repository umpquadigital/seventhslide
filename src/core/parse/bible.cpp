#include <seventhslide/parse/bible.hpp>

#include "bible_readers.hpp"

#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/strings.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <map>
#include <regex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace seventhslide::parse {
namespace {

[[nodiscard]] int attr_int(pugi::xml_node node, const char* attr)
{
    return util::parse_leading_int(node.attribute(attr).value()).value_or(0);
}

[[nodiscard]] std::vector<model::BibleVerse> parse_bible_books(
    pugi::xml_node root,
    std::string_view book_tag,
    std::string_view chap_tag,
    std::string_view verse_tag,
    const char* book_attr,
    const char* chap_attr,
    const char* verse_attr)
{
    std::vector<model::BibleVerse> verses;
    // The library holds one row per verse. A source that numbers a verse twice
    // (one real Tyndale file has two 1 Corinthians 10:29) keeps both texts, in
    // reading order, under that number rather than dropping one.
    std::map<std::tuple<std::string, int, int>, std::size_t> seen;
    for (pugi::xml_node b_node = root.first_child(); b_node;
         b_node = b_node.next_sibling()) {
        if (b_node.type() != pugi::node_element
            || xml_local_name(b_node.name()) != book_tag) {
            continue;
        }
        const char* raw_book = b_node.attribute(book_attr).value();
        const std::string b_name =
            (raw_book && *raw_book) ? raw_book : "Unknown Book";
        for (pugi::xml_node c_node = b_node.first_child(); c_node;
             c_node = c_node.next_sibling()) {
            if (c_node.type() != pugi::node_element
                || xml_local_name(c_node.name()) != chap_tag) {
                continue;
            }
            const int c_num = attr_int(c_node, chap_attr);
            for (pugi::xml_node v_node = c_node.first_child(); v_node;
                 v_node = v_node.next_sibling()) {
                if (v_node.type() != pugi::node_element
                    || xml_local_name(v_node.name()) != verse_tag) {
                    continue;
                }
                const int v_num = attr_int(v_node, verse_attr);
                // The verse element's own text only, not text of child elements.
                const char* raw = v_node.child_value();
                auto text = util::trim(raw ? raw : "");
                if (text.empty()) {
                    continue;
                }
                const auto [it, added] =
                    seen.try_emplace({b_name, c_num, v_num}, verses.size());
                if (!added) {
                    verses[it->second].text += ' ';
                    verses[it->second].text += text;
                    continue;
                }
                model::BibleVerse row;
                row.book = b_name;
                row.chapter = c_num;
                row.verse_num = v_num;
                row.text = std::move(text);
                verses.push_back(std::move(row));
            }
        }
    }
    return verses;
}

[[nodiscard]] bool is_osis_root(pugi::xml_node root)
{
    return xml_local_name(root.name()) == "osis";
}

[[nodiscard]] ParsedBible parse_document(pugi::xml_document& doc)
{
    const auto root = doc.document_element();
    if (!root) {
        throw BibleXmlError("Failed to parse XML: empty document");
    }

    if (is_osis_root(root)) {
        return read_osis(root);
    }

    ParsedBible out;
    const char* biblename = root.attribute("biblename").value();
    const char* name_attr = root.attribute("name").value();
    if (biblename && *biblename) {
        out.name = biblename;
    } else if (name_attr && *name_attr) {
        out.name = name_attr;
    } else {
        out.name = "Unknown Bible";
    }
    const char* copy = root.attribute("copyright").value();
    out.copyright = copy ? copy : "";

    const auto tag = util::to_upper(xml_local_name(root.name()));
    const auto tag_lower = util::to_lower(xml_local_name(root.name()));
    if (tag == "XMLBIBLE") {
        out.verses = parse_bible_books(root, "BIBLEBOOK", "CHAPTER", "VERS",
                                       "bname", "cnumber", "vnumber");
    } else if (tag_lower == "bible") {
        out.verses =
            parse_bible_books(root, "b", "c", "v", "n", "n", "n");
    } else {
        out.verses.clear();
    }
    return out;
}

[[nodiscard]] ParsedBible parse_bytes(std::string_view data)
{
    reject_doctype<BibleXmlError>(data);
    pugi::xml_document doc;
    const auto load = [&](unsigned int options) {
        const auto result = doc.load_buffer(data.data(), data.size(), options);
        if (!result) {
            throw BibleXmlError(std::string("Failed to parse XML: ")
                                + result.description());
        }
    };
    load(pugi::parse_default);
    // OSIS marks words up (``<w>In</w> <w>the</w>``), so the spaces between
    // elements are text too. The others keep the default: they read a verse's
    // first text node, which would then be the whitespace before an element.
    if (is_osis_root(doc.document_element())) {
        load(pugi::parse_default | pugi::parse_ws_pcdata);
    }
    reject_deep_nesting<BibleXmlError>(doc);
    return parse_document(doc);
}

const std::unordered_map<std::string_view, std::string_view> k_book_aliases{
    {"gn", "genesis"},       {"gen", "genesis"},
    {"ex", "exodus"},        {"exod", "exodus"},   {"exo", "exodus"},
    {"lv", "leviticus"},     {"nm", "numbers"},    {"nb", "numbers"},
    {"dt", "deuteronomy"},
    {"jdg", "judges"},       {"jgs", "judges"},    {"jsh", "joshua"},
    {"ps", "psalms"},        {"psa", "psalms"},    {"pss", "psalms"},
    {"pslm", "psalms"},      {"psalm", "psalms"},
    {"pr", "proverbs"},      {"prv", "proverbs"},
    {"sg", "songofsolomon"}, {"song", "songofsolomon"},
    {"sos", "songofsolomon"}, {"songofsongs", "songofsolomon"},
    {"canticles", "songofsolomon"},
    {"is", "isaiah"},        {"isa", "isaiah"},
    {"jr", "jeremiah"},      {"jer", "jeremiah"},
    {"ezk", "ezekiel"},      {"ezek", "ezekiel"},  {"dn", "daniel"},
    {"mt", "matthew"},       {"matt", "matthew"},
    {"mk", "mark"},          {"mrk", "mark"},      {"mr", "mark"},
    {"lk", "luke"},          {"luk", "luke"},
    {"jn", "john"},          {"jhn", "john"},
    {"rm", "romans"},
    {"phil", "philippians"}, {"php", "philippians"}, {"pp", "philippians"},
    {"phm", "philemon"},     {"phlm", "philemon"}, {"philem", "philemon"},
    {"jas", "james"},        {"jm", "james"},
    {"rev", "revelation"},   {"rv", "revelation"}, {"apoc", "revelation"},
    {"1jn", "1john"},        {"2jn", "2john"},     {"3jn", "3john"},
    {"1kgs", "1kings"},      {"2kgs", "2kings"},
    {"1chr", "1chronicles"}, {"2chr", "2chronicles"},
    {"1sm", "1samuel"},      {"2sm", "2samuel"},
};

const std::unordered_map<std::string_view, std::string_view> k_ordinal_words{
    {"i", "1"}, {"ii", "2"}, {"iii", "3"},
    {"first", "1"}, {"second", "2"}, {"third", "3"},
};

// Leading book ordinal.
const std::regex k_book_num_prefix_re(
    R"(^\s*([1-3]|i{1,3}|first|second|third)\b[\s.]*)",
    std::regex::icase | std::regex::ECMAScript);

[[nodiscard]] std::string normalize_book(std::string_view s_in)
{
    std::string s = util::trim(s_in);
    std::smatch m;
    if (std::regex_search(s, m, k_book_num_prefix_re) && m.position() == 0) {
        const auto raw = util::to_lower(m[1].str());
        std::string num;
        if (auto it = k_ordinal_words.find(raw); it != k_ordinal_words.end()) {
            num = std::string(it->second);
        } else {
            num = raw; // already 1/2/3
        }
        s = num + " " + s.substr(static_cast<std::size_t>(m.length()));
    }
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (util::is_alnum(c)) {
            out.push_back(util::to_lower(c));
        }
    }
    return out;
}

// Trailing chapter:verse[-verse] (en-dash normalized to '-' before match).
const std::regex k_ref_cv_re(
    R"((\d+)\s*[:.]\s*(\d+)(?:\s*-\s*(\d+))?\s*$)",
    std::regex::ECMAScript);
const std::regex k_ref_c_re(R"((\d+)\s*$)", std::regex::ECMAScript);

[[nodiscard]] std::string normalize_ref_dashes(std::string s)
{
    // UTF-8 en-dash U+2013 -> ASCII hyphen (std::regex is byte-oriented).
    for (;;) {
        const auto pos = s.find("\xE2\x80\x93");
        if (pos == std::string::npos) {
            break;
        }
        s.replace(pos, 3, "-");
    }
    return s;
}

}  // namespace

std::optional<std::string> resolve_bible_book(const std::vector<std::string>& books,
                                              std::string_view token)
{
    if (token.empty() || books.empty()) {
        return std::nullopt;
    }
    std::unordered_map<std::string, std::string> norm_map;
    for (const auto& book : books) {
        norm_map.emplace(normalize_book(book), book);
    }
    const auto t = normalize_book(token);
    if (t.empty()) {
        return std::nullopt;
    }
    if (auto it = norm_map.find(t); it != norm_map.end()) {
        return it->second;
    }

    std::optional<std::string> alias;
    if (auto ait = k_book_aliases.find(t); ait != k_book_aliases.end()) {
        alias = std::string(ait->second);
        if (auto it = norm_map.find(*alias); it != norm_map.end()) {
            return it->second;
        }
    }

    auto unique_prefix = [&](std::string_view target) -> std::optional<std::string> {
        std::optional<std::string> hit;
        for (const auto& [n, book] : norm_map) {
            if (n.size() >= target.size()
                && n.compare(0, target.size(), target) == 0) {
                if (hit) {
                    return std::nullopt;
                }
                hit = book;
            }
        }
        return hit;
    };

    const std::vector<std::string_view> targets =
        alias ? std::vector<std::string_view>{t, *alias}
              : std::vector<std::string_view>{t};
    for (auto target : targets) {
        if (auto hit = unique_prefix(target)) {
            return hit;
        }
    }
    return std::nullopt;
}

std::vector<std::string> suggest_bible_books(const std::vector<std::string>& books,
                                             std::string_view token)
{
    std::vector<std::string> out;
    if (token.empty() || books.empty()) {
        return out;
    }
    const auto t = normalize_book(token);
    if (t.empty()) {
        return out;
    }

    auto starts_with = [](std::string_view s, std::string_view pfx) {
        return s.size() >= pfx.size() && s.compare(0, pfx.size(), pfx) == 0;
    };

    std::unordered_set<std::string> seen;
    for (const auto& book : books) {
        const auto n = normalize_book(book);
        if (n.empty() || seen.count(n)) {
            continue;
        }
        bool match = starts_with(n, t);
        if (!match) {
            for (const auto& [alias, target] : k_book_aliases) {
                if (target != n) {
                    continue;
                }
                if (starts_with(alias, t)) {
                    match = true;
                    break;
                }
            }
        }
        if (!match) {
            continue;
        }
        seen.insert(n);
        out.push_back(book);
    }
    return out;
}

std::string format_verse_ref(VerseRef ref)
{
    return std::to_string(ref.chapter) + ':' + std::to_string(ref.verse);
}

std::optional<VerseRef> parse_verse_ref(std::string_view text)
{
    // Up to three digits, each side: nothing in range is longer.
    const auto read_number = [&](std::size_t& i) -> std::optional<int> {
        const std::size_t start = i;
        int value = 0;
        while (i < text.size() && i - start < 3 && util::is_digit(text[i])) {
            value = value * 10 + (text[i] - '0');
            ++i;
        }
        if (i == start || (i < text.size() && util::is_digit(text[i]))) {
            return std::nullopt;
        }
        return value;
    };
    std::size_t i = 0;
    const auto chapter = read_number(i);
    if (!chapter || i >= text.size() || text[i] != ':') {
        return std::nullopt;
    }
    ++i;
    const auto verse = read_number(i);
    if (!verse || i != text.size()) {
        return std::nullopt;
    }
    if (*chapter < 1 || *chapter > k_max_bible_chapter || *verse < 1
        || *verse > k_max_bible_verse) {
        return std::nullopt;
    }
    return VerseRef{.chapter = *chapter, .verse = *verse};
}

std::optional<LeadingVerseRef> split_leading_verse_ref(std::string_view text)
{
    const auto space = text.find(' ');
    const auto ref = parse_verse_ref(text.substr(0, space));
    if (!ref) {
        return std::nullopt;
    }
    return LeadingVerseRef{.ref = *ref,
                           .rest = space == std::string_view::npos ? std::string_view{}
                                                                   : text.substr(space + 1)};
}

std::optional<model::BibleRef> parse_bible_reference(
    std::string_view reference,
    const std::vector<std::string>& books)
{
    const std::string ref = normalize_ref_dashes(util::trim(reference));
    if (ref.empty()) {
        return std::nullopt;
    }

    std::optional<int> verse_start;
    std::optional<int> verse_end;
    int chapter = 0;
    std::string book_part;

    // The groups are digit runs of any length; a number too large for an int
    // cannot name a chapter or verse, so the reference simply does not resolve.
    std::smatch m;
    if (std::regex_search(ref, m, k_ref_cv_re)) {
        const auto c = util::parse_leading_int(m[1].str());
        const auto vs = util::parse_leading_int(m[2].str());
        const auto ve = m[3].matched ? util::parse_leading_int(m[3].str()) : vs;
        if (!c || !vs || !ve) {
            return std::nullopt;
        }
        chapter = *c;
        verse_start = *vs;
        verse_end = *ve;
        book_part = ref.substr(0, static_cast<std::size_t>(m.position()));
    } else if (std::regex_search(ref, m, k_ref_c_re)) {
        const auto c = util::parse_leading_int(m[1].str());
        if (!c) {
            return std::nullopt;
        }
        chapter = *c;
        book_part = ref.substr(0, static_cast<std::size_t>(m.position()));
    } else {
        return std::nullopt;
    }

    auto book = resolve_bible_book(books, book_part);
    if (!book || chapter < 1) {
        return std::nullopt;
    }
    if (verse_start && verse_end && *verse_end < *verse_start) {
        std::swap(*verse_start, *verse_end);
    }

    model::BibleRef out;
    out.book = std::move(*book);
    out.chapter = chapter;
    out.verse_start = verse_start;
    out.verse_end = verse_end;
    return out;
}

ParsedBible parse_bible_file(const std::filesystem::path& path)
{
    return with_xml_errors<BibleXmlError>([&] { return parse_bytes(read_file_bytes(path)); });
}

ParsedBible parse_bible_bytes(std::string_view data)
{
    return with_xml_errors<BibleXmlError>([&] { return parse_bytes(data); });
}

}  // namespace seventhslide::parse
