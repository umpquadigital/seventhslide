#include "seventhslide/scene/theme_draft_preview.hpp"

#include "seventhslide/build/bible_layout.hpp"
#include "seventhslide/build/song_layout.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/scene/chrome_text.hpp"
#include "seventhslide/scene/scene_builder.hpp"
#include "seventhslide/scene/text_metrics.hpp"
#include "seventhslide/theme/resolved_theme.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::scene {
namespace {


// Ending of We Have This Hope - enough lines that small boxes naturally clip.
constexpr std::string_view k_sample_song_lyrics =
    "---[Verse:1]---\n"
    "[G]Soon the heavens will [C]open [G]wide,\n"
    "Christ will [D]come to claim His [G]bride.\n"
    "All the [C]universe will [G]sing,\n"
    "Halle[Em]lujah! [D]Christ is [G]King!\n"
    "We have this hope, this faith and God's great love.\n"
    "We are united in Christ.\n";

// Revelation 14:6-7, New King James Version. Two verses, so a theme that runs
// verses on together previews that way.
constexpr int k_sample_bible_chapter = 14;
constexpr int k_sample_bible_first_verse = 6;
constexpr std::string_view k_sample_bible_verses[] = {
    "Then I saw another angel flying in the midst of heaven, having the "
    "everlasting gospel to preach to those who dwell on the earth— to every "
    "nation, tribe, tongue, and people—",
    "saying with a loud voice, “Fear God and give glory to Him, for the hour of "
    "His judgment has come; and worship Him who made heaven and earth, the sea "
    "and springs of water.”",
};

constexpr std::string_view k_sample_bible_ref = "Revelation 14:6-7";

/// What a picture element with no picture yet says on the designer stage.
constexpr std::string_view k_picture_stand_in = "Pick an image";

/// The style keys that belong to a theme's surface rather than its text.
constexpr const char* k_surface_keys[] = {
    "background_type",
    "background_color",
    "background_image",
    "background_video",
};

void append_plain_line(SceneTextContent& content,
                       std::string_view text,
                       SceneLineRole role = SceneLineRole::Normal,
                       const canvas::LineType& type = {})
{
    SceneTextLine line;
    line.runs = model::parse_inline_markup(text);
    line.role = role;
    line.type = type;
    content.lines.push_back(std::move(line));
}

/// The lyric lines of the sample song, without its section header or chords.
[[nodiscard]] std::vector<std::string> sample_song_lines()
{
    std::vector<std::string> lines;
    std::string_view rest = k_sample_song_lyrics;
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        const auto line = rest.substr(0, nl);
        if (!line.empty() && !line.starts_with("---[")) {
            lines.push_back(layout::lyric_without_chords(line));
        }
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
    }
    return lines;
}

/// Without a measurer there is no layout: the sample lines, one row each.
/// Following shows the first line live and the next two following it.
[[nodiscard]] SceneTextContent sample_song_text_unwrapped(const theme::ResolvedTheme& theme,
                                                          bool follow_demo)
{
    SceneTextContent content;
    content.box = song_box(theme);
    content.z = theme.song.z;
    content.chords = theme.song.chords;
    const auto lines = sample_song_lines();
    if (!(follow_demo || theme.behavior.follow_lines > 0)) {
        for (const auto& line : lines) {
            append_plain_line(content, line);
        }
        return content;
    }
    constexpr std::size_t k_follow_demo_lines = 3;
    for (std::size_t i = 0; i < std::min(k_follow_demo_lines, lines.size()); ++i) {
        if (i == 0) {
            append_plain_line(content, lines[i], SceneLineRole::Active, theme.song.active_line);
        } else {
            append_plain_line(content, lines[i], SceneLineRole::Follow, theme.song.other_lines);
        }
    }
    return content;
}

