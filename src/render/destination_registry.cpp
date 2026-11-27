#include <seventhslide/render/destination_registry.hpp>

#include <seventhslide/json/read.hpp>
#include <seventhslide/render/device_catalog.hpp>
#include <seventhslide/render/ndi_output.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>
#include <seventhslide/util/find.hpp>

#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace seventhslide::render {
namespace {

[[nodiscard]] QString unique_destination_title(DestinationKind kind,
                                               const QStringList& taken_titles)
{
    QSet<QString> taken;
    for (const QString& t : taken_titles) {
        taken.insert(t.trimmed().toLower());
    }
    const QString base = destination_kind_title(kind);
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!taken.contains(candidate.toLower())) {
            return candidate;
        }
    }
}

[[nodiscard]] QString new_dest_id()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

[[nodiscard]] nlohmann::json stream_record(const stream::StreamSettings& s)
{
    return {
        {"mode", std::string(stream::mode_name(s.mode))},
        {"host", s.host},
        {"port", s.port},
        {"preset", std::string(stream::preset_name(s.preset))},
        {"codec", std::string(stream::codec_name(s.codec))},
        {"bitrate_kbps", s.bitrate_kbps},
        {"passphrase", s.passphrase},
    };
}

/// A part missing or not understood is read as a new destination has it.
[[nodiscard]] stream::StreamSettings stream_from_record(const nlohmann::json& o)
{
    stream::StreamSettings s;
    s.mode = stream::mode_from_name(json::str_or(o, "mode")).value_or(s.mode);
    s.host = json::str_or(o, "host");
    s.port = json::int_or(o, "port", s.port);
    s.preset = stream::preset_from_name(json::str_or(o, "preset")).value_or(s.preset);
    s.codec = stream::codec_from_name(json::str_or(o, "codec")).value_or(s.codec);
    s.bitrate_kbps = json::int_or(o, "bitrate_kbps", 0);
    s.passphrase = json::str_or(o, "passphrase");
    return s;
}

/// The text ``key`` holds in ``o``; empty when it holds none.
[[nodiscard]] QString text_of(const nlohmann::json& o, const char* key)
{
    return QString::fromStdString(json::str_or(o, key));
}

}  // namespace

Destination* DestinationRegistry::find(const QString& dest_id)
{
    return util::find_by(destinations_, dest_id, &Destination::id);
}

const Destination* DestinationRegistry::find(const QString& dest_id) const
{
    return util::find_by(destinations_, dest_id, &Destination::id);
}

bool DestinationRegistry::set_output_names(QHash<QString, QString> names_by_id)
{
    if (names_by_id == output_names_) {
        return false;
    }
    output_names_ = std::move(names_by_id);
    return true;
}

QString DestinationRegistry::output_label(const QString& output_id) const
{
    return output_names_.value(output_id, output_id);
}

Destination& DestinationRegistry::add(const QString& output_id, DestinationKind kind)
{
    Destination d;
    d.id = new_dest_id();
    d.output_id = output_id;
    d.kind = kind;
    d.enabled = false;
    choose_target(d);
    d.name = unique_title(d.kind, output_id);
    destinations_.push_back(std::move(d));
    return destinations_.back();
}

void DestinationRegistry::choose_target(Destination& dest)
{
    switch (dest.kind) {
    case DestinationKind::Display:
        dest.target_key = unused_screen_key();
        DeviceCatalog::apply_screen(dest, DeviceCatalog::screen_by_key(dest.target_key));
        break;
    case DestinationKind::Vcam:
        dest.target_key = unused_camera_key();
        dest.label = DeviceCatalog::camera_label(dest.target_key);
        break;
    case DestinationKind::Ndi:
        dest.target_key = unused_ndi_name(dest.output_id);
        dest.label = dest.target_key;
        break;
    case DestinationKind::Bmd:
        // mode_key stays empty: the sink picks the card's best 1080p mode
        // until the operator chooses one.
        dest.target_key = unused_bmd_device();
        dest.label = DeviceCatalog::bmd_device_label(dest.target_key);
        break;
    case DestinationKind::Srt:
        // Waiting for a receiver, the simplest to point one at: H.265 where
        // this computer can encode it. This runs on the window's thread, so
        // it does not try the encoders itself (the output editor has them
        // tried on a worker when it opens); until they are known, H.264.
        dest.stream = stream::StreamSettings{};
        dest.stream.port = unused_srt_port();
        if (const auto h265 = stream::known_encoders(stream::VideoCodec::H265); !h265 || h265->empty()) {
            dest.stream.codec = stream::VideoCodec::H264;
        }
        dest.target_key = QString::fromStdString(stream::address_text(dest.stream));
        dest.label = dest.target_key;
        break;
    case DestinationKind::Window:
        // No target: any number of windows can be open, and the first opens
        // where the host puts a new one (its bounds empty).
        break;
    }
}

