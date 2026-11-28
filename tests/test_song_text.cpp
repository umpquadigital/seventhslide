// The song editor's content rules come from C++ (SongText), the same ones a
// save and the slides go by, not from JavaScript copies of them.

#include <catch2/catch_test_macros.hpp>

#include "song_text.hpp"

#include <seventhslide/model/content.hpp>

#include <QString>
#include <QStringList>
#include <QVariantMap>

using seventhslide::app::SongText;

TEST_CASE("SongText: the next theme chip is the lowest number free", "[app][songtext]")
{
    CHECK(SongText::nextThemeCode({}) == QStringLiteral("TH1"));
    CHECK(SongText::nextThemeCode({QStringLiteral("V1"), QStringLiteral("TH1"), QStringLiteral("C1")})
          == QStringLiteral("TH2"));
    // Any case; a gap is filled first; a title chip (T1) or a verse is not one.
    CHECK(SongText::nextThemeCode({QStringLiteral("th1"), QStringLiteral("TH3"), QStringLiteral("T2")})
          == QStringLiteral("TH2"));
    CHECK(SongText::nextThemeCode({QStringLiteral("THX"), QStringLiteral("TH")}) == QStringLiteral("TH1"));
}

TEST_CASE("SongText: a chord on the first line, and a forced break that is not one", "[app][songtext]")
{
    CHECK(SongText::firstLineHasChord(QStringLiteral("[G]Amazing grace\nhow sweet")));
    CHECK_FALSE(SongText::firstLineHasChord(QStringLiteral("Amazing grace\n[G]how sweet")));
    CHECK_FALSE(SongText::firstLineHasChord(QStringLiteral("[---][---]Amazing grace")));
    CHECK(SongText::firstLineHasChord(QStringLiteral("[---][---]Amazing [D]grace")));
    CHECK_FALSE(SongText::firstLineHasChord(QString()));
}

TEST_CASE("SongText: a topic is added as a save would store it", "[app][songtext]")
{
    const QStringList topics{QStringLiteral("Grace"), QStringLiteral("Easter")};
    CHECK(SongText::withTopic(topics, QStringLiteral("  Holy   Week "))
          == QStringList{QStringLiteral("Grace"), QStringLiteral("Easter"), QStringLiteral("Holy Week")});
    // Already there, in another case: nothing changes.
    CHECK(SongText::withTopic(topics, QStringLiteral("grace")) == topics);
    CHECK(SongText::withTopic(topics, QStringLiteral("   ")) == topics);

    // No more than a song may have.
    QStringList full;
    for (std::size_t i = 0; i < seventhslide::model::k_max_song_topics; ++i) {
        full.push_back(QStringLiteral("Topic %1").arg(i));
    }
    CHECK(SongText::withTopic(full, QStringLiteral("One more")) == full);
}

TEST_CASE("SongText: a theme change's Previous is what the change before it played with",
          "[app][songtext]")
{
    const auto song_pick = [](QVariantMap pick) {
        return QVariantMap{{QStringLiteral("out-main"), QVariantMap{{QStringLiteral("song"), pick}}}};
    };
    const QVariantMap chips{
        {QStringLiteral("TH1"),
         song_pick({{QStringLiteral("theme"), QStringLiteral("t-a")},
                    {QStringLiteral("bg"), QStringLiteral("b-a")}})},
        {QStringLiteral("TH2"), song_pick({{QStringLiteral("theme"), QStringLiteral("t-b")}})},
        {QStringLiteral("TH3"), song_pick({{QStringLiteral("bg"), SongText::inheritAboveId()}})},
    };
    const QStringList order{QStringLiteral("TH1"), QStringLiteral("V1"), QStringLiteral("TH2"),
                            QStringLiteral("C1"),  QStringLiteral("TH3"), QStringLiteral("V2"),
                            QStringLiteral("TH1"), QStringLiteral("C1")};

    CHECK(SongText::previousThemeMap(chips, order, 0).isEmpty());
    CHECK(SongText::previousThemeMap(chips, order, 2)
          == song_pick({{QStringLiteral("theme"), QStringLiteral("t-a")},
                        {QStringLiteral("bg"), QStringLiteral("b-a")}}));
    CHECK(SongText::previousThemeMap(chips, order, 4)
          == song_pick({{QStringLiteral("theme"), QStringLiteral("t-b")},
                        {QStringLiteral("bg"), QStringLiteral("b-a")}}));
    // TH1 again, after TH3's Above: where a chip plays decides its Previous.
    CHECK(SongText::previousThemeMap(chips, order, 6)
          == song_pick({{QStringLiteral("theme"), QStringLiteral("t-b")}}));
    // A place outside the order has no change before it.
    CHECK(SongText::previousThemeMap(chips, order, -1).isEmpty());
    CHECK(SongText::previousThemeMap(chips, order, 8).isEmpty());
}
