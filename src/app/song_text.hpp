#pragma once

// The song text rules the song editor works by - how lyrics split into verses,
// what code a label plays as - from parse::, the same functions that build the
// slides. ``SongText`` in QML.

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace seventhslide::app {

class SongText : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Verse types a verse can be given, in menu order (Title is a chip, not
    /// a verse type).
    Q_PROPERTY(QStringList verseTypes READ verseTypes CONSTANT)
    /// What an arrangement the operator has not named is stored as
    /// (theme::k_default_arrangement_name). Shown as qsTr("Default").
    Q_PROPERTY(QString defaultArrangementName READ defaultArrangementName CONSTANT)
    /// What a theme change stores for a theme it leaves to the theme priority
    /// ("Above", theme::k_inherit_above). An unset one is "Previous".
    Q_PROPERTY(QString inheritAboveId READ inheritAboveId CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] static QStringList verseTypes();
    [[nodiscard]] static QString defaultArrangementName();
    [[nodiscard]] static QString inheritAboveId();

    /// ``lyrics`` as ``[{label, content}]`` (parse::labelled_verses).
    Q_INVOKABLE [[nodiscard]] static QVariantList verses(const QString& lyrics);
    /// ``[{label, content}]`` back as lyrics (parse::join_labelled_verses).
    Q_INVOKABLE [[nodiscard]] static QString lyrics(const QVariantList& verses);
    /// ``Verse:1a`` -> ``v1``; unknown labels -> ``misc``.
    Q_INVOKABLE [[nodiscard]] static QString labelToCode(const QString& label);
    /// ``v1a`` / ``V1A`` -> ``v1``; any other code comes back unchanged.
    Q_INVOKABLE [[nodiscard]] static QString baseVerseCode(const QString& code);
    /// ``T1`` / ``t1a``: a title chip. Case does not matter.
    Q_INVOKABLE [[nodiscard]] static bool isTitleCode(const QString& code);
    /// ``TH1``: a theme chip. Case does not matter.
    Q_INVOKABLE [[nodiscard]] static bool isThemeCode(const QString& code);

    // The editor's verse labels (``Verse:2``; parse::verse_display_name and
    // the functions after it). ``verses`` is ``[{label, content}]``.

    /// ``Verse:2a`` -> ``Verse 2``.
    Q_INVOKABLE [[nodiscard]] static QString verseDisplayName(const QString& label);
    /// Where the type ``label`` names sits in ``verseTypes``; 0 (Verse) for none.
    Q_INVOKABLE [[nodiscard]] static int verseTypeIndex(const QString& label);
    /// That type's name.
    Q_INVOKABLE [[nodiscard]] static QString verseTypeName(const QString& label);
    /// ``label``'s verse number, 1..99.
    Q_INVOKABLE [[nodiscard]] static int verseNumber(const QString& label);
    /// ``Type:N``, the number clamped to 1..99.
    Q_INVOKABLE [[nodiscard]] static QString makeVerseLabel(const QString& type, int number);
    /// The verse that is ``type`` ``number``, ``except`` skipped; -1 for none.
    Q_INVOKABLE [[nodiscard]] static int findVerse(const QVariantList& verses,
                                                   const QString& type,
                                                   int number,
                                                   int except = -1);
    /// The first ``type`` number from ``preferred`` up that no other verse
    /// uses; -1 when all 99 are taken.
    Q_INVOKABLE [[nodiscard]] static int nextFreeVerseNumber(const QVariantList& verses,
                                                             const QString& type,
                                                             int preferred,
                                                             int except = -1);
    /// The codes of a typed verse order, upper-cased: ``"v1 c1"`` -> ``V1 C1``.
    Q_INVOKABLE [[nodiscard]] static QStringList orderTokens(const QString& order);
    /// The theme chip a play order (``chips``) does not have yet: ``TH`` and
    /// the lowest number none of its theme chips uses.
    Q_INVOKABLE [[nodiscard]] static QString nextThemeCode(const QStringList& chips);
    /// Whether the first line of ``lyrics`` has a chord (layout::has_chords;
    /// a forced break, ``[---][---]``, is not one).
    Q_INVOKABLE [[nodiscard]] static bool firstLineHasChord(const QString& lyrics);
    /// ``topics`` with ``topic`` added, as a save stores them
    /// (model::normalize_song_topics): tidied, a repeat in any case left out,
    /// no more than a song may have.
    Q_INVOKABLE [[nodiscard]] static QStringList withTopic(const QStringList& topics,
                                                           const QString& topic);

    /// The theme map the editor's ``arrangements`` store, ``active`` selected
    /// (theme::pack_song_theme_map): ``{}`` for a song with no themes of its own.
    Q_INVOKABLE [[nodiscard]] static QVariantMap packThemeMap(const QVariantList& arrangements,
                                                              const QString& active);
    /// What the theme change at ``place`` in the play order ``order`` takes
    /// for Previous ({outputId: {category: {theme, bg}}}): what the change
    /// before it played with (theme::previous_chip_picks). ``chips`` is the
    /// editor's ``{TH1: map}``. ``{}`` when no change comes before it, or
    /// ``place`` is not in the order.
    Q_INVOKABLE [[nodiscard]] static QVariantMap previousThemeMap(const QVariantMap& chips,
                                                                  const QStringList& order,
                                                                  int place);
};

}  // namespace seventhslide::app