/// The sample song's first content slide, laid out and painted the way the
/// audience frame would be.
[[nodiscard]] SceneTextContent sample_song_text_laid_out(theme::ResolvedTheme theme,
                                                         bool follow_demo,
                                                         layout::ITextMeasurer& measurer)
{
    // The behavior demo shows follow lines even when the theme has none.
    if (follow_demo && theme.behavior.follow_lines <= 0) {
        theme.behavior.follow_lines = 1;
    }

    const auto layout =
        build::compute_song_layout(theme, measurer, k_sample_song_lyrics, "v1");

    // First content slide (skip a title slide if present).
    std::size_t group_i = 0;
    while (group_i < layout.groups.size() && layout.groups[group_i].is_title) {
        ++group_i;
    }
    if (group_i >= layout.groups.size()) {
        group_i = 0;
    }
    return build_song_slide_text(layout, theme, static_cast<int>(group_i), &measurer);
}

[[nodiscard]] SceneTextContent sample_bible_text_unwrapped(const theme::ResolvedTheme& theme)
{
    SceneTextContent content;
    content.box = bible_box(theme);
    content.z = theme.bible.z;
    append_plain_line(content, k_sample_bible_verses[0]);
    return content;
}

[[nodiscard]] SceneTextContent sample_bible_text_laid_out(const theme::ResolvedTheme& theme,
                                                          layout::ITextMeasurer& measurer)
{
    const std::vector<const theme::ResolvedTheme*> outs{&theme};
    std::vector<build::BibleVerseInput> verses;
    for (const auto verse : k_sample_bible_verses) {
        verses.push_back(build::BibleVerseInput{
            .verse_num = k_sample_bible_first_verse + static_cast<int>(verses.size()),
            .text = std::string(verse)});
    }
    const auto plan = build::plan_bible_chunks(k_sample_bible_chapter, verses, outs, measurer);
    const auto slides = build::build_bible_slides_for_output(theme, plan, measurer);
    // Same rows the audience frame gets - the editor must not show a fit the
    // output does not have.
    const auto& first = slides.slides.front();
    return build_bible_text_content(first.text, first.citations, theme, &measurer);
}

}  // namespace

std::optional<DraftContext> draft_context_from_string(std::string_view context) noexcept
{
    if (context == "song") {
        return DraftContext::Song;
    }
    if (context == "bible") {
        return DraftContext::Bible;
    }
    if (context == "image") {
        return DraftContext::Image;
    }
    if (context == "video") {
        return DraftContext::Video;
    }
    if (context == "web") {
        return DraftContext::Web;
    }
    if (context == "live") {
        return DraftContext::Live;
    }
    if (context == "behavior") {
        return DraftContext::Behavior;
    }
    if (context == "titles") {
        return DraftContext::Titles;
    }
    if (context == "bg") {
        return DraftContext::Background;
    }
    return std::nullopt;
}

nlohmann::json isolate_text_preview_style(nlohmann::json text_style)
{
    if (!text_style.is_object()) {
        text_style = nlohmann::json::object();
    }
    for (const char* key : k_surface_keys) {
        text_style.erase(key);
    }
    text_style["background_type"] = "transparent";
    return text_style;
}

namespace {

/// Token values the designer stage substitutes into themed text elements -
/// the same fixture song / passage the sample body comes from, so the copyright
/// and reference boxes preview with real-looking content.
[[nodiscard]] std::map<std::string, std::string> sample_token_pool()
{
    model::Song song;
    song.title = "We Have This Hope";
    song.show_copyright = true;
    song.authors = {"Wayne Hooper"};
    song.copyright = "© 1962";
    song.ccli_song_number = "7201044";

    model::BiblePassage passage;
    passage.ref = std::string(k_sample_bible_ref);
    passage.book = "Revelation";
    passage.chapter = k_sample_bible_chapter;
    passage.verse_start = k_sample_bible_first_verse;
    passage.verse_end =
        k_sample_bible_first_verse + static_cast<int>(std::size(k_sample_bible_verses)) - 1;
    passage.version = "NKJV";

    SceneExtras extras;
    extras.copyright_vars = copyright_token_values(song, "123456");
    extras.bible_ref_vars = bible_ref_token_values(passage);
    return frame_token_pool(extras);
}

}  // namespace

