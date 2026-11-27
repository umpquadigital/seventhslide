#include "seventhslide/theme/theme_kind.hpp"

#include "seventhslide/model/theme_keys.hpp"

namespace seventhslide::theme {
namespace {

constexpr ElementContext k_song_contexts[] = {ElementContext::Song, ElementContext::Titles};
constexpr ElementContext k_bible_contexts[] = {ElementContext::Bible};
constexpr ElementContext k_media_contexts[] = {
    ElementContext::Image,
    ElementContext::Video,
    ElementContext::Web,
    ElementContext::Live,
};

}  // namespace

std::string_view to_string(ThemeKind kind) noexcept
{
    switch (kind) {
    case ThemeKind::Song:
        return "song";
    case ThemeKind::Bible:
        return "bible";
    case ThemeKind::Media:
        return "media";
    case ThemeKind::Bg:
        return "bg";
    }
    return {};
}

std::optional<ThemeKind> parse_theme_kind(std::string_view raw) noexcept
{
    for (const auto kind : k_theme_kinds) {
        if (raw == to_string(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

std::span<const ElementContext> contexts_of(ThemeKind kind) noexcept
{
    switch (kind) {
    case ThemeKind::Song:
        return k_song_contexts;
    case ThemeKind::Bible:
        return k_bible_contexts;
    case ThemeKind::Media:
        return k_media_contexts;
    case ThemeKind::Bg:
        break;
    }
    return {};
}

ThemeKind kind_of(ElementContext ctx) noexcept
{
    switch (ctx) {
    case ElementContext::Song:
    case ElementContext::Titles:
        return ThemeKind::Song;
    case ElementContext::Bible:
        return ThemeKind::Bible;
    case ElementContext::Image:
    case ElementContext::Video:
    case ElementContext::Web:
    case ElementContext::Live:
        return ThemeKind::Media;
    }
    return ThemeKind::Song;
}

ElementContext primary_context(ThemeKind kind) noexcept
{
    const auto contexts = contexts_of(kind);
    return contexts.empty() ? ElementContext::Song : contexts.front();
}

bool is_theme_key(ThemeKind kind, std::string_view key) noexcept
{
    switch (kind) {
    case ThemeKind::Song:
        return model::key_in(key, model::k_song_theme_keys);
    case ThemeKind::Bible:
        return model::key_in(key, model::k_bible_theme_keys);
    case ThemeKind::Media:
        return model::key_in(key, model::k_media_theme_keys);
    case ThemeKind::Bg:
        return model::is_bg_theme_key(key);
    }
    return false;
}

}  // namespace seventhslide::theme