bool DestinationRegistry::remove(const QString& dest_id)
{
    for (int i = 0; i < destinations_.size(); ++i) {
        if (destinations_[i].id == dest_id) {
            destinations_.removeAt(i);
            return true;
        }
    }
    return false;
}

bool DestinationRegistry::move(const QString& dest_id, int step)
{
    if (step == 0) {
        return false;
    }
    const int dir = step < 0 ? -1 : 1;
    for (int i = 0; i < destinations_.size(); ++i) {
        if (destinations_[i].id != dest_id) {
            continue;
        }
        // Other outputs' destinations share the list; step over them.
        for (int j = i + dir; j >= 0 && j < destinations_.size(); j += dir) {
            if (destinations_[j].output_id == destinations_[i].output_id) {
                std::swap(destinations_[i], destinations_[j]);
                return true;
            }
        }
        return false;
    }
    return false;
}

QStringList DestinationRegistry::remove_output(const QString& output_id)
{
    QStringList removed;
    QVector<Destination> kept;
    kept.reserve(destinations_.size());
    for (auto& d : destinations_) {
        if (d.output_id == output_id) {
            removed.push_back(d.id);
            continue;
        }
        kept.push_back(std::move(d));
    }
    destinations_ = std::move(kept);
    return removed;
}

QStringList DestinationRegistry::others_on_target(DestinationKind kind,
                                                  const QString& target_key,
                                                  const QString& keep_id) const
{
    QStringList out;
    if (target_key.isEmpty()) {
        return out;
    }
    for (const auto& d : destinations_) {
        if (d.id != keep_id && d.enabled && d.kind == kind && d.target_key == target_key) {
            out.push_back(d.id);
        }
    }
    return out;
}

QString DestinationRegistry::conflict_output(const Destination& d) const
{
    if (d.target_key.isEmpty()) {
        return {};
    }
    for (const auto& other : destinations_) {
        if (other.id != d.id && other.enabled && other.kind == d.kind
            && other.target_key == d.target_key) {
            return output_label(other.output_id);
        }
    }
    return {};
}

QStringList DestinationRegistry::titles() const
{
    QHash<QString, QStringList> claimed;
    for (const auto& d : destinations_) {
        if (!DeviceCatalog::opaque_name(d.name)) {
            claimed[d.output_id].push_back(d.name);
        }
    }
    QStringList out;
    out.reserve(destinations_.size());
    for (const auto& d : destinations_) {
        QString title = d.name;
        if (DeviceCatalog::opaque_name(title)) {
            title = unique_destination_title(d.kind, claimed.value(d.output_id));
            claimed[d.output_id].push_back(title);
        }
        out.push_back(title);
    }
    return out;
}

QString DestinationRegistry::unique_title(DestinationKind kind, const QString& output_id,
                                          const QString& exclude_id) const
{
    QStringList taken;
    for (const auto& d : destinations_) {
        if (d.output_id != output_id || d.id == exclude_id) {
            continue;
        }
        if (DeviceCatalog::opaque_name(d.name)) {
            continue;
        }
        taken.push_back(d.name);
    }
    return unique_destination_title(kind, taken);
}

QString DestinationRegistry::unused_screen_key() const
{
    QSet<QString> claimed;
    for (const auto& d : destinations_) {
        if (d.kind == DestinationKind::Display && !d.target_key.isEmpty()) {
            claimed.insert(d.target_key);
        }
    }
    // A screen the operator is not on first: an output opened over the
    // operator's own window hides what they work in.
    QList<QScreen*> screens = QGuiApplication::screens();
    if (QScreen* own = DeviceCatalog::operator_screen(); own != nullptr && screens.removeOne(own)) {
        screens.push_back(own);
    }
    for (QScreen* s : screens) {
        const QString key = DeviceCatalog::screen_key(s);
        if (!claimed.contains(key)) {
            return key;
        }
    }
    if (QScreen* fallback = DeviceCatalog::audience_screen()) {
        return DeviceCatalog::screen_key(fallback);
    }
    return {};
}

QString DestinationRegistry::unused_camera_key() const
{
    QSet<QString> claimed;
    for (const auto& d : destinations_) {
        if (d.kind == DestinationKind::Vcam && !d.target_key.isEmpty() && d.enabled) {
            claimed.insert(d.target_key);
        }
    }
    QString fallback;
    for (const auto& info : list_virtual_camera_devices()) {
        if (claimed.contains(info.path)) {
            continue;
        }
        if (info.card.contains(QLatin1String("SeventhSlide"), Qt::CaseInsensitive)) {
            return info.path;
        }
        if (fallback.isEmpty()) {
            fallback = info.path;
        }
    }
    return fallback;
}