std::optional<theme::ElementContext> element_context_of(DraftContext context) noexcept
{
    switch (context) {
    case DraftContext::Song:
    case DraftContext::Behavior:
        return theme::ElementContext::Song;
    case DraftContext::Bible:
        return theme::ElementContext::Bible;
    case DraftContext::Image:
        return theme::ElementContext::Image;
    case DraftContext::Video:
        return theme::ElementContext::Video;
    case DraftContext::Web:
        return theme::ElementContext::Web;
    case DraftContext::Live:
        return theme::ElementContext::Live;
    case DraftContext::Titles:
        return theme::ElementContext::Titles;
    case DraftContext::Background:
        break;
    }
    return std::nullopt;
}

namespace {

/// The draft frame, its text not yet measured (see build_theme_draft_frame).
[[nodiscard]] SceneFrame assemble_draft_frame(const nlohmann::json& style,
                                              DraftContext context,
                                              int canvas_width,
                                              int canvas_height,
                                              std::string_view output_name,
                                              layout::ITextMeasurer* measurer,
                                              bool designer_stage,
                                              std::string_view suppress_id)
{
    SceneFrame frame;
    frame.output_name = std::string(output_name);
    const auto canvas = geom::canvas_or_reference(canvas_width, canvas_height);
    frame.canvas_width = canvas.width;
    frame.canvas_height = canvas.height;
    frame.slide_index = 0;
    frame.is_blank = false;
    frame.is_frozen = false;

    // ``Behavior`` is the song design with the wrap / follow controls in front
    // of it, so it previews the song elements.
    const auto elements_ctx = element_context_of(context);
    // The draft, resolved the way a live build resolves a theme: elements fitted
    // to this canvas, body boxes and behaviour flags read once.
    nlohmann::json draft = style.is_object() ? style : nlohmann::json::object();
    const theme::ResolvedTheme resolved = context == DraftContext::Background
                                              ? theme::resolve_styles({}, draft, canvas)
                                              : theme::resolve_styles(draft, draft, canvas);
    frame.background = background_of(resolved);

    // The raster is the real painter, so the designer sees what the audience
    // will, every element at its own place in the stack: the sample body and
    // the stand-ins too. QML adds only selection outlines, handles, and the
    // caret - plus the one box it is typing into, which is left out of the
    // raster here.
    //
    // Tokens are the exception, and only on the designer stage: there a box
    // paints the ``{token}`` text it was authored with, because the box is
    // being edited and its source is what the designer needs to see. Filling
    // them would make the box say one thing while you look at it and another
    // the moment you click into it. A gallery thumbnail is not being edited, so
    // it keeps the sample fill and previews the finished result.
    //
    // The designer also previews every conditioned element as shown - the
    // theme is being designed, not played, and there is no song to ask.
    const auto append_elements = [&]() {
        if (!elements_ctx) {
            return;
        }
        theme::ElementConditions conditions;
        conditions.song_copyright = true;
        // A picture not chosen yet shows where it is on the stage, the one
        // place it can be picked; a card shows what will paint, which is none.
        const auto picture_stand_in =
            designer_stage ? std::optional(PictureStandIn{k_picture_stand_in, canvas})
                           : std::nullopt;
        auto objects = build_theme_element_objects(
            resolved.elements.at(*elements_ctx), 0, 1,
            designer_stage ? std::map<std::string, std::string>{} : sample_token_pool(),
            std::vector<IndicatorItem>{{"V1", false}, {"C1", true}}, false, suppress_id, conditions,
            picture_stand_in);
        frame.objects.insert(frame.objects.end(), std::make_move_iterator(objects.begin()),
                             std::make_move_iterator(objects.end()));
    };

    switch (context) {
    case DraftContext::Titles:
        // Playback routes title boxes through the overlay because layout
        // consults them; here there is no layout, so they paint as objects like
        // everything else and the designer can select them the same way.
        frame.mode = SceneMode::Empty;
        break;
    case DraftContext::Background:
        // Surface only. The clock is a text-theme element, so a bg preview never
        // paints one - the Freeform editor uses this image as its backdrop under
        // the document's own Clock objects.
        frame.mode = SceneMode::Empty;
        break;
    case DraftContext::Bible:
        frame.mode = SceneMode::Bible;
        if (resolved.bible.shown) {
            frame.text = measurer != nullptr ? sample_bible_text_laid_out(resolved, *measurer)
                                             : sample_bible_text_unwrapped(resolved);
        }
        break;
    case DraftContext::Image:
    case DraftContext::Video:
    case DraftContext::Web:
    case DraftContext::Live: {
        // A media frame like the one that plays, placed by the same code, so
        // its area paints at its own place among the elements. What plays
        // there is only known at showtime: a labelled stand-in takes its
        // place, on the plate the output paints.
        std::string_view label;
        if (context == DraftContext::Image) {
            frame.mode = SceneMode::Image;
            frame.image.emplace();
            label = "Picture";
        } else if (context == DraftContext::Video) {
            frame.mode = SceneMode::Video;
            frame.video.emplace();
            label = "Video";
        } else if (context == DraftContext::Web) {
            frame.mode = SceneMode::Web;
            frame.web.emplace();
            label = "Web page";
        } else {
            frame.mode = SceneMode::Live;
            frame.live.emplace();
            label = "Live video";
        }
        place_frame_media(frame, resolved);
        const auto& area = frame.image   ? frame.image->area
                           : frame.video ? frame.video->area
                           : frame.web   ? frame.web->area
                                         : frame.live->area;
        if (area.enabled) {
            const auto placed = area.on_canvas(canvas.width, canvas.height);
            SceneMediaStandIn stand_in;
            // A picture or video shows as colour bars, the area's size and
            // shape at a glance; a page or live source as the plain plate.
            stand_in.bars = frame.image.has_value() || frame.video.has_value();
            stand_in.label = stand_in_label(
                label, {.x = static_cast<double>(placed.x), .y = static_cast<double>(placed.y),
                        .w = static_cast<double>(placed.width),
                        .h = static_cast<double>(placed.height)},
                canvas);
            if (stand_in.bars) {
                // White, outlined in black, to read on every bar.
                stand_in.label.font_color = "#ffffff";
                stand_in.label.effects.text_outline = {
                    .color = "#000000",
                    .width = std::max(1.0, stand_in.label.font_size / 12.0)};
            }
            frame.media_stand_in = std::move(stand_in);
        }
        break;
    }
    case DraftContext::Song:
    case DraftContext::Behavior: {
        frame.mode = SceneMode::Song;
        const bool follow_demo = context == DraftContext::Behavior;
        // ``Behavior`` is a settings demo, not a proxy for a live slide: its wrap /
        // follow controls need the sample body even when the song context hides it.
        if (follow_demo || resolved.song.shown) {
            frame.text = measurer != nullptr
                             ? sample_song_text_laid_out(resolved, follow_demo, *measurer)
                             : sample_song_text_unwrapped(resolved, follow_demo);
        }
        break;
    }
    }
    append_elements();
    return frame;
}

}  // namespace

SceneFrame build_theme_draft_frame(const nlohmann::json& style,
                                   DraftContext context,
                                   int canvas_width,
                                   int canvas_height,
                                   std::string_view output_name,
                                   layout::ITextMeasurer* measurer,
                                   bool designer_stage,
                                   std::string_view suppress_id)
{
    auto frame = assemble_draft_frame(style, context, canvas_width, canvas_height, output_name,
                                      measurer, designer_stage, suppress_id);
    // Measured once, like a live frame: the designer repaints this raster on
    // every drag tick.
    if (measurer != nullptr) {
        lay_out_frame_text(frame, *measurer);
    }
    return frame;
}

}  // namespace seventhslide::scene
