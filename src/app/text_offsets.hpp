#pragma once

// A place in text as core counts it (UTF-8 bytes) and as QML does (UTF-16
// units).

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cstddef>

namespace seventhslide::app {

/// Where byte ``at`` of ``utf8`` falls in its UTF-16 text; the end for one
/// past it. A byte inside a character is the character's start.
[[nodiscard]] inline qsizetype utf16_offset(const QByteArray& utf8, std::size_t at)
{
    auto n = static_cast<qsizetype>(std::min<std::size_t>(at, static_cast<std::size_t>(utf8.size())));
    // Back past the continuation bytes (10xxxxxx), at most three, to the lead.
    for (int i = 0; i < 3 && n > 0 && n < utf8.size() && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80;
         ++i) {
        --n;
    }
    return QString::fromUtf8(utf8.left(n)).size();
}

/// Where UTF-16 unit ``at`` of ``text`` falls in its UTF-8 bytes; the start or
/// the end for one before or past them. A unit between the halves of a
/// surrogate pair is the pair's start.
[[nodiscard]] inline std::size_t utf8_offset(const QString& text, int at)
{
    int n = std::clamp(at, 0, static_cast<int>(text.size()));
    if (n > 0 && n < text.size() && text[n - 1].isHighSurrogate() && text[n].isLowSurrogate()) {
        --n;
    }
    return static_cast<std::size_t>(text.left(n).toUtf8().size());
}

}  // namespace seventhslide::app
