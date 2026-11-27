// seventhslide-render-smoke - 1080p PNGs of the oracle song fixtures, painted natively.
//
//   seventhslide-render-smoke <oracle-dir> <out-dir>
//
// Reads fixtures/oracle/*.json (via manifest) and writes <id>_line0.png, the
// first line of each: what tests/test_render_smoke.cpp compares against.

#include "oracle_render.hpp"

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/version.hpp"

#include <QGuiApplication>

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <oracle-dir> <out-dir>\n"
                  << "SeventhSlide " << seventhslide::version_string() << "\n";
        return 2;
    }

    const fs::path oracle_dir = argv[1];
    const fs::path out_dir = argv[2];
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    seventhslide::layout::ApproxTextMeasurer measurer;
    const auto manifest =
        seventhslide::render_smoke::load_json(oracle_dir / "manifest.json");

    int written = 0;
    for (const auto& id_j : manifest.at("fixtures")) {
        const auto id = id_j.get<std::string>();
        const auto oracle =
            seventhslide::render_smoke::load_json(oracle_dir / (id + ".json"));
        const auto result =
            seventhslide::render_smoke::render_oracle_song_frame(oracle, measurer, 0, false);
        const fs::path out = out_dir / (id + "_line0.png");
        if (!result.image.save(QString::fromStdString(out.string()), "PNG")) {
            std::cerr << "failed to write " << out << "\n";
            return 1;
        }
        std::cout << "wrote " << out << " (" << result.image.width() << "x"
                  << result.image.height() << ")\n";
        ++written;
    }

    std::cout << "Render smoke: " << written << " PNG(s)\n";
    return 0;
}
