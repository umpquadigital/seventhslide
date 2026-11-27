// The render half of the deck check (tools/deck_check.py): a verification
// tool, not an importer. Each <deck>/document.json, a Freeform
// document the script wrote from a PowerPoint deck, is played and every slide
// rendered, fully built, to <deck>/out/s-NNN.png through the output preview, so
// the real painters and the real font and image lookups do the drawing.
//
//     SEVENTHSLIDE_DECK_DIR=dir ./seventhslide_tests_dev "[.deck-check]"
//
// ``dir`` holds a folder per deck (document.json, images/, videos/) and
// fonts/fonts.json, the families the decks name as the script found them on
// the machine that made the references: they are handed to the registry as a
// library's fonts, so type measures as it does there. SEVENTHSLIDE_DECK_ONLY
// names one deck folder.

#include "session_facade_fixture.hpp"

#include <QFile>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

using seventhslide::fonts::FontFaceEntry;
using seventhslide::fonts::FontIndex;
namespace fs = std::filesystem;

namespace {

/// fonts.json as the registry's index.
FontIndex deck_fonts(const fs::path& root)
{
    FontIndex index;
    const auto manifest = root / "fonts" / "fonts.json";
    std::ifstream in(manifest);
    if (!in) {
        return index;
    }
    const auto families = nlohmann::json::parse(in);
    for (const auto& family : families) {
        auto name = family.value("family", std::string{});
        std::string lower;
        for (const unsigned char c : name) {
            lower.push_back(static_cast<char>(std::tolower(c)));
        }
        auto& faces = index[lower];
        for (const auto& face : family["faces"]) {
            faces.push_back(FontFaceEntry{.subfamily = face.value("subfamily", "Regular"),
                                          .path = root / face.value("file", std::string{}),
                                          .variable = face.value("variable", false)});
        }
    }
    return index;
}

void render_deck(const fs::path& folder, const FontIndex& fonts)
{
    std::ifstream in(folder / "document.json");
    REQUIRE(in.good());
    auto document = nlohmann::json::parse(in);
    auto& deck = document["decks"][0];
    const int width = deck["canvas"].value("width", 1920);
    const int height = deck["canvas"].value("height", 1080);
    const auto slide_count = static_cast<int>(deck["slides"].size());

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.previews().fonts().set_library_fonts(fonts);
    facade.set_media_root(folder);
    OutputConfig output = seventhslide::test::facade::make_main();
    output.canvas_width = width;
    output.canvas_height = height;
    facade.set_outputs({output});

    const auto output_id = facade.session().primary_output()->id;
    deck["output"] = output_id;
    seventhslide::model::FreeformItem item;
    item.name = folder.filename().string();
    item.document = document;
    const auto id = facade.freeform_repo()->create_item(item);
    facade.refresh_library();
    REQUIRE(facade.live()->playFreeform(static_cast<qint64>(id)));

    fs::create_directories(folder / "out");
    for (int i = 0; i < slide_count; ++i) {
        const QImage image = facade.outputPreviews()->render_output_preview(
            QString::fromStdString(output_id), QSize(width, height), i);
        REQUIRE_FALSE(image.isNull());
        char name[32];
        std::snprintf(name, sizeof name, "s-%03d.png", i + 1);
        CHECK(image.save(QString::fromStdString((folder / "out" / name).string())));
    }
}

}  // namespace

TEST_CASE("Deck check renders the converted decks", "[.deck-check]")
{
    const char* dir = std::getenv("SEVENTHSLIDE_DECK_DIR");
    if (dir == nullptr) {
        SKIP("SEVENTHSLIDE_DECK_DIR is not set");
    }
    seventhslide::test::ensure_qt_application();
    const fs::path root{dir};
    const FontIndex fonts = deck_fonts(root);
    const char* only = std::getenv("SEVENTHSLIDE_DECK_ONLY");
    int rendered = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!fs::exists(entry.path() / "document.json")) {
            continue;
        }
        if (only != nullptr && entry.path().filename() != only) {
            continue;
        }
        INFO(entry.path().string());
        render_deck(entry.path(), fonts);
        ++rendered;
    }
    CHECK(rendered > 0);
}
