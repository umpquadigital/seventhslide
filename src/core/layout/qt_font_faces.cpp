#include "seventhslide/layout/qt_font_faces.hpp"

#include "seventhslide/fonts/font_file.hpp"
#include "seventhslide/util/read_file.hpp"
#include "seventhslide/util/strings.hpp"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRawFont>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::layout {
namespace {

[[nodiscard]] QFont::Weight clamp_weight(int weight) noexcept
{
    if (weight < static_cast<int>(QFont::Thin)) {
        return QFont::Normal;
    }
    if (weight > static_cast<int>(QFont::Black)) {
        return QFont::Black;
    }
    return static_cast<QFont::Weight>(weight);
}

/// True when the SFNT style name is a Regular/Normal token (possibly with an
/// optical-size prefix like "08 Regular"). Pinning that name onto the
/// typographic family ("Open Sans") would throw away SemiBold/Light files
/// whose name table lists subfamily Regular under a styled family name.
[[nodiscard]] bool is_regular_style_name(std::string_view style_name)
{
    auto s = util::to_lower(style_name);
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s.empty() || s == "regular" || s == "normal" || s == "book" || s == "roman"
        || s.ends_with("regular") || s.ends_with("normal");
}

[[nodiscard]] double probe_advance(const QFont& font)
{
    return QFontMetricsF(font).horizontalAdvance(QLatin1String("Hamburg"));
}

/**
 * Re-select the face Qt matched, without asking for a heavy *weight*.
 *
 * Qt's FreeType engine emboldens glyphs itself whenever the request asks for
 * weight >= Bold and the file it opened is not flagged bold - which a variable
 * font's Bold *named instance* never is, so a real bold gets a second, fake
 * bold on top. That synthesis only runs on the cached-glyph path (rendered
 * size <= 64px), so left to it the same text is heavier in the operator
 * preview than on the audience screen, and heavier in a small theme size than
 * in a large one. Pinning the matched style name at normal weight selects the
 * same face - verified by advance - with nothing left to trigger the synthesis.
 */
[[nodiscard]] QFont face_without_engine_embolden(const QFont& matched, double matched_advance)
{
    const QString style_name = QFontInfo(matched).styleName();
    if (style_name.isEmpty() || matched_advance <= 0.0) {
        return matched;
    }
    QFont styled = matched;
    styled.setStyleName(style_name);
    styled.setWeight(QFont::Normal);
    const double styled_advance = probe_advance(styled);
    if (std::abs(styled_advance - matched_advance) > 0.01 * matched_advance) {
        return matched;  // style name alone did not reach the same face
    }
    return styled;
}

}  // namespace

FontMetrics vertical_metrics(const QFont& font)
{
    const QFontMetricsF metrics(font);
    const double cap = metrics.capHeight();
    return {metrics.ascent(), metrics.descent(), metrics.height(),
            cap > 0.0 ? std::min(cap, metrics.ascent()) : metrics.ascent()};
}

QtFontFaces::QtFontFaces(fonts::FontRegistry& fonts)
    : fonts_(fonts)
{
}

std::string QtFontFaces::face_key(const FontStyle& style)
{
    std::string key;
    key.reserve(style.family.size() + 3);
    key += style.family;
    key += '\0';
    key += style.bold ? '1' : '0';
    key += style.italic ? '1' : '0';
    return key;
}

