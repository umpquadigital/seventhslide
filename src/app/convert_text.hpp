#pragma once

// What the Convert dialog offers - the formats, sizes, frame rates and bit
// rates its menus list - and what it shows about a conversion before it runs
// - the new file's title, its size where that can be known, what the source
// is - from media::convert, the same functions the conversion names its file
// with. ``ConvertText`` in QML.
//
// A probe is the map ConvertController's probe reports: ``width``,
// ``height``, ``fps``, ``duration`` (seconds), ``size`` (bytes),
// ``hasVideo``, ``hasAudio``; a missing field is unknown. A choice is the
// map ``media::convert_choice_from_json`` reads, for ``kind`` ``video``,
// ``audio`` or ``image``.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace seventhslide::app {

class ConvertText : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The sizes the dialog offers as presets, largest first: each names the
    /// picture's shorter side (``1080`` is 1080p; see ``scaledSize``).
    Q_PROPERTY(QList<int> heights READ heights CONSTANT)
    /// The frame rates offered a video.
    Q_PROPERTY(QList<int> frameRates READ frameRates CONSTANT)
    /// The bit rates (kbit/s) offered a lossy sound.
    Q_PROPERTY(QList<int> audioBitrates READ audioBitrates CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] static QList<int> heights();
    [[nodiscard]] static QList<int> frameRates();
    [[nodiscard]] static QList<int> audioBitrates();

    /// The formats of ``kind`` (``video``, ``audio``, ``image``), in menu
    /// order (media::k_video_formats and the others), as ``[{value, label}]``: the format as a choice names it
    /// (``mp4``) and what it is called (``MP4``). Empty for another kind.
    Q_INVOKABLE [[nodiscard]] static QVariantList formats(const QString& kind);

    /// The title the converted file gets: the source's title and what
    /// changed (``Sunrise (720p, WebM).webm``), or with ``replaceOriginal``
    /// the source's title and the new extension (``Sunrise.webm``).
    /// ``sourceFilename`` is the library file, for the source's extension.
    /// Empty for a choice that does not read.
    Q_INVOKABLE [[nodiscard]] static QString newTitle(const QString& kind,
                                                      const QString& sourceTitle,
                                                      const QString& sourceFilename,
                                                      const QVariantMap& choice,
                                                      const QVariantMap& probe,
                                                      bool replaceOriginal);

    /// The converted file's size ("4.8 MB") where the choice fixes it;
    /// empty where it depends on the picture.
    Q_INVOKABLE [[nodiscard]] static QString estimatedSize(const QString& kind,
                                                           const QVariantMap& choice,
                                                           const QVariantMap& probe);

    /// What the source is, each part it has: ``1920 × 1080``, ``29.97 fps``,
    /// ``0:12``, ``4.5 MB``, joined with `` · ``.
    Q_INVOKABLE [[nodiscard]] static QString summary(const QVariantMap& probe);

    /// The file's codecs, as people call them, and its sound's channels,
    /// each on its own: ``["H.264", "AAC", "Stereo"]``. The container is
    /// left out, as ``filename`` shows it; so is an image's codec.
    Q_INVOKABLE [[nodiscard]] static QStringList formatTags(const QString& filename,
                                                            const QVariantMap& probe);
    /// What a file of ``kind`` is, as ``[{label, value}]`` in the order the
    /// dialog shows them: a video's Picture, Frame rate, Length, Sound and
    /// File size; a sound's Length, Sound and File size; an image's Picture
    /// and File size. Each one the probe knows, each one short.
    Q_INVOKABLE [[nodiscard]] static QVariantList details(const QString& kind,
                                                          const QVariantMap& probe);

    /// The size preset ``chosen`` makes of a source the probe read, as
    /// ``"1280 × 720"`` (media::scaled_size: a preset names the shorter
    /// side); empty when it keeps its own size or the size is unknown.
    Q_INVOKABLE [[nodiscard]] static QString scaledSize(int chosen, const QVariantMap& probe);

    /// What a source of ``kind`` becomes with ``choice``: ``video``,
    /// ``audio`` (a video made into sound, too) or ``image``; empty for a
    /// choice that does not read.
    Q_INVOKABLE [[nodiscard]] static QString outputKind(const QString& kind,
                                                        const QVariantMap& choice);
    /// The extension the new file gets (``.mp4``); empty for a choice that
    /// does not read.
    Q_INVOKABLE [[nodiscard]] static QString extension(const QString& kind,
                                                       const QVariantMap& choice);
    /// The picture codecs video format ``format`` holds, by codec_key, in
    /// menu order (``auto`` and ``copy`` first).
    Q_INVOKABLE [[nodiscard]] static QStringList videoCodecs(const QString& format);
    /// The sound codecs ``format`` holds - a video format, or a sound one.
    Q_INVOKABLE [[nodiscard]] static QStringList audioCodecs(const QString& format);
};

} // namespace seventhslide::app
