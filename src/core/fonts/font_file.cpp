#include "seventhslide/fonts/font_file.hpp"

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace seventhslide::fonts {
namespace {

// Offsets into the tables, from the OpenType specification.
constexpr std::size_t k_sfnt_header_bytes = 12;
constexpr std::size_t k_table_record_bytes = 16;
constexpr std::size_t k_head_bytes = 54;
constexpr std::size_t k_head_adjustment_at = 8;
constexpr std::size_t k_head_magic_at = 12;
constexpr std::size_t k_head_units_at = 18;
constexpr std::size_t k_head_mac_style_at = 44;
constexpr std::uint32_t k_head_magic = 0x5F0F3CF5;
constexpr std::uint32_t k_whole_file_checksum = 0xB1B0AFBA;
constexpr std::size_t k_maxp_bytes = 6;
constexpr std::size_t k_maxp_glyphs_at = 4;
constexpr std::size_t k_hhea_bytes = 36;
constexpr std::size_t k_hhea_metrics_at = 34;
constexpr std::size_t k_hmetric_bytes = 4;
constexpr std::size_t k_os2_selection_at = 62;
constexpr std::size_t k_name_header_bytes = 6;
constexpr std::size_t k_name_record_bytes = 12;
constexpr std::uint16_t k_name_family = 1;
constexpr std::uint16_t k_name_subfamily = 2;
constexpr std::uint32_t k_collection_header_bytes = 12;
/// More faces than any collection has; a file saying otherwise is damaged.
constexpr std::uint32_t k_max_collection_faces = 1024;

constexpr std::string_view k_damaged = "This font file is damaged.";

[[nodiscard]] std::uint16_t u16_at(std::string_view d, std::size_t at)
{
    return static_cast<std::uint16_t>((static_cast<unsigned char>(d[at]) << 8)
                                      | static_cast<unsigned char>(d[at + 1]));
}

[[nodiscard]] std::uint32_t u32_at(std::string_view d, std::size_t at)
{
    return (static_cast<std::uint32_t>(u16_at(d, at)) << 16) | u16_at(d, at + 2);
}

void put_u16(std::string& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value & 0xff));
}

void put_u32(std::string& out, std::uint32_t value)
{
    put_u16(out, static_cast<std::uint16_t>(value >> 16));
    put_u16(out, static_cast<std::uint16_t>(value & 0xffff));
}

void set_u32(std::string& out, std::size_t at, std::uint32_t value)
{
    std::string bytes;
    put_u32(bytes, value);
    out.replace(at, bytes.size(), bytes);
}

/// A font's tables by tag. The views are into the file it was read from.
struct Sfnt {
    std::string_view version;
    std::map<std::string_view, std::string_view> tables;

    [[nodiscard]] std::string_view table(std::string_view tag) const
    {
        const auto it = tables.find(tag);
        return it == tables.end() ? std::string_view{} : it->second;
    }
};

[[nodiscard]] bool is_sfnt_version(std::string_view version)
{
    return version == std::string_view("\0\1\0\0", 4) || version == "OTTO" || version == "true";
}

/// The font whose table directory is at ``base`` of ``file``. A table's offset
/// is from the start of the file, in a collection too. Nullopt when it does
/// not read as a font.
[[nodiscard]] std::optional<Sfnt> read_sfnt(std::string_view file, std::size_t base)
{
    if (base > file.size() || file.size() - base < k_sfnt_header_bytes) {
        return std::nullopt;
    }
    Sfnt sfnt;
    sfnt.version = file.substr(base, 4);
    if (!is_sfnt_version(sfnt.version)) {
        return std::nullopt;
    }
    const std::size_t count = u16_at(file, base + 4);
    if (count == 0 || (file.size() - base - k_sfnt_header_bytes) / k_table_record_bytes < count) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = base + k_sfnt_header_bytes + i * k_table_record_bytes;
        const std::size_t offset = u32_at(file, record + 8);
        const std::size_t length = u32_at(file, record + 12);
        if (offset > file.size() || length > file.size() - offset) {
            return std::nullopt;
        }
        sfnt.tables.emplace(file.substr(record, 4), file.substr(offset, length));
    }
    return sfnt;
}

/// A Windows or Unicode name (UTF-16BE) as UTF-8, the basic plane only: font
/// names have nothing beyond it.
[[nodiscard]] std::string utf16be_to_utf8(std::string_view data)
{
    std::string out;
    for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
        const auto cp = u16_at(data, i);
        if (cp == 0 || (cp >= 0xd800 && cp <= 0xdfff)) {
            continue;
        }
        util::append_utf8(out, cp);
    }
    return out;
}

