// A song in a Lyrics box, audited with real fonts across many boxes and looks,
// chords on and off: every slide paints exactly the height layout planned for
// it, no row is wider than the box but for one character or one chord name
// that is, a slide fits its box unless it is a single row or a single word,
// and no line or chord is lost, cut lines included. And a song saved as
// Freeform paints as it did: the same rows on every slide, inside the box.
//
// The everyday looks run with the rest of the tests. The full audit is hidden
// ([.audit]): real fonts over some tens of thousands of slides. Run it after a
// change to song layout, chords, Freeform conversion or text paint. With
// SEVENTHSLIDE_CHORD_AUDIT_DIR set it also paints a few slides there as PNGs.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/freeform_convert.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/util/strings.hpp>

#include <QImage>
#include <QPainter>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <string>
#include <vector>

using seventhslide::layout::ChordItemKind;
using seventhslide::layout::ChordLine;
using seventhslide::scene::SceneLineRole;

namespace {

constexpr const char* k_songs[] = {
    // The designer's sample.
    "---[Verse:1]---\n"
    "[G]Soon the heavens will [C]open [G]wide,\n"
    "Christ will [D]come to claim His [G]bride.\n"
    "All the [C]universe will [G]sing,\n"
    "Halle[Em]lujah! [D]Christ is [G]King!\n"
    "We have this hope, this faith and God's great love.\n"
    "We are united in Christ.\n",
    // Long lines, long chord names, chords alone, inside words and at the end,
    // styled words, sentences and clauses to split at.
    "---[Verse:1]---\n"
    "[G]Amazing [D/F#]grace how [Cmaj7]sweet the [G]sound that [Dsus4]saved a "
    "[Em7]wretch like [C]me. I [G]once was [D]lost, but [G]now am [Am7]found; was "
    "[Bb]blind but [F#m7b5]now I [G]see[D]\n"
    "[G] [D] [Em] [C] [Gmaj7] [Dsus2/F#] [Em7] [Cadd9]\n"
    "<b>[G]Twas grace that</b> [D]taught my [F] <i>heart to [C]fear</i>, and "
    "Halle[Em]lu[D]jah[C]lujah [G]grace my fears re[D]lieved\n"
    "---[Chorus:1]---\n"
    "[C]How [G]pre[Am]cious [F]did that [C]grace ap[G]pear [C]\n"
    "Supercalifragilistic[G]expialidocious[D]antidisestablishmentarianism\n"
    "No chords on this line, which is long enough to wrap in most of the boxes here\n"
    "[A]Short\n",
    // Text sizes of its own, on lines with chords and without.
    "---[Verse:1]---\n"
    "Some small words and <size=150>some very large ones</size> and small again\n"
    "<size=200>All of it large</size>, then not\n"
    "<size=50>A quiet line</size>\n"
    "[G]Chords <size=150>over [D]large</size> words\n",
};

struct Look {
    bool show_chords;
    const char* family;
    int font_size;
    int width;
    int height;
    int chord_size_pct;
    int chord_gap_pct;
    double line_height;
    double letter_spacing;
    bool bold;
    int follow_lines;
    int active_size;
    bool balance = false;
    int verse_gap = 0;
};

[[nodiscard]] std::vector<Look> looks()
{
    std::vector<Look> out;
    for (const bool chords : {true, false}) {
        for (const char* family : {"EB Garamond", "Liberation Sans"}) {
            for (const int size : {36, 60, 110}) {
                for (const int width : {260, 700, 1690}) {
                    for (const int height : {120, 360, 900}) {
                        for (const auto& [chord_pct, gap] :
                             {std::pair{75, 15}, {150, 0}, {40, 60}}) {
                            out.push_back({chords, family, size, width, height, chord_pct, gap,
                                           1.2, 0.0, false, 0, 0});
                        }
                        out.push_back({chords, family, size, width, height, 75, 15, 2.0, 4.0,
                                       true, 0, 0});
                        out.push_back({chords, family, size, width, height, 90, 20, 1.0, 0.0,
                                       false, 1, size * 3 / 2});
                        out.push_back({chords, family, size, width, height, 75, 15, 1.2, 0.0,
                                       false, 2, size * 2 / 3});
                        out.push_back({chords, family, size, width, height, 75, 15, 1.2, 0.0,
                                       false, 0, 0, true});
                        out.push_back({chords, family, size, width, height, 75, 15, 1.2, 0.0,
                                       false, 0, 0, false, 40});
                    }
                }
            }
        }
    }
    return out;
}

/// A few ordinary boxes, chords on and off, with and without following lines.
[[nodiscard]] std::vector<Look> everyday_looks()
{
    std::vector<Look> out;
    for (const bool chords : {true, false}) {
        for (const int width : {700, 1690}) {
            for (const int height : {360, 900}) {
                out.push_back({chords, "X", 60, width, height, 75, 15, 1.2, 0.0, false, 0, 0});
                out.push_back({chords, "X", 60, width, height, 90, 20, 1.0, 0.0, false, 1, 90});
                out.push_back({chords, "X", 60, width, height, 75, 15, 1.2, 0.0, false, 2, 54});
                out.push_back({chords, "X", 60, width, height, 75, 15, 1.2, 0.0, false, 0, 0, true});
                out.push_back({chords, "X", 60, width, height, 75, 15, 1.2, 0.0, false, 0, 0, false, 40});
            }
        }
    }
    return out;
}

[[nodiscard]] nlohmann::json flat_of(const Look& look)
{
    return {
        {"font_family", look.family},
        {"font_size", look.font_size},
        {"width_px", look.width},
        {"height_px", look.height},
        {"show_chords", look.show_chords},
        {"chord_size_pct", look.chord_size_pct},
        {"chord_gap_pct", look.chord_gap_pct},
        {"line_height", look.line_height},
        {"letter_spacing", look.letter_spacing},
        {"bold", look.bold},
        {"follow_lines", look.follow_lines},
        {"active_line", {{"size", look.active_size}}},
        {"balance_wrapped_lines", look.balance},
        {"balance_wrapped_strength", 100},
        {"fluid_slides", look.verse_gap > 0},
        {"verse_gap", look.verse_gap},
    };
}

[[nodiscard]] std::vector<std::string> chord_names(const ChordLine& line)
{
    std::vector<std::string> out;
    for (const auto& item : line.items) {
        if (item.kind == ChordItemKind::Cell && item.cell.chord && !item.cell.chord->empty()) {
            out.push_back(*item.cell.chord);
        }
    }
    return out;
}

/// Every chord of ``song``, in order, as paint writes them.
[[nodiscard]] std::vector<std::string> song_chords(std::string_view song)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < song.size()) {
        auto nl = song.find('\n', start);
        if (nl == std::string_view::npos) {
            nl = song.size();
        }
        const auto line = song.substr(start, nl - start);
        if (!line.starts_with("---[")) {
            for (auto& name : chord_names(seventhslide::layout::parse_chord_line(line))) {
                out.push_back(std::move(name));
            }
        }
        start = nl + 1;
    }
    return out;
}

