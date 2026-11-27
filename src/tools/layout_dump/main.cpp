// seventhslide-layout-dump - writes the layout dump for a song fixture.
//
// Usage:
//   seventhslide-layout-dump <oracle-fixture.json> [-o out.json]
//   seventhslide-layout-dump --lyrics-file song.txt --verse-order "v1" [-o out.json]
//
// Reads an oracle fixture (or raw lyrics) and emits seventhslide.scene_dump JSON
// with ``oracle: "native"``. Uses ApproxTextMeasurer so CI does not depend on
// host fonts for short-line fixtures.

#include "seventhslide/devsupport/scene_dump.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::devsupport::SceneDumpInput;
using seventhslide::devsupport::build_song_scene_dump;
using seventhslide::devsupport::native_scene_dump_from_oracle_fixture;
using seventhslide::devsupport::validate_scene_document;
using seventhslide::model::OutputConfig;

namespace {

void usage(const char* argv0)
{
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " <oracle-fixture.json> [-o out.json]\n"
        << "  " << argv0
        << " --lyrics-file FILE [--verse-order ORDER] [--title TITLE] [-o out.json]\n";
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

json load_json_file(const std::string& path)
{
    return json::parse(read_file(path));
}

void write_json(const json& doc, const std::string& path)
{
    if (path.empty() || path == "-") {
        std::cout << doc.dump(2) << '\n';
        return;
    }
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write: " + path);
    }
    out << doc.dump(2) << '\n';
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        std::string fixture_path;
        std::string lyrics_file;
        std::string verse_order;
        std::string title = "Untitled";
        std::string out_path;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            }
            if (a == "-o" && i + 1 < argc) {
                out_path = argv[++i];
                continue;
            }
            if (a == "--lyrics-file" && i + 1 < argc) {
                lyrics_file = argv[++i];
                continue;
            }
            if (a == "--verse-order" && i + 1 < argc) {
                verse_order = argv[++i];
                continue;
            }
            if (a == "--title" && i + 1 < argc) {
                title = argv[++i];
                continue;
            }
            if (!a.empty() && a[0] != '-') {
                fixture_path = a;
                continue;
            }
            std::cerr << "unknown argument: " << a << '\n';
            usage(argv[0]);
            return 2;
        }

        ApproxTextMeasurer measurer;
        json doc;

        if (!lyrics_file.empty()) {
            SceneDumpInput in;
            in.title = title;
            in.lyrics = read_file(lyrics_file);
            in.verse_order = verse_order;
            OutputConfig oc;
            oc.name = "Main";
            const auto theme = seventhslide::theme::resolve_theme(oc, {});
            doc = build_song_scene_dump(
                in, oc.name, oc.canvas_width, oc.canvas_height, theme, measurer);
        } else if (!fixture_path.empty()) {
            const auto oracle = load_json_file(fixture_path);
            doc = native_scene_dump_from_oracle_fixture(oracle, measurer);
        } else {
            usage(argv[0]);
            return 2;
        }

        const auto errs = validate_scene_document(doc);
        if (!errs.empty()) {
            std::cerr << "invalid scene dump:\n";
            for (const auto& e : errs) {
                std::cerr << "  - " << e << '\n';
            }
            return 1;
        }

        write_json(doc, out_path);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
