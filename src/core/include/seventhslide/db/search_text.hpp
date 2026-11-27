#pragma once

// How every library search matches (songs, services, images, videos, audio,
// Freeform, web pages, live sources, the web remote's library): case, accents
// and punctuation aside, so "eglise" finds "Église" and "twas grace sweet
// sounding" finds "'Twas Grace, Sweet-Sounding". A search the fold leaves
// empty ("#") still finds the text as typed, case aside. And how lists of
// names sort: case aside, numbers by value.
//
// Seam: QtCore (Unicode normalization and collation).

#include <QCollator>
#include <QString>

#include <string_view>

namespace seventhslide::db {

/// ``text`` as a search compares it: lower case, accents and punctuation
/// gone, dashes, slashes and runs of space one space.
[[nodiscard]] QString search_fold(const QString& text);

/// A search box's text, trimmed and folded once.
class SearchQuery {
public:
    explicit SearchQuery(const QString& text);
    explicit SearchQuery(std::string_view text);

    /// Nothing typed: everything matches.
    [[nodiscard]] bool empty() const noexcept { return typed_.isEmpty(); }
    [[nodiscard]] bool matches(const QString& text) const;
    [[nodiscard]] bool matches(std::string_view text) const;
    /// ``text`` whose fold (``search_fold``) its list keeps already.
    [[nodiscard]] bool matches(const QString& text, const QString& folded) const;

    [[nodiscard]] const QString& typed() const noexcept { return typed_; }
    [[nodiscard]] const QString& folded() const noexcept { return folded_; }

private:
    QString typed_;
    QString folded_;
};

/// Names as people read them: case aside, numbers by value ("Hymn 2" before
/// "Hymn 10").
[[nodiscard]] const QCollator& name_collator();

}  // namespace seventhslide::db