QString DestinationRegistry::unused_ndi_name(const QString& output_id,
                                             const QString& exclude_id) const
{
    QSet<QString> claimed;
    for (const auto& d : destinations_) {
        if (d.kind != DestinationKind::Ndi || d.id == exclude_id) {
            continue;
        }
        if (!d.target_key.isEmpty()) {
            claimed.insert(d.target_key.trimmed().toLower());
        }
    }
    const QString base = ndi_default_source_name(output_label(output_id));
    if (!claimed.contains(base.toLower())) {
        return base;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!claimed.contains(candidate.toLower())) {
            return candidate;
        }
    }
}

QString DestinationRegistry::unused_bmd_device() const
{
    QSet<QString> claimed;
    for (const auto& d : destinations_) {
        if (d.kind == DestinationKind::Bmd && !d.target_key.isEmpty() && d.enabled) {
            claimed.insert(d.target_key);
        }
    }
    QString fallback;
    for (const auto& info : bmd_list_devices()) {
        if (claimed.contains(info.id)) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = info.id;
        }
    }
    return fallback;
}

int DestinationRegistry::unused_srt_port() const
{
    QSet<int> claimed;
    for (const auto& d : destinations_) {
        if (d.kind == DestinationKind::Srt) {
            claimed.insert(d.stream.port);
        }
    }
    int port = stream::k_default_port;
    while (claimed.contains(port)) {
        ++port;
    }
    return port;
}

bool DestinationRegistry::ndi_name_taken(const QString& name, const QString& dest_id) const
{
    for (const auto& other : destinations_) {
        if (other.id == dest_id || other.kind != DestinationKind::Ndi) {
            continue;
        }
        if (other.target_key.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

nlohmann::json DestinationRegistry::records() const
{
    auto arr = nlohmann::json::array();
    for (const auto& d : destinations_) {
        nlohmann::json o = {
            {"id", d.id.toStdString()},
            {"output", d.output_id.toStdString()},
            {"type", destination_kind_name(d.kind).toStdString()},
            {"enabled", d.enabled},
            {"ignore_live", d.ignore_live},
            {"key", d.target_key.toStdString()},
            {"name", d.name.toStdString()},
            {"label", d.label.toStdString()},
            {"bounds", d.bounds.toStdString()},
            {"mode", d.mode_key.toStdString()},
            {"alpha", d.alpha},
            {"audio", d.audio},
            {"audio_device", d.audio_device.toStdString()},
            {"max_height", d.max_height},
            {"video", d.video},
        };
        if (d.kind == DestinationKind::Srt) {
            o["stream"] = stream_record(d.stream);
        }
        arr.push_back(std::move(o));
    }
    return arr;
}

void DestinationRegistry::replace(const nlohmann::json& records)
{
    destinations_.clear();
    if (!records.is_array()) {
        return;
    }
    for (const auto& o : records) {
        if (!o.is_object()) {
            continue;
        }
        const QString output_id = text_of(o, "output");
        if (output_id.isEmpty()) {
            continue;
        }
        const QString type = text_of(o, "type");
        const auto kind = destination_kind_from_name(type);
        if (!kind) {
            qWarning("SeventhSlide: skipping an output destination of unknown type \"%s\"",
                     qUtf8Printable(type));
            continue;
        }
        Destination d;
        d.output_id = output_id;
        d.kind = *kind;
        d.id = text_of(o, "id");
        if (d.id.isEmpty()) {
            qWarning("SeventhSlide: skipping an output destination with no id");
            continue;
        }
        d.enabled = json::bool_or(o, "enabled", true);
        d.ignore_live = json::bool_or(o, "ignore_live", false);
        d.target_key = text_of(o, "key");
        d.name = text_of(o, "name");
        d.label = text_of(o, "label");
        d.bounds = text_of(o, "bounds");
        // An empty mode means "let the card pick".
        d.mode_key = text_of(o, "mode");
        d.alpha = json::bool_or(o, "alpha", false);
        d.audio = json::bool_or(o, "audio", true);
        d.audio_device = text_of(o, "audio_device");
        // No height limit: as the output is.
        d.max_height = std::max(0, json::int_or(o, "max_height", 0));
        d.video = json::bool_or(o, "video", true);
        if (!destination_kind_sizable(d.kind)) {
            d.max_height = 0;
        }
        if (!destination_kind_sends_sound_alone(d.kind)) {
            d.video = true;
        }
        if (!d.video) {
            d.audio = true;
        }
        if (d.kind == DestinationKind::Srt) {
            d.stream = stream_from_record(json::object_or_empty(o, "stream"));
            d.target_key = QString::fromStdString(stream::address_text(d.stream));
            d.label = d.target_key;
        }
        destinations_.push_back(std::move(d));
    }
    for (auto& d : destinations_) {
        if (DeviceCatalog::opaque_name(d.name)) {
            d.name = unique_title(d.kind, d.output_id, d.id);
        }
    }
}

}  // namespace seventhslide::render
