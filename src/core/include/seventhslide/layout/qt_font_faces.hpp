#pragma once

// Shared Qt font-face cache for measure + paint.
// Resolves files via FontRegistry, then builds a QFont that actually selects
// that file's family + style/weight (or Qt's matcher / engine synthesis when
// the file cannot satisfy the request). Never double-draws to fake bold.

#include "seventhslide/fonts/font_registry.hpp"
#include "seventhslide/layout/text_measurer.hpp"

#include <QFont>
#include <QString>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace seventhslide::layout {

/**
 * Resolved face for one FontStyle.
 *
 * ``font`` is ready to measure and paint: pixel size, weight, and italic are
 * already applied. ``face_is_bold`` / ``face_is_italic`` describe the *file*
 * FontRegistry returned (a Regular file used as a synthesis base is not bold).
 */
struct ResolvedQtFont {
    QFont font;
    bool request_bold = false;
    bool request_italic = false;
    bool face_is_bold = false;
    bool face_is_italic = false;
    bool resolved = false;  // false -> family string only / approx
    /// In-place outline thickness when no heavier face exists (0 = none).
    double synthetic_embolden_px = 0;

    [[nodiscard]] bool needs_synthetic_embolden() const noexcept
    {
        return synthetic_embolden_px > 0.0;
    }
};

/// ``font``'s vertical metrics. A face that reports no cap height counts its
/// ascent as one, so the ink never starts above it.
[[nodiscard]] FontMetrics vertical_metrics(const QFont& font);

/**
 * Process-wide-friendly face cache keyed by family + bold/italic (+ path).
 *
 * Pixel size is applied per call so one cache entry serves all sizes.
 */
class QtFontFaces {
public:
    explicit QtFontFaces(fonts::FontRegistry& fonts);
    /// Lets go of what it loaded: Qt's fonts outlive this, and one left behind
    /// could answer for its name and style long after its file has gone.
    ~QtFontFaces();

    [[nodiscard]] ResolvedQtFont resolve(const FontStyle& style) const;

    /// The family Qt knows ``family`` by, with its faces loaded, for what sets a
    /// font by name itself rather than through ``resolve``: the stand-in's, when
    /// that is what slides show for it.
    [[nodiscard]] std::string painted_family(std::string_view family) const;

    /// One face to show on its own, loaded into Qt until
    /// ``QFontDatabase::removeApplicationFont(qt_font_id)``. Tells no one and
    /// keeps nothing here, so a family shown on its own is never said to be
    /// missing.
    struct LoadedFile {
        int qt_font_id = -1;
        QFont font;
        /// False when the file is not there or is refused.
        bool loaded = false;
    };
    [[nodiscard]] static LoadedFile load_file(const fonts::FontMatch& match);

    /// Appends everything ``resolve`` reads from ``style`` to ``key``. Anything
    /// that caches a result derived from the resolved font (measured widths)
    /// keys on this, so a field added to the font cannot be missed there.
    static void append_font_key(std::string& key, const FontStyle& style);

    void clear_cache();

private:
    struct FaceEntry {
        std::string path;
        int qt_font_id = -1;
        std::string qt_family;
        std::string style_name;
        int weight = static_cast<int>(QFont::Normal);
        bool face_is_bold = false;
        bool face_is_italic = false;
        bool have_file = false;
        /// The file holds heavier weights Qt can select within it.
        bool variable = false;
    };

    fonts::FontRegistry& fonts_;
    /// Guards the three tables below; every member that reads them holds it.
    mutable std::mutex mu_;
    mutable std::unordered_map<std::string, FaceEntry> faces_;
    /// Each file loaded, by path, so one file several styles or families
    /// resolve to is added to Qt once.
    mutable std::unordered_map<std::string, FaceEntry> files_;
    /// Finished fonts per full style. Reusing the QFont also keeps its loaded
    /// font engine, so repeat measures skip Qt's font matching.
    mutable std::unordered_map<std::string, ResolvedQtFont> resolved_;

    [[nodiscard]] static std::string face_key(const FontStyle& style);
    /// ``mu_`` held.
    [[nodiscard]] FaceEntry& face_for(const FontStyle& style) const;
    /// ``match``'s file loaded, the first time only. ``mu_`` held.
    [[nodiscard]] const FaceEntry& file_face(const fonts::FontMatch& match) const;
    /// Load ``match``'s file into Qt; a face without ``have_file`` if it cannot be.
    [[nodiscard]] static FaceEntry load_face(const fonts::FontMatch& match);
    [[nodiscard]] ResolvedQtFont resolve_uncached(const FontStyle& style) const;
    static void apply_file_face(QFont& font, const FaceEntry& face);
};

}  // namespace seventhslide::layout