/// The value of a name record, and how much it is preferred; nullopt for an
/// encoding not read.
[[nodiscard]] std::optional<std::pair<std::string, int>> name_value(std::uint16_t platform,
                                                                    std::uint16_t encoding,
                                                                    std::uint16_t language,
                                                                    std::string_view text)
{
    constexpr std::uint16_t k_windows = 3;
    constexpr std::uint16_t k_unicode = 0;
    constexpr std::uint16_t k_macintosh = 1;
    constexpr std::uint16_t k_windows_us_english = 0x0409;
    constexpr std::uint16_t k_windows_english = 0x09;  // the low ten bits: any English
    constexpr std::uint16_t k_windows_language_mask = 0x03ff;
    constexpr std::uint16_t k_mac_english = 0;
    // English before anything else: US English, then any other, then whatever
    // the font has.
    if (platform == k_windows && (encoding == 0 || encoding == 1 || encoding == 10)) {
        const int score = language == k_windows_us_english ? 5
                          : (language & k_windows_language_mask) == k_windows_english ? 4
                                                                                      : 1;
        return std::pair{utf16be_to_utf8(text), score};
    }
    if (platform == k_unicode) {
        return std::pair{utf16be_to_utf8(text), 3};
    }
    // Mac Roman: only what it shares with ASCII is read as it stands.
    if (platform == k_macintosh && encoding == 0
        && std::ranges::all_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
        return std::pair{std::string(text), language == k_mac_english ? 2 : 0};
    }
    return std::nullopt;
}

[[nodiscard]] std::pair<std::string, std::string> names_in(std::string_view name)
{
    std::string family;
    std::string subfamily;
    int family_score = -1;
    int subfamily_score = -1;
    if (name.size() < k_name_header_bytes) {
        return {};
    }
    const std::size_t count = u16_at(name, 2);
    const std::size_t strings = u16_at(name, 4);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = k_name_header_bytes + i * k_name_record_bytes;
        if (name.size() < record + k_name_record_bytes) {
            break;
        }
        const auto id = u16_at(name, record + 6);
        if (id != k_name_family && id != k_name_subfamily) {
            continue;
        }
        const std::size_t length = u16_at(name, record + 8);
        const auto at = strings + u16_at(name, record + 10);
        if (at > name.size() || length > name.size() - at) {
            continue;
        }
        auto value = name_value(u16_at(name, record), u16_at(name, record + 2),
                                u16_at(name, record + 4), name.substr(at, length));
        if (!value || value->first.empty()) {
            continue;
        }
        auto& best = id == k_name_family ? family : subfamily;
        auto& score = id == k_name_family ? family_score : subfamily_score;
        if (value->second > score) {
            score = value->second;
            best = std::move(value->first);
        }
    }
    return {util::trim(family), util::trim(subfamily)};
}

[[nodiscard]] bool has_control_character(std::string_view text)
{
    return std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

/// Whether ``s`` has the tables a font cannot be drawn without, whole enough
/// to read. Not whether each is right: that is the font engine's to say.
[[nodiscard]] bool has_usable_tables(const Sfnt& s)
{
    const auto head = s.table("head");
    if (head.size() < k_head_bytes || u32_at(head, k_head_magic_at) != k_head_magic
        || u16_at(head, k_head_units_at) == 0) {
        return false;
    }
    const auto maxp = s.table("maxp");
    if (maxp.size() < k_maxp_bytes || u16_at(maxp, k_maxp_glyphs_at) == 0) {
        return false;
    }
    const auto hhea = s.table("hhea");
    if (hhea.size() < k_hhea_bytes) {
        return false;
    }
    const std::size_t metrics = u16_at(hhea, k_hhea_metrics_at);
    return metrics > 0 && s.table("hmtx").size() / k_hmetric_bytes >= metrics
           && s.table("cmap").size() >= 4;
}

[[nodiscard]] bool has_outlines(const Sfnt& s)
{
    return (!s.table("glyf").empty() && !s.table("loca").empty()) || !s.table("CFF ").empty()
           || !s.table("CFF2").empty();
}

/// ``style``'s words, whichever way they are run together: "BoldItalic",
/// "Bold-Italic" and "Bold Italic" are two. A word that only qualifies the next
/// stays with it, as "Semi Bold" is not bold but a weight of its own.
[[nodiscard]] std::vector<std::string> style_words(std::string_view style)
{
    std::vector<std::string> words;
    std::string word;
    const auto end_word = [&] {
        if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    };
    char before = ' ';
    for (const char c : style) {
        if (c == ' ' || c == '-' || c == '_') {
            end_word();
        } else {
            const auto is_lower = [](char x) { return x >= 'a' && x <= 'z'; };
            const auto is_upper = [](char x) { return x >= 'A' && x <= 'Z'; };
            if (is_lower(before) && is_upper(c)) {
                end_word();
            }
            word.push_back(c);
        }
        before = c;
    }
    end_word();

    std::vector<std::string> out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto lower = util::to_lower(words[i]);
        const bool qualifier =
            lower == "semi" || lower == "demi" || lower == "extra" || lower == "ultra";
        if (qualifier && i + 1 < words.size()) {
            out.push_back(words[i] + words[i + 1]);
            ++i;
        } else {
            out.push_back(words[i]);
        }
    }
    return out;
}

