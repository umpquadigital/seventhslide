#pragma once

// What the text song formats (ChordPro, OpenSong, SongSelect, plain text)
// share: section headings, building a song's lyrics and verse order from its
// sections, and chords written on a line above the words.
// Seam: plain C++20 (no Qt).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::parse {

/// A section heading: one of ``k_verse_types``' names (never Title) and a
/// number, 0 when the heading gave none.
struct SectionHeading {
    std::string type;
    int number = 0;

    [[nodiscard]] bool operator==(const SectionHeading&) const = default;
};

/// ``line`` read as a heading on a line of its own: "Verse 2", "CHORUS",
/// "Pre-Chorus 1:", "[Bridge]", "(PRE-CHORUS)", "Refrain", the common
/// German, Spanish, French, Dutch and Portuguese words ("Vers 1", "Coro"),
/// Outro/Coda as Ending, and Tag/Vamp/Interlude/Misc as Other. Nullopt for
/// anything else, words of a song among them.
[[nodiscard]] std::optional<SectionHeading> heading_label(std::string_view line);

/// A song's lyrics and verse order, built a section at a time.
///
/// A section met again with the same words (a chorus sung after every
/// verse, written out each time) is a verse order entry, not a second copy;
/// with other words it is numbered on. A heading with no words after it
/// repeats the section it names. The verse order is kept only when it
/// differs from the lyrics' own order.
class SongBuilder {
public:
    /// Start a section, ending the one before. ``number`` 0 takes the
    /// section of this type with the same words, or the next free number.
    void begin(std::string_view type, int number = 0);

    /// A lyric line of the current section (stored markup), starting a
    /// verse when none is open. Blank lines are skipped.
    void line(std::string markup);

    /// A slide break inside the current section.
    void slide_break();

    /// End the current section, if any.
    void end();

    /// True when no section has words yet.
    [[nodiscard]] bool empty() const noexcept;

    /// True when a section is open and has words.
    [[nodiscard]] bool open_has_lines() const noexcept;

    struct Result {
        /// ``---[Type:N]---`` sections, a blank line apart.
        std::string lyrics;
        std::string verse_order;
    };

    /// End the current section, and the song so far.
    [[nodiscard]] Result finish();

private:
    struct Section {
        std::string type;
        int number = 0;
        std::vector<std::string> lines;
    };

    [[nodiscard]] std::string code_of(const Section& section) const;

    std::vector<Section> sections_;
    std::vector<std::string> order_;
    std::optional<Section> open_;
    bool open_explicit_number_ = false;
};

/// True when every word of ``line`` is a chord ("G", "C#m7/G#", "Ebmaj7",
/// "N.C.") or a bar line, and there is at least one chord.
[[nodiscard]] bool is_chord_line(std::string_view line);

/// ``lyric`` (plain text) as stored markup with the chords of
/// ``chord_line`` set in as ``[X]`` at the columns they stand over (in
/// characters). A chord past the end of the words is added after them.
[[nodiscard]] std::string chords_over_words(std::string_view chord_line, std::string_view lyric);

/// ``chord_line``'s chords alone, as a line of stored markup (``[G] [C]``),
/// for chords over no words.
[[nodiscard]] std::string chords_only(std::string_view chord_line);

/// Writers' names in one field: split at ``|``, ``;``, `` & `` and `` and ``,
/// and at ``,`` too when ``commas``. Empty names are dropped.
[[nodiscard]] std::vector<std::string> split_names(std::string_view text, bool commas);

/// What the footer of a SongSelect download says: the lines from the
/// writers or "CCLI Song #" down. Either layout (before 2023: number,
/// writers, copyright; since: writers, number, copyright). The terms of use
/// and the church's "CCLI License #" are not the song's and are dropped.
struct CcliFooter {
    std::string ccli_number;
    std::vector<std::string> authors;
    std::string copyright;
};
[[nodiscard]] CcliFooter read_ccli_footer(const std::vector<std::string_view>& lines);

/// Where the footer of a SongSelect download starts in ``lines``: at the
/// writers line before "CCLI Song #" (2023 on) or at that line itself; or
/// ``lines.size()`` when there is none. ``from`` is where to start looking.
[[nodiscard]] std::size_t ccli_footer_start(const std::vector<std::string_view>& lines,
                                            std::size_t from = 0);

/// ``text``'s lines (``\n``, ``\r\n`` or ``\r``), untrimmed.
[[nodiscard]] std::vector<std::string_view> split_lines(std::string_view text);

}  // namespace seventhslide::parse