QtFontFaces::FaceEntry QtFontFaces::load_face(const fonts::FontMatch& match)
{
    FaceEntry face;
    face.path = match.path.string();
    face.variable = match.variable;
    const QString qpath = QString::fromStdString(face.path);
    if (!QFile::exists(qpath)) {
        return face;
    }
    // A library's file may have come by sync from anyone, and the font engine is
    // not the place to find out what it holds. What the program ships is known.
    if (!qpath.startsWith(QLatin1Char(':'))) {
        const auto bytes = util::read_file(match.path);
        if (!bytes || !fonts::read_font_file(*bytes).refusal.empty()) {
            return face;
        }
    }
    if (QGuiApplication::instance() != nullptr) {
        const int id = QFontDatabase::addApplicationFont(qpath);
        if (id < 0) {
            return face;
        }
        face.qt_font_id = id;
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty()) {
            face.qt_family = families.first().toStdString();
        }
        const QRawFont raw(qpath, 12.0);
        if (raw.isValid()) {
            if (face.qt_family.empty()) {
                face.qt_family = raw.familyName().toStdString();
            }
            face.style_name = raw.styleName().toStdString();
            face.weight = raw.weight();
            face.face_is_italic = raw.style() != QFont::StyleNormal;
        }
    }
    if (face.weight <= 0) {
        face.weight = static_cast<int>(QFont::Normal);
    }
    face.face_is_bold = face.weight >= static_cast<int>(QFont::DemiBold);
    face.have_file = true;
    return face;
}

const QtFontFaces::FaceEntry& QtFontFaces::file_face(const fonts::FontMatch& match) const
{
    auto path = match.path.string();
    if (auto it = files_.find(path); it != files_.end()) {
        return it->second;
    }
    auto [it, _] = files_.emplace(std::move(path), load_face(match));
    return it->second;
}

QtFontFaces::FaceEntry& QtFontFaces::face_for(const FontStyle& style) const
{
    const auto key = face_key(style);
    if (auto it = faces_.find(key); it != faces_.end()) {
        return it->second;
    }

    FaceEntry face;
    if (const auto match = fonts_.find(style.family, style.bold, style.italic)) {
        face = file_face(*match);
        if (!face.have_file && match->source != fonts::FontSource::StandIn) {
            if (const auto stand_in = fonts_.stand_in_for(style.family, style.bold, style.italic)) {
                face = file_face(*stand_in);
            }
        }
    }

    auto [it, _] = faces_.emplace(key, std::move(face));
    return it->second;
}

std::string QtFontFaces::painted_family(std::string_view family) const
{
    const std::lock_guard lock(mu_);
    FontStyle style;
    style.family = std::string(family);
    std::string name;
    // A view may ask for any of them by weight and slant, so all go in.
    for (const bool bold : {true, false}) {
        for (const bool italic : {true, false}) {
            style.bold = bold;
            style.italic = italic;
            name = face_for(style).qt_family;
        }
    }
    return name.empty() ? std::string(family) : name;
}

QtFontFaces::LoadedFile QtFontFaces::load_file(const fonts::FontMatch& match)
{
    const auto face = load_face(match);
    LoadedFile out;
    out.qt_font_id = face.qt_font_id;
    if (face.have_file && !face.qt_family.empty()) {
        out.font = QFont(QString::fromStdString(face.qt_family));
        apply_file_face(out.font, face);
        out.loaded = true;
    }
    return out;
}

void QtFontFaces::apply_file_face(QFont& font, const FaceEntry& face) 
{
    const bool regular_name = is_regular_style_name(face.style_name);
    const bool weight_is_regular =
        face.weight < static_cast<int>(QFont::Medium);

    // Pin a distinctive style ("Bold", "SemiBold", "08 Regular") so Qt uses
    // this file. Do not pin a generic Regular when the file's OS/2 weight is
    // heavier - that combination maps "Open Sans SemiBold.ttf" onto Open Sans
    // Regular.
    if (!face.style_name.empty() && (!regular_name || weight_is_regular)) {
        font.setStyleName(QString::fromStdString(face.style_name));
    } else {
        font.setStyleName(QString());
    }
    font.setWeight(clamp_weight(face.weight));
    font.setItalic(face.face_is_italic);
}

void QtFontFaces::append_font_key(std::string& key, const FontStyle& style)
{
    key += face_key(style);
    const auto size_bits = std::bit_cast<std::uint64_t>(style.size_px);
    const auto spacing_bits = std::bit_cast<std::uint64_t>(style.letter_spacing);
    key.append(reinterpret_cast<const char*>(&size_bits), sizeof size_bits);
    key.append(reinterpret_cast<const char*>(&spacing_bits), sizeof spacing_bits);
}