/// What ``style`` says, read against ``family``: bold, italic, and any other
/// words the family does not already end in ("Light Oblique" is italic and
/// "Light", which "Foo Light" already has).
struct StyleWords {
    bool bold = false;
    bool italic = false;
    std::string rest;
};

[[nodiscard]] StyleWords read_style_words(std::string_view family, std::string_view style)
{
    StyleWords out;
    const auto lower_family = util::to_lower(family);
    for (const auto& word : style_words(style)) {
        const auto lower = util::to_lower(word);
        if (lower == "bold") {
            out.bold = true;
        } else if (lower == "italic" || lower == "oblique") {
            out.italic = true;
        } else if (lower != "regular" && lower != "normal" && lower != "book" && lower != "roman"
                   && !lower_family.ends_with(" " + lower)) {
            out.rest += (out.rest.empty() ? "" : " ") + word;
        }
    }
    return out;
}

[[nodiscard]] std::string style_named(bool bold, bool italic)
{
    if (bold) {
        return italic ? "Bold Italic" : "Bold";
    }
    return italic ? "Italic" : "Regular";
}

/// The face ``s`` is, all but its bytes; or why it is refused.
[[nodiscard]] FontFileRead face_of(const Sfnt& s)
{
    FontFileRead read;
    if (!has_usable_tables(s)) {
        read.refusal = k_damaged;
        return read;
    }
    if (!has_outlines(s)) {
        read.refusal = "This font holds pictures of its letters and no outlines, so it cannot "
                       "be drawn at any size.";
        return read;
    }
    auto [family, style] = names_in(s.table("name"));
    if (family.empty() || has_control_character(family) || has_control_character(style)) {
        read.refusal = "This font file does not say what the font is called.";
        return read;
    }
    FontFileFace face;
    const auto words = read_style_words(family, style);
    const auto head = s.table("head");
    const auto mac_style = u16_at(head, k_head_mac_style_at);
    const auto os2 = s.table("OS/2");
    const std::uint16_t selection =
        os2.size() >= k_os2_selection_at + 2 ? u16_at(os2, k_os2_selection_at) : 0;
    constexpr std::uint16_t k_selection_italic = 1U << 0U;
    constexpr std::uint16_t k_selection_bold = 1U << 5U;
    constexpr std::uint16_t k_selection_oblique = 1U << 9U;
    face.bold = words.bold || (mac_style & 1U) != 0 || (selection & k_selection_bold) != 0;
    face.italic = words.italic || (mac_style & 2U) != 0
                  || (selection & (k_selection_italic | k_selection_oblique)) != 0;
    // Its own style stays when it says what this face is ("Book"). Otherwise the
    // family has the rest of what it said, and the slot's name says the rest.
    const bool says_what_it_is =
        !style.empty() && words.rest.empty() && words.bold == face.bold && words.italic == face.italic
        && !util::to_lower(family).ends_with(" " + util::to_lower(style));
    if (!words.rest.empty()) {
        family += " " + words.rest;
    }
    face.family = std::move(family);
    face.style = says_what_it_is ? std::move(style) : style_named(face.bold, face.italic);
    face.variable = !s.table("fvar").empty();
    face.extension = s.version == "OTTO" ? ".otf" : ".ttf";
    read.faces.push_back(std::move(face));
    return read;
}

/// The big-endian sum of ``bytes`` as 32-bit words, the last padded with zeros.
[[nodiscard]] std::uint32_t checksum(std::string_view bytes)
{
    std::uint32_t sum = 0;
    std::size_t at = 0;
    for (; at + 4 <= bytes.size(); at += 4) {
        sum += u32_at(bytes, at);
    }
    std::uint32_t last = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        last = (last << 8U)
               | (at + i < bytes.size() ? static_cast<unsigned char>(bytes[at + i]) : 0U);
    }
    return sum + last;
}

