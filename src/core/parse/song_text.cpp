// SongSelect's lyrics .txt and plain text; see song_readers.hpp.
//
// SongSelect: the title, then each section as a heading line ("Verse 1")
// and its lines, two blank lines apart, then the footer (song_sections.hpp).
// A "Misc" section may open with "(PRE-CHORUS)", which is what it is.
// Plain text: the same without a footer, the title only when it stands
// apart, and a blank line starting a new verse where no heading does.
// Chords on a line of their own above the words are set into them.

#include "song_readers.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/util/strings.hpp>

#include <optional>
#include <utility>

namespace seventhslide::parse {
namespace {

[[nodiscard]] std::string_view rtrim(std::string_view s)
{
    while (!s.empty() && util::is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

/// Lines to sections. A heading is held until words, a blank line or
/// another heading follow: a heading right under another names what the
/// first is ("Misc" then "(PRE-CHORUS)"), and one with no words repeats.
class BodyReader {
public:
    void read(std::string_view raw)
    {
        const auto line = rtrim(raw);
        if (util::trim_view(line).empty()) {
            flush_chords();
            blank_ = true;
            return;
        }
        if (auto heading = heading_label(line)) {
            flush_chords();
            if (heading_ && blank_) {
                builder_.begin(heading_->type, heading_->number);
            }
            heading_ = std::move(heading);
            blank_ = false;
            return;
        }
        if (heading_) {
            builder_.begin(heading_->type, heading_->number);
            heading_.reset();
        } else if (blank_ && builder_.open_has_lines()) {
            builder_.begin("Verse");
        }
        blank_ = false;
        if (is_chord_line(line)) {
            flush_chords();
            chords_ = std::string(line);
            return;
        }
        const auto markup = chords_.empty() ? model::escape_markup_text(util::trim_view(line))
                                            : chords_over_words(chords_, line);
        chords_.clear();
        builder_.line(util::trim(markup));
    }

    [[nodiscard]] SongBuilder::Result finish()
    {
        flush_chords();
        if (heading_) {
            builder_.begin(heading_->type, heading_->number);
        }
        return builder_.finish();
    }

private:
    /// Chords over no words: a line of chords alone.
    void flush_chords()
    {
        if (!chords_.empty()) {
            builder_.line(chords_only(chords_));
            chords_.clear();
        }
    }

    SongBuilder builder_;
    std::optional<SectionHeading> heading_;
    std::string chords_;
    bool blank_ = false;
};

}  // namespace

std::vector<ImportedSong> read_lyrics_text(std::string_view text,
                                           std::string_view fallback_title,
                                           bool songselect)
{
    const auto lines = split_lines(text);
    ImportedSong imported;
    imported.format = songselect ? SongFormat::SongSelectText : SongFormat::PlainText;
    model::Song& song = imported.song;

    std::size_t i = 0;
    while (i < lines.size() && util::trim_view(lines[i]).empty()) {
        ++i;
    }
    const std::size_t footer = songselect ? ccli_footer_start(lines, i) : lines.size();
    if (i < footer) {
        const auto first = util::trim_view(lines[i]);
        // Plain text names its title only on a line of its own.
        const bool stands_apart = i + 1 >= footer || util::trim_view(lines[i + 1]).empty()
            || heading_label(lines[i + 1]).has_value();
        if (songselect || (!heading_label(first) && !is_chord_line(first) && stands_apart)) {
            song.title = std::string(first);
            ++i;
        }
    }
    if (song.title.empty()) {
        song.title = std::string(fallback_title);
    }

    BodyReader body;
    for (; i < footer; ++i) {
        body.read(lines[i]);
    }
    auto built = body.finish();
    song.lyrics = std::move(built.lyrics);
    song.verse_order = std::move(built.verse_order);

    if (footer < lines.size()) {
        auto info = read_ccli_footer({lines.begin() + static_cast<std::ptrdiff_t>(footer), lines.end()});
        song.ccli_song_number = std::move(info.ccli_number);
        song.copyright = std::move(info.copyright);
        for (auto& name : info.authors) {
            song.authors.emplace_back(std::move(name));
        }
    }
    return {std::move(imported)};
}

}  // namespace seventhslide::parse
