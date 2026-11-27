#pragma once

// The media kinds as QML names them: ``MediaKind.Image``, ``MediaKind.Video``,
// ``MediaKind.Audio``. MediaController's entry points take one, so a
// misspelt kind is a qmllint error rather than a refusal at run time. The
// enum is an int underneath, so any number QML passes is a value of it and
// to_db can refuse the rest.

#include <seventhslide/db/media_kind.hpp>

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace seventhslide::app::MediaKindQml {
Q_NAMESPACE
QML_NAMED_ELEMENT(MediaKind)

enum Kind : int { Image, Video, Audio };
Q_ENUM_NS(Kind)

/// ``kind`` as the library's kind; nullopt for a value outside the enum.
[[nodiscard]] constexpr std::optional<db::MediaKind> to_db(Kind kind) noexcept
{
    switch (kind) {
    case Image:
        return db::MediaKind::Image;
    case Video:
        return db::MediaKind::Video;
    case Audio:
        return db::MediaKind::Audio;
    }
    return std::nullopt;
}

/// The library's ``kind`` as QML names it.
[[nodiscard]] constexpr Kind from_db(db::MediaKind kind) noexcept
{
    switch (kind) {
    case db::MediaKind::Video:
        return Video;
    case db::MediaKind::Audio:
        return Audio;
    case db::MediaKind::Image:
        break;
    }
    return Image;
}

} // namespace seventhslide::app::MediaKindQml