/// A font file of ``s``'s tables alone, each table's checksum and the file's
/// as the format has them (a face in a collection has no file checksum of its
/// own, and a system's font loader may want one).
[[nodiscard]] std::string file_of(const Sfnt& s)
{
    const auto count = static_cast<std::uint16_t>(s.tables.size());
    std::uint16_t selector = 0;
    while ((2U << selector) <= count) {
        ++selector;
    }
    const auto search_range = static_cast<std::uint16_t>((1U << selector) * k_table_record_bytes);
    std::string out(s.version);
    put_u16(out, count);
    put_u16(out, search_range);
    put_u16(out, selector);
    put_u16(out, static_cast<std::uint16_t>(count * k_table_record_bytes - search_range));

    std::size_t offset = k_sfnt_header_bytes + count * k_table_record_bytes;
    for (const auto& [tag, table] : s.tables) {
        out += tag;
        put_u32(out, 0);  // its checksum, below
        put_u32(out, static_cast<std::uint32_t>(offset));
        put_u32(out, static_cast<std::uint32_t>(table.size()));
        offset += (table.size() + 3) / 4 * 4;
    }
    std::size_t record = k_sfnt_header_bytes;
    std::size_t head_at = 0;
    for (const auto& [tag, table] : s.tables) {
        const auto at = out.size();
        out += table;
        out.resize((out.size() + 3) / 4 * 4, '\0');
        if (tag == "head") {
            head_at = at;
            set_u32(out, at + k_head_adjustment_at, 0);
        }
        set_u32(out, record + 4, checksum(std::string_view(out).substr(at, table.size())));
        record += k_table_record_bytes;
    }
    set_u32(out, head_at + k_head_adjustment_at, k_whole_file_checksum - checksum(out));
    return out;
}

/// Whether ``faces`` can each have a slot of their own. Two that would share
/// one take care of it by name where the file says more: "Book" and "Roman" are
/// both regular, so the second is the family "Avenir Roman". False when the file
/// has the same face twice.
[[nodiscard]] bool settle_slots(std::vector<FontFileFace>& faces)
{
    std::set<std::tuple<std::string, bool, bool>> taken;
    const auto take = [&](const FontFileFace& face) {
        return taken.emplace(util::to_lower(face.family), face.bold, face.italic).second;
    };
    for (auto& face : faces) {
        if (take(face)) {
            continue;
        }
        face.family += " " + face.style;
        face.style = style_named(face.bold, face.italic);
        if (!take(face)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] FontFileRead read_collection(std::string_view file)
{
    FontFileRead read;
    const auto refuse = [&](std::string_view why) {
        FontFileRead refused;
        refused.refusal = why;
        return refused;
    };
    if (file.size() < k_collection_header_bytes) {
        return refuse(k_damaged);
    }
    const auto count = u32_at(file, 8);
    if (count == 0 || count > k_max_collection_faces
        || (file.size() - k_collection_header_bytes) / 4 < count) {
        return refuse(k_damaged);
    }
    // Every face is checked before any is copied out: only then are the bytes
    // these tables come to known to be reasonable.
    std::vector<Sfnt> fonts;
    std::int64_t total = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto sfnt = read_sfnt(file, u32_at(file, k_collection_header_bytes + i * 4));
        if (!sfnt) {
            return refuse(k_damaged);
        }
        auto face = face_of(*sfnt);
        if (!face.refusal.empty()) {
            return face;
        }
        std::int64_t bytes = 0;
        for (const auto& [tag, table] : sfnt->tables) {
            bytes += static_cast<std::int64_t>(table.size());
        }
        if (bytes > k_max_font_file_bytes) {
            return refuse(k_damaged);
        }
        total += bytes;
        if (total > k_max_collection_face_bytes) {
            return refuse("This font collection is too large.");
        }
        read.faces.push_back(std::move(face.faces.front()));
        fonts.push_back(std::move(*sfnt));
    }
    if (!settle_slots(read.faces)) {
        return refuse("This font collection has the same font in it twice.");
    }
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        read.faces[i].bytes = file_of(fonts[i]);
    }
    return read;
}

}  // namespace

FontFileRead read_font_file(std::string_view bytes)
{
    const auto tag = bytes.substr(0, 4);
    if (tag == "wOFF" || tag == "wOF2") {
        FontFileRead read;
        read.refusal = "This is a font for web pages. Use the desktop version, a TTF or OTF file.";
        return read;
    }
    if (tag == "ttcf") {
        return read_collection(bytes);
    }
    const auto sfnt = read_sfnt(bytes, 0);
    if (!sfnt) {
        FontFileRead read;
        read.refusal = is_sfnt_version(tag) ? std::string(k_damaged)
                                            : std::string("This is not a font file.");
        return read;
    }
    auto read = face_of(*sfnt);
    if (read.refusal.empty()) {
        read.faces.front().bytes = bytes;
    }
    return read;
}

}  // namespace seventhslide::fonts
