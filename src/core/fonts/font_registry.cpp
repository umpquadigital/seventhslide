#include <seventhslide/fonts/font_registry.hpp>

#include "seventhslide/util/strings.hpp"

#include <algorithm>

namespace seventhslide::fonts {

std::pair<bool, bool> classify_subfamily(std::string_view sub)
{
    const auto s = util::to_lower(sub);
    const bool is_bold = util::contains_ci(s, "bold") || util::contains_ci(s, "black")
        || util::contains_ci(s, "heavy");
    const bool is_italic =
        util::contains_ci(s, "italic") || util::contains_ci(s, "oblique");
    return {is_bold, is_italic};
}

const FontFaceEntry* index_match(const FontIndex& index,
                                 std::string_view family,
                                 bool want_bold,
                                 bool want_italic)
{
    const auto it = index.find(util::to_lower(family));
    if (it == index.end() || it->second.empty()) {
        return nullptr;
    }
    const FontFaceEntry* best = nullptr;
    int best_score = -1;
    for (const auto& face : it->second) {
        const auto [is_bold, is_italic] = classify_subfamily(face.subfamily);
        int score = (is_bold == want_bold ? 2 : 0) + (is_italic == want_italic ? 2 : 0);
        auto s = util::to_lower(face.subfamily);
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        bool canonical = false;
        if (!want_bold && !want_italic) {
            canonical = s == "regular" || s == "book" || s == "roman" || s == "normal"
                || s.empty();
        } else if (want_bold && !want_italic) {
            canonical = s == "bold";
        } else if (!want_bold && want_italic) {
            canonical = s == "italic" || s == "oblique";
        } else {
            canonical = s == "bolditalic" || s == "boldoblique";
        }
        if (canonical) {
            score += 1;
        }
        if (score > best_score) {
            best_score = score;
            best = &face;
        }
    }
    return best;
}

void FontRegistry::set_library_fonts(FontIndex fonts)
{
    const std::lock_guard lock(mu_);
    library_ = std::move(fonts);
    ++library_revision_;
}

std::uint64_t FontRegistry::library_revision() const
{
    const std::lock_guard lock(mu_);
    return library_revision_;
}

FontIndex FontRegistry::library_fonts() const
{
    const std::lock_guard lock(mu_);
    return library_;
}

void FontRegistry::set_shipped_fonts(FontIndex fonts)
{
    const std::lock_guard lock(mu_);
    shipped_ = std::move(fonts);
}

void FontRegistry::set_stand_in_family(std::string family)
{
    const std::lock_guard lock(mu_);
    stand_in_family_ = std::move(family);
}

void FontRegistry::set_listener(Listener listener)
{
    const std::lock_guard lock(mu_);
    listener_ = std::move(listener);
}

std::optional<FontMatch> FontRegistry::stand_in_unlocked(bool bold, bool italic) const
{
    for (const auto* index : {&shipped_, &library_}) {
        if (const auto* face = index_match(*index, stand_in_family_, bold, italic)) {
            return FontMatch{face->path, FontSource::StandIn, face->variable};
        }
    }
    return std::nullopt;
}

std::optional<FontMatch> FontRegistry::find(std::string_view family, bool bold, bool italic) const
{
    const std::lock_guard lock(mu_);
    if (const auto* face = index_match(library_, family, bold, italic)) {
        return FontMatch{face->path, FontSource::Library, face->variable};
    }
    // No family at all is the stand-in's to show, and nothing to tell anyone.
    if (!family.empty() && listener_) {
        listener_(std::string(family), library_revision_);
    }
    return stand_in_unlocked(bold, italic);
}

std::optional<FontMatch> FontRegistry::stand_in_for(std::string_view family, bool bold,
                                                    bool italic) const
{
    const std::lock_guard lock(mu_);
    if (listener_) {
        listener_(std::string(family), library_revision_);
    }
    return stand_in_unlocked(bold, italic);
}

}  // namespace seventhslide::fonts