/// ``text`` with its whitespace gone: rows compared by the words on them.
[[nodiscard]] std::string words_only(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (!seventhslide::util::is_space(c)) {
            out.push_back(c);
        }
    }
    return out;
}

/// A slide's rows as words and chords, whichever box drew them.
struct PaintedRow {
    std::string words;
    std::vector<std::string> chords;
    [[nodiscard]] bool operator==(const PaintedRow&) const = default;
};

/// The text a row reads, for messages.
[[nodiscard]] std::string row_text(const seventhslide::scene::SceneTextLine& row)
{
    return seventhslide::model::plain_text(row.runs);
}

void audit_song_fit(const std::vector<Look>& audited,
                    seventhslide::layout::ITextMeasurer& measurer,
                    const seventhslide::layout::QtFontFaces* faces)
{
    const char* shots = faces != nullptr ? std::getenv("SEVENTHSLIDE_CHORD_AUDIT_DIR") : nullptr;
    int shot = 0;
    int slides = 0;
    int too_tall_words = 0;

    for (const auto& look : audited) {
        const auto theme = seventhslide::test::resolved_from_flat(flat_of(look));
        for (std::size_t s = 0; s < std::size(k_songs); ++s) {
            const std::string_view song = k_songs[s];
            INFO(look.family << " " << look.font_size << "px, box " << look.width << "x"
                             << look.height << ", chords " << look.show_chords << " "
                             << look.chord_size_pct << "%/" << look.chord_gap_pct << "%, line "
                             << look.line_height << ", follow " << look.follow_lines << " at "
                             << look.active_size << ", song " << s);
            const auto layout =
                seventhslide::build::compute_song_layout(theme, measurer, song, "v1 c1");

            // No chord lost, in parts or whole.
            std::vector<std::string> laid_chords;
            for (const auto& ln : layout.lines) {
                for (auto& name : chord_names(ln.chords)) {
                    laid_chords.push_back(std::move(name));
                }
                if (!ln.chord_text.empty()) {
                    CHECK(seventhslide::layout::strip_chords(ln.chord_text) == ln.text);
                }
            }
            if (look.show_chords) {
                CHECK(laid_chords == song_chords(song));
            }

            // Every line is shown, and is active on exactly one slide.
            std::vector<int> active(layout.lines.size(), 0);
            for (const auto& group : layout.groups) {
                for (std::size_t i = 0; i < group.indices.size(); ++i) {
                    if (static_cast<int>(i) < group.active_count) {
                        ++active[static_cast<std::size_t>(group.indices[i])];
                    }
                }
            }
            CHECK(std::ranges::all_of(active, [](int count) { return count == 1; }));

            for (std::size_t g = 0; g < layout.groups.size(); ++g) {
                const auto& group = layout.groups[g];
                if (group.is_title) {
                    continue;
                }
                auto text = seventhslide::scene::build_song_slide_text(
                    layout, theme, static_cast<int>(g), &measurer);
                seventhslide::scene::measure_text_content(text, measurer);
                ++slides;

                // What the grouper packed the slide with.
                double planned = 0.0;
                for (std::size_t i = 0; i < group.indices.size(); ++i) {
                    const auto& ln = layout.lines[static_cast<std::size_t>(group.indices[i])];
                    planned += static_cast<int>(i) < group.active_count ? ln.active_height_px
                                                                        : ln.base_height_px;
                    // Evening may take a row more than the line was planned at,
                    // where the slide has room for it.
                    if (ln.evened) {
                        planned = planned - ln.base_height_px + ln.extra_px
                            + static_cast<double>(ln.wrap_fragments.size()) * ln.line_height_px;
                    }
                    // A verse gap is only painted with a line above it.
                    if (i == 0) {
                        planned -= ln.extra_px;
                    }
                }
                double painted = 0.0;
                const auto chord_style = theme.song.chords;
                for (const auto& row : text.lines) {
                    painted += row.margin_top_px + row.metrics->height;
                    const double size =
                        seventhslide::canvas::painted_type(theme.song, row.type).size;
                    seventhslide::layout::FontStyle font{.family = look.family,
                                                         .size_px = size,
                                                         .bold = look.bold,
                                                         .letter_spacing = look.letter_spacing};
                    const auto chord_font = seventhslide::layout::chord_font(font, chord_style);
                    // Only one character or one chord name may be wider than the box.
                    bool uncuttable = false;
                    if (row.has_chords) {
                        const auto& items = row.chords.items;
                        uncuttable = items.size() == 1
                            && (seventhslide::util::utf8_length(items[0].cell.syllable) <= 1
                                || (items[0].cell.chord
                                    && measurer.measure_width(*items[0].cell.chord, chord_font)
                                           > theme.song.width));
                        // A chord that overhangs its word, alone in the row.
                        if (!uncuttable) {
                            for (const auto& item : items) {
                                uncuttable = uncuttable
                                    || (item.kind == ChordItemKind::Cell && item.cell.chord
                                        && measurer.measure_width(*item.cell.chord, chord_font)
                                               > theme.song.width);
                            }
                        }
                    } else {
                        uncuttable = seventhslide::util::utf8_length(row_text(row)) <= 1;
                    }
                    INFO("row: " << row_text(row));
                    CHECK((row.metrics->width <= theme.song.width + 0.01 || uncuttable));
                }
                INFO("slide " << g);
                CHECK(painted == planned);

                // A slide fits unless it is one row, or the one word on it is
                // cut across more rows than the box holds: no word is cut at a
                // slide, as every output steps through the same pieces.
                const auto& first = layout.lines[static_cast<std::size_t>(group.indices[0])];
                const bool one_word =
                    group.indices.size() == 1 && first.text.find(' ') == std::string::npos;
                if (text.lines.size() > 1 && !one_word) {
                    INFO("line: " << first.text);
                    CHECK(painted <= theme.song.height);
                }
                if (text.lines.size() > 1 && one_word && painted > theme.song.height) {
                    ++too_tall_words;
                }

                if (shots != nullptr && s == 1 && look.width == 700 && look.height == 900
                    && look.font_size == 60 && shot < 12) {
                    QImage image(look.width + 40, look.height + 40, QImage::Format_ARGB32);
                    image.fill(QColor(20, 24, 32));
                    QPainter painter(&image);
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setPen(QColor(90, 160, 255));
                    painter.drawRect(20, 20, look.width, look.height);
                    text.box.x = 20;
                    text.box.y = 20;
                    seventhslide::render::PaintContext ctx;
                    ctx.faces = faces;
                    seventhslide::render::paint_text_content(painter, text, ctx);
                    painter.end();
                    const auto path = std::filesystem::path(shots)
                        / ("chords_" + std::to_string(shot++) + ".png");
                    image.save(QString::fromStdString(path.string()));
                }
            }
        }
    }
    CHECK(slides > 0);
    if (faces != nullptr) {
        WARN(slides << " slides; a word alone taller than its box on " << too_tall_words);
    }
}

