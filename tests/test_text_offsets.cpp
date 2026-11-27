// app::utf16_offset and app::utf8_offset: a place in text as core counts it
// and as QML does.

#include "text_offsets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QString>

using seventhslide::app::utf16_offset;
using seventhslide::app::utf8_offset;

namespace {

/// "a", then e-acute (2 bytes, 1 unit), a musical note (3 bytes, 1 unit), a
/// smile (4 bytes, a surrogate pair), "b".
const QString k_text = QStringLiteral("aé♪\U0001F600b");

}  // namespace

TEST_CASE("text offsets match at every character's edge", "[text_offsets]")
{
    const QByteArray utf8 = k_text.toUtf8();
    REQUIRE(utf8.size() == 11);
    REQUIRE(k_text.size() == 6);
    const int units[] = {0, 1, 2, 3, 5, 6};
    const std::size_t bytes[] = {0, 1, 3, 6, 10, 11};
    for (int i = 0; i < 6; ++i) {
        CHECK(utf8_offset(k_text, units[i]) == bytes[i]);
        CHECK(utf16_offset(utf8, bytes[i]) == units[i]);
    }
}

TEST_CASE("a UTF-16 offset inside a surrogate pair is the pair's start", "[text_offsets]")
{
    CHECK(utf8_offset(k_text, 4) == 6);
    CHECK(utf8_offset(QStringLiteral("\U0001F600"), 1) == 0);
}

TEST_CASE("a UTF-8 offset inside a character is the character's start", "[text_offsets]")
{
    const QByteArray utf8 = k_text.toUtf8();
    CHECK(utf16_offset(utf8, 2) == 1);  // inside the e-acute
    CHECK(utf16_offset(utf8, 4) == 2);  // inside the note
    CHECK(utf16_offset(utf8, 5) == 2);
    CHECK(utf16_offset(utf8, 7) == 3);  // inside the smile
    CHECK(utf16_offset(utf8, 8) == 3);
    CHECK(utf16_offset(utf8, 9) == 3);
}

TEST_CASE("text offsets before or past the text are its start or end", "[text_offsets]")
{
    const QByteArray utf8 = k_text.toUtf8();
    CHECK(utf8_offset(k_text, -1) == 0);
    CHECK(utf8_offset(k_text, -100) == 0);
    CHECK(utf8_offset(k_text, 7) == 11);
    CHECK(utf8_offset(k_text, 1000) == 11);
    CHECK(utf16_offset(utf8, 12) == 6);
    CHECK(utf16_offset(utf8, 1000) == 6);
    CHECK(utf8_offset(QString(), 3) == 0);
    CHECK(utf16_offset(QByteArray(), 3) == 0);
}
