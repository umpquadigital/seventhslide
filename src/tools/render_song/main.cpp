// seventhslide-render-song - paint one song on one OutputConfig to PNGs.
//
//   seventhslide-render-song <output.json> <song.json> <static-root> <out-dir>
//                           [--lines 0,1,2] [--all-first-of-slide]
//
// Uses QtTextMeasurer + BackgroundImageCache (real fonts / theme images).

#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/canvas/box.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/version.hpp>

#include <QGuiApplication>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] nlohmann::json load_json(const fs::path& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    nlohmann::json j;
    in >> j;
    return j;
}

[[nodiscard]] std::vector<int> parse_lines(const std::string& s)
{
    std::vector<int> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty()) {
            continue;
        }
        out.push_back(std::stoi(part));
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);

    if (argc < 5) {
        std::cerr
            << "Usage: " << argv[0]
            << " <output.json> <song.json> <static-root> <out-dir> "
               "[--lines 0,3,6] [--all-first-of-slide]\n"
            << "SeventhSlide " << seventhslide::version_string() << "\n";
        return 2;
    }

    const fs::path output_path = argv[1];
    const fs::path song_path = argv[2];
    const fs::path static_root = argv[3];
    const fs::path out_dir = argv[4];

    std::vector<int> line_indices;
    bool all_first_of_slide = false;
    for (int i = 5; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lines" && i + 1 < argc) {
            line_indices = parse_lines(argv[++i]);
        } else if (a == "--all-first-of-slide") {
            all_first_of_slide = true;
        }
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);

    const auto output_j = load_json(output_path);
    const auto song_j = load_json(song_path);

    auto oc = seventhslide::model::OutputConfig::from_dict(output_j);
    seventhslide::devsupport::install_flat_style(oc, output_j);

    // The shipped fonts, a folder per family (third_party/fonts).
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::fonts::FontIndex shipped;
    for (const auto& family : std::filesystem::directory_iterator(SEVENTHSLIDE_FONTS_DIR)) {
        if (!family.is_directory()) {
            continue;
        }
        auto& faces = shipped[seventhslide::util::to_lower(family.path().filename().string())];
        for (const auto& face : std::filesystem::directory_iterator(family.path())) {
            if (face.path().extension() == ".ttf" || face.path().extension() == ".otf") {
                const auto stem = face.path().stem().string();
                faces.push_back({stem.substr(stem.rfind('-') + 1), face.path()});
            }
        }
    }
    fonts.set_library_fonts(std::move(shipped));
    fonts.set_stand_in_family(std::string(seventhslide::canvas::k_default_font_family));
    seventhslide::layout::QtTextMeasurer measurer(fonts);

    seventhslide::session::Session session(measurer);
    session.set_outputs({std::move(oc)});

    auto live = seventhslide::model::Song::from_json(song_j);

    session.play_song(std::move(live));
    auto& shown = session.focused_stack().program;

    // Keep the token values play_song computed (copyright / CCLI); only the
    // transition key is ours to set. Replacing the whole struct dropped them.
    auto extras = shown.scene_extras();
    extras.transition_key = "song";
    shown.set_scene_extras(std::move(extras));

    seventhslide::render::BackgroundImageCache cache;
    cache.set_static_root(static_root);

    const auto& rt = shown.last_build().outputs.at(0);
    const int slide_n = rt.slide_count();

    if (line_indices.empty() && all_first_of_slide) {
        std::vector<bool> seen(static_cast<std::size_t>(std::max(1, slide_n)), false);
        for (int li = 0; li < shown.player().total_lines(); ++li) {
            if (li < 0 || li >= static_cast<int>(rt.line_to_slide.size())) {
                continue;
            }
            const int si = rt.line_to_slide[static_cast<std::size_t>(li)];
            if (si < 0 || si >= static_cast<int>(seen.size()) || seen[static_cast<std::size_t>(si)]) {
                continue;
            }
            seen[static_cast<std::size_t>(si)] = true;
            line_indices.push_back(li);
        }
    }
    if (line_indices.empty()) {
        line_indices = {0};
        if (shown.player().total_lines() > 1) {
            line_indices.push_back(std::min(4, shown.player().total_lines() - 1));
        }
        if (shown.player().total_lines() > 8) {
            line_indices.push_back(8);
        }
    }

    std::cout << "song=\"" << shown.last_build().player.all_lines.size()
              << " lines\" title from song.json; "
              << "player_lines=" << shown.player().total_lines()
              << " slides=" << slide_n << "\n";
    std::cout << "resolved bg_type=" << seventhslide::layout::to_string(rt.theme.background.type)
              << " bg_image=" << rt.theme.background.image << "\n";
    std::cout << "font=" << rt.theme.song.font_family << " size=" << rt.theme.song.font_size << "\n";

    int written = 0;
    for (int li : line_indices) {
        if (!shown.jump_to_line(li)) {
            std::cerr << "jump_to_line(" << li << ") failed\n";
            continue;
        }
        auto frames = session.scene_frames();
        if (frames.empty()) {
            std::cerr << "no frames at line " << li << "\n";
            continue;
        }
        auto& frame = frames.front();
        const int w = frame.canvas_width > 0 ? frame.canvas_width : 1920;
        const int h = frame.canvas_height > 0 ? frame.canvas_height : 1080;
        if (frame.background.type == seventhslide::layout::BackgroundType::Image) {
            cache.ensure(frame.background.image);
        }
        seventhslide::render::SceneResources res;
        res.cache = &cache;
        QImage img = seventhslide::render::render_scene_frame_image(frame, w, h,
                                                                    QColor(Qt::black), res);
        const auto out = out_dir / ("line_" + std::to_string(li) + ".png");
        if (!img.save(QString::fromStdString(out.string()), "PNG")) {
            std::cerr << "save failed: " << out << "\n";
            return 1;
        }
        std::cout << "wrote " << out << " bg=" << seventhslide::layout::to_string(frame.background.type);
        if (!frame.background.image.empty()) {
            std::cout << " img=" << frame.background.image;
        }
        if (frame.text) {
            std::cout << " text_lines=" << frame.text->lines.size()
                      << " font=" << frame.text->box.font_family
                      << " size=" << frame.text->box.font_size;
        }
        std::cout << "\n";
        ++written;
    }

    std::cout << "done: " << written << " PNG(s)\n";
    return written > 0 ? 0 : 1;
}