/// Every slide of ``built``'s output ``o``, saved in ``doc``, against song mode.
void check_converted_output(const seventhslide::build::SlideBuildResult& built,
                            const seventhslide::freeform::Document& doc,
                            std::size_t o,
                            seventhslide::layout::ITextMeasurer& measurer,
                            int& slides)
{
    const std::map<std::string, std::string> values;
    const auto& rt = built.outputs[o];
    const auto& deck = doc.decks[o];
    for (std::size_t g = 0; g < rt.layout.groups.size() && g < deck.slides.size(); ++g) {
        if (rt.layout.groups[g].is_title) {
            continue;
        }
        const auto& theme = rt.theme_for_slide(static_cast<int>(g));
        auto text = seventhslide::scene::build_song_slide_text(rt.layout, theme,
                                                               static_cast<int>(g), &measurer);
        seventhslide::scene::measure_text_content(text, measurer);
        double song_height = 0.0;
        std::vector<PaintedRow> song_rows;
        for (const auto& row : text.lines) {
            song_height += row.margin_top_px + row.metrics->height;
            song_rows.push_back({words_only(row_text(row)),
                                 row.has_chords ? chord_names(row.chords)
                                                : std::vector<std::string>{}});
        }
        for (const auto& obj : deck.slides[g].objects) {
            if (!obj.cue_linked) {
                continue;
            }
            const auto box = seventhslide::freeform::compile_text_object(
                doc, deck, static_cast<int>(g), obj, values, measurer);
            const auto laid = seventhslide::layout::lay_out_overlay(box, measurer);
            std::vector<PaintedRow> freeform_rows;
            for (const auto& row : laid.rows) {
                PaintedRow painted;
                for (const auto& run : row.runs) {
                    if (run.text != "-") {
                        painted.words += words_only(run.text);
                    }
                }
                for (const auto& chord : row.chords) {
                    painted.chords.push_back(chord.text);
                }
                freeform_rows.push_back(std::move(painted));
            }
            ++slides;
            const auto read_rows = [](const std::vector<PaintedRow>& rows) {
                std::string out;
                for (const auto& row : rows) {
                    out += row.words;
                    for (const auto& chord : row.chords) {
                        out += "[" + chord + "]";
                    }
                    out += " | ";
                }
                return out;
            };
            INFO("output " << o << ", slide " << g << ": song " << song_rows.size() << " rows "
                           << song_height << " px: " << read_rows(song_rows) << "\nfreeform "
                           << freeform_rows.size() << " rows " << laid.height
                           << " px: " << read_rows(freeform_rows));
            CHECK(song_rows == freeform_rows);
            CHECK(std::abs(laid.height - song_height) <= 1.0);
            // The same place on every row: as wide, on the same baseline.
            if (text.lines.size() == laid.rows.size()) {
                double y = 0.0;
                for (std::size_t r = 0; r < laid.rows.size(); ++r) {
                    const auto& song_row = text.lines[r];
                    y += song_row.margin_top_px;
                    INFO("row " << r << ": " << song_rows[r].words);
                    CHECK(std::abs(laid.rows[r].width - song_row.metrics->width) <= 1.0);
                    CHECK(std::abs(laid.rows[r].y - y) <= 1.0);
                    if (!laid.rows[r].runs.empty()) {
                        const auto& run = laid.rows[r].runs.front();
                        const double rise = text.box.font_size
                            * (std::max(0, run.style.baseline_rise_pct) / 100.0);
                        CHECK(std::abs(run.baseline + rise - song_row.metrics->baseline) <= 1.0);
                    }
                    y += song_row.metrics->height;
                }
            }
        }
    }
}

