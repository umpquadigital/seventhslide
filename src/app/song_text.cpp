#include "song_text.hpp"

#include "qvariant_json.hpp"

#include <seventhslide/layout/chords.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/theme/song_theme_pack.hpp>
#include <seventhslide/util/strings.hpp>

#include <QVariantMap>

#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {
namespace {

std::string lower_utf8(const QString& s)
{
    return s.toLower().toStdString();
}

[[nodiscard]] std::vector<std::string> verse_labels(const QVariantList& verses)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(verses.size()));
    for (const auto& v : verses) {
        out.push_back(v.toMap().value(QStringLiteral("label")).toString().toStdString());
    }
    return out;
}

}  // namespace

QStringList SongText::verseTypes()
{
    QStringList out;
    for (const auto& type : parse::k_verse_types) {
        if (type.code != 't') {
            out.push_back(QString::fromUtf8(type.name));
        }
    }
    return out;
}

QString SongText::defaultArrangementName()
{
    return QString::fromUtf8(theme::k_default_arrangement_name);
}

QString SongText::inheritAboveId()
{
    return QString::fromUtf8(theme::k_inherit_above);
}

QVariantList SongText::verses(const QString& lyrics)
{
    QVariantList out;
    for (const auto& v : parse::labelled_verses(lyrics.toStdString())) {
        out.push_back(QVariantMap{{QStringLiteral("label"), QString::fromUtf8(v.label)},
                                  {QStringLiteral("content"), QString::fromUtf8(v.content)}});
    }
    return out;
}

QString SongText::lyrics(const QVariantList& verses)
{
    std::vector<parse::LabelledVerse> list;
    list.reserve(static_cast<std::size_t>(verses.size()));
    for (const auto& v : verses) {
        const auto m = v.toMap();
        list.push_back(parse::LabelledVerse{m.value(QStringLiteral("label")).toString().toStdString(),
                                            m.value(QStringLiteral("content")).toString().toStdString()});
    }
    return QString::fromUtf8(parse::join_labelled_verses(list));
}

QString SongText::labelToCode(const QString& label)
{
    return QString::fromUtf8(parse::label_to_code(label.toStdString()));
}

QString SongText::baseVerseCode(const QString& code)
{
    const auto lower = lower_utf8(code);
    const auto base = parse::base_verse_code(lower);
    return base == lower ? code : QString::fromUtf8(base);
}

bool SongText::isTitleCode(const QString& code)
{
    return parse::is_title_code(lower_utf8(code));
}

bool SongText::isThemeCode(const QString& code)
{
    return parse::is_theme_code(lower_utf8(code));
}

QString SongText::verseDisplayName(const QString& label)
{
    return QString::fromStdString(parse::verse_display_name(label.toStdString()));
}

int SongText::verseTypeIndex(const QString& label)
{
    return static_cast<int>(verseTypes().indexOf(verseTypeName(label)));
}

QString SongText::verseTypeName(const QString& label)
{
    return QString::fromUtf8(parse::editor_verse_type(label.toStdString()));
}

int SongText::verseNumber(const QString& label)
{
    return parse::verse_number(label.toStdString());
}

QString SongText::makeVerseLabel(const QString& type, int number)
{
    return QString::fromStdString(parse::make_verse_label(type.toStdString(), number));
}

int SongText::findVerse(const QVariantList& verses, const QString& type, int number, int except)
{
    return parse::find_verse_label(verse_labels(verses), type.toStdString(), number, except);
}

int SongText::nextFreeVerseNumber(const QVariantList& verses,
                                  const QString& type,
                                  int preferred,
                                  int except)
{
    return parse::next_free_verse_number(verse_labels(verses), type.toStdString(), preferred,
                                         except);
}

QStringList SongText::orderTokens(const QString& order)
{
    QStringList out;
    for (const auto& token : parse::verse_order_tokens(order.toStdString())) {
        out.push_back(QString::fromStdString(token));
    }
    return out;
}

QString SongText::nextThemeCode(const QStringList& chips)
{
    std::set<int> taken;
    for (const auto& chip : chips) {
        const auto code = lower_utf8(chip);
        if (parse::is_theme_code(code)) {
            if (const auto n = util::parse_leading_int(std::string_view(code).substr(2))) {
                taken.insert(*n);
            }
        }
    }
    int n = 1;
    while (taken.contains(n)) {
        ++n;
    }
    return QStringLiteral("TH%1").arg(n);
}

bool SongText::firstLineHasChord(const QString& lyrics)
{
    const auto text = lyrics.toStdString();
    std::string first = text.substr(0, text.find('\n'));
    for (auto at = first.find(model::k_forced_split); at != std::string::npos;
         at = first.find(model::k_forced_split, at)) {
        first.erase(at, model::k_forced_split.size());
    }
    return layout::has_chords(first);
}

QStringList SongText::withTopic(const QStringList& topics, const QString& topic)
{
    std::vector<std::string> list;
    list.reserve(static_cast<std::size_t>(topics.size()) + 1);
    for (const auto& t : topics) {
        list.push_back(t.toStdString());
    }
    list.push_back(topic.toStdString());
    QStringList out;
    for (const auto& t : model::normalize_song_topics(list)) {
        out.push_back(QString::fromStdString(t));
    }
    return out;
}

QVariantMap SongText::packThemeMap(const QVariantList& arrangements, const QString& active)
{
    return to_qvariant(
               theme::pack_song_theme_map(to_json(arrangements), active.toStdString()).to_json())
        .toMap();
}

QVariantMap SongText::previousThemeMap(const QVariantMap& chips,
                                       const QStringList& order,
                                       int place)
{
    if (place < 0 || place >= order.size()) {
        return {};
    }
    const auto picks = theme::previous_chip_picks(theme::chip_maps_from_json(to_json(chips)),
                                                  order.join(QLatin1Char(' ')).toStdString(),
                                                  static_cast<std::size_t>(place));
    return to_qvariant(picks.to_json()).toMap();
}

}  // namespace seventhslide::app