ResolvedQtFont QtFontFaces::resolve(const FontStyle& style) const
{
    // The result depends only on the fields keyed here and the face table.
    std::string key;
    append_font_key(key, style);

    std::lock_guard lock(mu_);
    if (const auto it = resolved_.find(key); it != resolved_.end()) {
        return it->second;
    }
    ResolvedQtFont out = resolve_uncached(style);
    resolved_.emplace(std::move(key), out);
    return out;
}

ResolvedQtFont QtFontFaces::resolve_uncached(const FontStyle& style) const
{
    const FaceEntry& face = face_for(style);

    ResolvedQtFont out;
    out.request_bold = style.bold;
    out.request_italic = style.italic;
    out.face_is_bold = face.face_is_bold;
    out.face_is_italic = face.face_is_italic;

    const int px = std::max(1, static_cast<int>(std::lround(style.size_px)));
    if (!face.qt_family.empty()) {
        out.font = QFont(QString::fromStdString(face.qt_family));
        out.resolved = true;
    } else {
        // Only with no fonts at all: every one, and the stand-in, unusable.
        out.font = QFont(QString::fromStdString(style.family));
        out.resolved = false;
    }
    out.font.setPixelSize(px);
    out.font.setStyleStrategy(QFont::PreferAntialias);
    const double tracking = clamp_letter_spacing_px(style.letter_spacing);
    if (tracking != 0.0) {
        // Leave Qt's default (PercentageSpacing 0) when unused. AbsoluteSpacing
        // 0 is not the same - it disables kerning and looks like extra tracking,
        // especially on large title fonts.
        out.font.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    }

    if (face.have_file) {
        apply_file_face(out.font, face);
    } else {
        out.font.setWeight(style.bold ? QFont::Bold : QFont::Normal);
        out.font.setItalic(style.italic);
        return out;
    }

    if (style.italic && !face.face_is_italic) {
        out.font.setItalic(true);
    }

    const bool need_heavier =
        style.bold && face.weight < static_cast<int>(QFont::Bold);
    if (!need_heavier) {
        return out;
    }

    // Stay on the face asked for and thicken it in place. Asking Qt for a
    // heavier one of the family's name would find the system's, if it has it.
    if (!face.variable) {
        out.synthetic_embolden_px = std::max(0.25, style.size_px * 0.03);
        return out;
    }

    // A heavier weight inside this file (variable wght axis). Qt can report
    // success while still returning the Regular - compare advances before
    // trusting it.
    QFont matched = out.font;
    matched.setStyleName(QString());
    matched.setWeight(QFont::Bold);
    matched.setItalic(style.italic);
    matched.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        QFont::PreferAntialias | QFont::NoFontMerging));

    const double pinned_w = probe_advance(out.font);
    const double matched_w = probe_advance(matched);
    if (pinned_w > 0.0 && matched_w > pinned_w * 1.025) {
        out.font = face_without_engine_embolden(matched, matched_w);
        if (tracking != 0.0) {
            out.font.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
        }
        out.face_is_bold = true;
        return out;
    }

    // Qt will not synthesize; a shifted double-draw ghosts.
    out.synthetic_embolden_px = std::max(0.25, style.size_px * 0.03);
    return out;
}

QtFontFaces::~QtFontFaces()
{
    if (QGuiApplication::instance() != nullptr) {
        clear_cache();
    }
}

void QtFontFaces::clear_cache()
{
    std::lock_guard lock(mu_);
    resolved_.clear();
    faces_.clear();
    for (const auto& [_, face] : files_) {
        if (face.qt_font_id >= 0) {
            QFontDatabase::removeApplicationFont(face.qt_font_id);
        }
    }
    files_.clear();
}

}  // namespace seventhslide::layout
