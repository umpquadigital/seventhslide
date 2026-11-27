// What the Convert dialog offers and shows; see convert_text.hpp.

#include "convert_text.hpp"

#include "qvariant_json.hpp"

#include <seventhslide/media/convert.hpp>

#include <QStringList>

#include <array>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <variant>

namespace seventhslide::app {

namespace {

media::MediaProbe probe_from_map(const QVariantMap& map)
{
    media::MediaProbe probe;
    probe.width = map.value(QStringLiteral("width")).toInt();
    probe.height = map.value(QStringLiteral("height")).toInt();
    probe.fps = map.value(QStringLiteral("fps")).toDouble();
    probe.duration_s = map.value(QStringLiteral("duration")).toDouble();
    probe.size_bytes = map.value(QStringLiteral("size")).toLongLong();
    probe.has_video = map.value(QStringLiteral("hasVideo")).toBool();
    probe.has_audio = map.value(QStringLiteral("hasAudio")).toBool();
    return probe;
}

std::optional<media::ConvertChoice> choice_from_map(const QString& kind, const QVariantMap& map)
{
    return media::convert_choice_from_json(kind.toStdString(), to_json(map));
}

template <typename Format, std::size_t N>
QVariantList format_options(const std::array<Format, N>& formats)
{
    QVariantList out;
    for (const auto format : formats) {
        // A choice names a format by its extension without the dot.
        out.push_back(QVariantMap{
            {QStringLiteral("value"), QString::fromUtf8(media::extension_of(format).substr(1))},
            {QStringLiteral("label"), QString::fromUtf8(media::format_label(format))}});
    }
    return out;
}

} // namespace

QList<int> ConvertText::heights()
{
    return {2160, 1440, 1080, 720, 480};
}

QList<int> ConvertText::frameRates()
{
    return {24, 25, 30, 50, 60};
}

QList<int> ConvertText::audioBitrates()
{
    return {64, 96, 128, 160, 192, 256, 320};
}

QVariantList ConvertText::formats(const QString& kind)
{
    if (kind == QLatin1String("video")) {
        return format_options(media::k_video_formats);
    }
    if (kind == QLatin1String("audio")) {
        return format_options(media::k_audio_formats);
    }
    if (kind == QLatin1String("image")) {
        return format_options(media::k_image_formats);
    }
    return {};
}

QString ConvertText::newTitle(const QString& kind,
                              const QString& sourceTitle,
                              const QString& sourceFilename,
                              const QVariantMap& choice,
                              const QVariantMap& probe,
                              bool replaceOriginal)
{
    const auto parsed = choice_from_map(kind, choice);
    if (!parsed) {
        return {};
    }
    const std::string title = sourceTitle.toStdString();
    if (replaceOriginal) {
        return QString::fromStdString(media::replacement_title(title, *parsed));
    }
    const std::string extension =
        std::filesystem::path(sourceFilename.toStdString()).extension().string();
    return QString::fromStdString(
        media::converted_title(title, *parsed, probe_from_map(probe), extension));
}

QString ConvertText::estimatedSize(const QString& kind,
                                   const QVariantMap& choice,
                                   const QVariantMap& probe)
{
    const auto parsed = choice_from_map(kind, choice);
    if (!parsed) {
        return {};
    }
    const auto bytes = media::estimated_bytes(*parsed, probe_from_map(probe));
    return bytes ? QString::fromStdString(media::format_file_size(*bytes)) : QString{};
}

QString ConvertText::summary(const QVariantMap& probe)
{
    const media::MediaProbe p = probe_from_map(probe);
    QStringList parts;
    if (p.width > 0 && p.height > 0) {
        parts << QStringLiteral("%1 × %2").arg(p.width).arg(p.height);
    }
    // A still image has a nominal rate too; only a film's is worth saying.
    if (p.fps > 0.0 && p.duration_s > 0.0 && p.has_video) {
        parts << tr("%1 fps").arg(QString::number(p.fps, 'g', 4));
    }
    if (const auto length = media::format_duration(p.duration_s); !length.empty()) {
        parts << QString::fromStdString(length);
    }
    if (p.size_bytes > 0) {
        parts << QString::fromStdString(media::format_file_size(p.size_bytes));
    }
    return parts.join(QStringLiteral(" · "));
}

QStringList ConvertText::formatTags(const QString& filename, const QVariantMap& probe)
{
    // The container is in the name already; an image's codec is that again.
    const auto format = QString::fromStdString(media::format_display_name(
        std::filesystem::path(filename.toStdString()).extension().string()));
    QStringList tags;
    for (const auto* key : {"videoCodec", "audioCodec"}) {
        const QString codec = probe.value(QString::fromLatin1(key)).toString();
        if (!codec.isEmpty() && codec != format && !tags.contains(codec)) {
            tags << codec;
        }
    }
    // How many channels the sound has, after its codec: Stereo.
    if (probe.value(QStringLiteral("hasAudio")).toBool()) {
        const int channels = probe.value(QStringLiteral("channels")).toInt();
        if (channels == 1) {
            tags << tr("Mono");
        } else if (channels == 2) {
            tags << tr("Stereo");
        } else if (channels > 2) {
            tags << tr("%1 channels").arg(channels);
        }
    }
    return tags;
}

QString ConvertText::scaledSize(int chosen, const QVariantMap& probe)
{
    const auto size = media::scaled_size(chosen, probe_from_map(probe), false, false);
    if (!size || size->first <= 0) {
        return {};
    }
    return QStringLiteral("%1 × %2").arg(size->first).arg(size->second);
}

QVariantList ConvertText::details(const QString& kind, const QVariantMap& probe)
{
    const media::MediaProbe p = probe_from_map(probe);
    const int rate = probe.value(QStringLiteral("sampleRate")).toInt();
    QVariantList out;
    const auto add = [&out](const QString& label, const QString& value) {
        if (!value.isEmpty()) {
            out.push_back(
                QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
        }
    };
    const bool image = kind == QLatin1String("image");
    const bool audio = kind == QLatin1String("audio");
    if (!audio && p.width > 0 && p.height > 0) {
        add(tr("Picture"), QStringLiteral("%1 × %2").arg(p.width).arg(p.height));
    }
    if (!audio && !image && p.fps > 0.0) {
        add(tr("Frame rate"), tr("%1 fps").arg(QString::number(p.fps, 'g', 4)));
    }
    if (!image) {
        add(tr("Length"), QString::fromStdString(media::format_duration(p.duration_s)));
        if (p.has_audio) {
            // The sample rate; the channels are among the format tags.
            add(tr("Sound"), rate > 0 ? tr("%1 kHz").arg(QString::number(rate / 1000.0, 'g', 3))
                                      : tr("Yes"));
        } else if (!audio && !probe.isEmpty()) {
            add(tr("Sound"), tr("None"));
        }
    }
    if (p.size_bytes > 0) {
        add(tr("File size"), QString::fromStdString(media::format_file_size(p.size_bytes)));
    }
    return out;
}

QString ConvertText::outputKind(const QString& kind, const QVariantMap& choice)
{
    const auto parsed = choice_from_map(kind, choice);
    if (!parsed) {
        return {};
    }
    if (std::holds_alternative<media::AudioConvert>(*parsed)) {
        return QStringLiteral("audio");
    }
    if (std::holds_alternative<media::ImageConvert>(*parsed)) {
        return QStringLiteral("image");
    }
    return QStringLiteral("video");
}

QString ConvertText::extension(const QString& kind, const QVariantMap& choice)
{
    const auto parsed = choice_from_map(kind, choice);
    return parsed ? QString::fromUtf8(media::extension_of(*parsed)) : QString{};
}

QStringList ConvertText::videoCodecs(const QString& format)
{
    QStringList out;
    const auto parsed = media::convert_choice_from_json(
        "video", nlohmann::json{{"format", format.toStdString()}});
    const auto* video = parsed ? std::get_if<media::VideoConvert>(&*parsed) : nullptr;
    if (video == nullptr) {
        return out;
    }
    for (const auto codec : media::k_video_codecs) {
        if (media::video_codec_fits(video->format, codec)) {
            out << QString::fromUtf8(media::codec_key(codec));
        }
    }
    return out;
}

QStringList ConvertText::audioCodecs(const QString& format)
{
    QStringList out;
    const auto parsed = media::convert_choice_from_json(
        "video", nlohmann::json{{"format", format.toStdString()}});
    if (!parsed) {
        return out;
    }
    for (const auto codec : media::k_audio_codecs) {
        const bool fits = std::visit(
            [codec](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, media::ImageConvert>) {
                    return false;
                } else {
                    return media::audio_codec_fits(c.format, codec);
                }
            },
            *parsed);
        if (fits) {
            out << QString::fromUtf8(media::codec_key(codec));
        }
    }
    return out;
}

} // namespace seventhslide::app