void audit_song_conversion(const std::vector<Look>& audited,
                           seventhslide::layout::ITextMeasurer& measurer)
{
    int slides = 0;
    for (const auto& look : audited) {
        for (std::size_t s = 0; s < std::size(k_songs); ++s) {
            INFO(look.family << " " << look.font_size << "px, box " << look.width << "x"
                             << look.height << ", follow " << look.follow_lines << " at "
                             << look.active_size << ", chords " << look.show_chords
                             << ", balance " << look.balance << ", verse gap " << look.verse_gap
                             << ", song " << s);
            seventhslide::build::SlideBuildInput in;
            in.kind = seventhslide::build::BuildKind::Song;
            in.selection.kind = seventhslide::model::ContentKind::Song;
            in.song.title = "Song";
            in.song.lyrics = k_songs[s];
            seventhslide::model::OutputConfig output;
            output.id = "out";
            output.name = "Out";
            output.canvas_width = 1920;
            output.canvas_height = 1080;
            seventhslide::test::set_look(output, flat_of(look));
            in.outputs.push_back(output);
            // A second screen with room to show lines whole, with chords and
            // without them.
            for (const bool second_chords : {false, true}) {
                if (second_chords && !look.show_chords) {
                    continue;
                }
                INFO("second screen chords " << second_chords);
                auto both = in;
                seventhslide::model::OutputConfig second = output;
                second.id = "second";
                second.name = "Second";
                second.song_themes.clear();
                auto second_look = look;
                second_look.show_chords = second_chords;
                second_look.width = 1800;
                second_look.height = 1000;
                seventhslide::test::set_look(second, flat_of(second_look));
                both.outputs.push_back(second);
                const auto built = seventhslide::build::build_slides(both, measurer);
                const auto doc = seventhslide::scene::document_from_build(
                    built, both.outputs, measurer, {.name = "Song"});
                for (std::size_t o = 0; o < built.outputs.size(); ++o) {
                    check_converted_output(built, doc, o, measurer, slides);
                }
            }
        }
    }
    CHECK(slides > 0);
}

}  // namespace

TEST_CASE("Songs paint as planned, inside their box, with every line and chord",
          "[song][chords]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    audit_song_fit(everyday_looks(), measurer, nullptr);
}

TEST_CASE("Songs paint as planned with real fonts in every look", "[.audit][song][chords]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    audit_song_fit(looks(), measurer, &measurer.faces());
}

TEST_CASE("A song saved as Freeform paints as it did in song mode",
          "[song][freeform][convert]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    audit_song_conversion(everyday_looks(), measurer);
}

TEST_CASE("A song saved as Freeform paints as it did, with real fonts in every look",
          "[.audit][song][freeform][convert]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    audit_song_conversion(looks(), measurer);
}
