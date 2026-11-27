#include "destination_rows.hpp"

#include "lan_addresses.hpp"

#include <seventhslide/audio/device_sink.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/destination.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/virtual_camera.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace seventhslide::app {
namespace {

/// Target key -> the display name of the output a live ``kind`` destination
/// on it shows.
[[nodiscard]] QHash<QString, QString> assigned_outputs(const render::OutputWindowHost& host,
                                                       render::DestinationKind kind)
{
    QHash<QString, QString> out;
    const auto live = host.live_targets(kind);
    for (auto it = live.cbegin(); it != live.cend(); ++it) {
        out.insert(it.key(), host.registry().output_label(it.value()));
    }
    return out;
}

}  // namespace

namespace {

/// Each "srt://address:port" a receiver on the local network can open.
[[nodiscard]] QStringList reach_addresses(int port)
{
    QStringList out;
    for (const QString& addr : lan_addresses()) {
        out.push_back(QStringLiteral("srt://%1:%2").arg(addr).arg(port));
    }
    return out;
}

}  // namespace

QVariantMap stream_row(const stream::StreamSettings& s)
{
    // The passphrase itself stays in C++: the editor needs only to know
    // there is one.
    return {{QStringLiteral("mode"), QString::fromUtf8(stream::mode_name(s.mode))},
            {QStringLiteral("host"), QString::fromStdString(s.host)},
            {QStringLiteral("port"), s.port},
            {QStringLiteral("preset"), QString::fromUtf8(stream::preset_name(s.preset))},
            {QStringLiteral("codec"), QString::fromUtf8(stream::codec_name(s.codec))},
            {QStringLiteral("bitrateKbps"), s.bitrate_kbps},
            {QStringLiteral("encrypted"), !s.passphrase.empty()},
            {QStringLiteral("address"), QString::fromStdString(stream::address_text(s))},
            // Waiting: where a receiver on the network finds it.
            {QStringLiteral("reachAt"), s.mode == stream::SrtMode::Listener
                                            ? reach_addresses(s.port)
                                            : QStringList{}}};
}

std::optional<stream::StreamSettings> stream_settings_from(const QVariantMap& changes,
                                                           stream::StreamSettings base)
{
    const auto text = [&changes](const char* key) {
        return changes.value(QLatin1String(key)).toString().toStdString();
    };
    if (changes.contains(QStringLiteral("mode"))) {
        const auto mode = stream::mode_from_name(text("mode"));
        if (!mode) {
            return std::nullopt;
        }
        base.mode = *mode;
    }
    if (changes.contains(QStringLiteral("preset"))) {
        const auto preset = stream::preset_from_name(text("preset"));
        if (!preset) {
            return std::nullopt;
        }
        base.preset = *preset;
    }
    if (changes.contains(QStringLiteral("codec"))) {
        const auto codec = stream::codec_from_name(text("codec"));
        if (!codec) {
            return std::nullopt;
        }
        base.codec = *codec;
    }
    if (changes.contains(QStringLiteral("host"))) {
        base.host = QString::fromStdString(text("host")).trimmed().toStdString();
    }
    if (changes.contains(QStringLiteral("port"))) {
        bool ok = false;
        base.port = changes.value(QStringLiteral("port")).toInt(&ok);
        if (!ok) {
            base.port = 0;  // refused by stream::validate
        }
    }
    if (changes.contains(QStringLiteral("bitrateKbps"))) {
        base.bitrate_kbps = changes.value(QStringLiteral("bitrateKbps")).toInt();
    }
    if (changes.contains(QStringLiteral("passphrase"))) {
        base.passphrase = text("passphrase");
    }
    return base;
}

QVariantList stream_codec_rows()
{
    QVariantList out;
    for (const auto codec : {stream::VideoCodec::H265, stream::VideoCodec::H264}) {
        const auto known = stream::known_encoders(codec);
        if (!known) {
            return {};  // still being tried
        }
        const auto& encoders = *known;
        out.push_back(QVariantMap{
            {QStringLiteral("key"), QString::fromUtf8(stream::codec_name(codec))},
            {QStringLiteral("available"), !encoders.empty()},
            {QStringLiteral("encoder"),
             encoders.empty() ? QString() : QString::fromStdString(encoders.front().name)},
            {QStringLiteral("hardware"), !encoders.empty() && encoders.front().hardware},
        });
    }
    return out;
}

QVariantMap stream_bitrate_row(const stream::StreamSettings& s, QSize size)
{
    return {{QStringLiteral("auto"),
             stream::auto_bitrate_kbps(s.codec, s.preset, size.width(), size.height())},
            {QStringLiteral("latencyMs"), stream::latency_ms(s.preset)}};
}

QVariantList destination_type_rows()
{
    QVariantList out;
    for (const auto kind : render::k_destination_kinds) {
        QVariantMap row{{QStringLiteral("type"), render::destination_kind_name(kind)},
                        {QStringLiteral("available"), render::destination_kind_available(kind)},
                        {QStringLiteral("built"), render::destination_kind_built(kind)}};
        // What to do when there is no device, in this system's own terms.
        if (kind == render::DestinationKind::Vcam) {
            row.insert(QStringLiteral("message"), render::virtual_camera_missing_message());
        }
        out.push_back(row);
    }
    return out;
}

QVariantList destination_rows(const render::OutputWindowHost& host, const QString& output_id)
{
    const auto& registry = host.registry();
    const auto& all = registry.all();
    const QStringList titles = registry.titles();
    QVariantList out;
    for (int i = 0; i < all.size(); ++i) {
        const auto& d = all[i];
        if (!output_id.isEmpty() && d.output_id != output_id) {
            continue;
        }
        QVariantMap m;
        m.insert(QStringLiteral("id"), d.id);
        m.insert(QStringLiteral("output"), d.output_id);
        m.insert(QStringLiteral("type"), render::destination_kind_name(d.kind));
        m.insert(QStringLiteral("enabled"), d.enabled);
        m.insert(QStringLiteral("ignoreLive"), d.ignore_live);
        m.insert(QStringLiteral("screenKey"), d.target_key);
        m.insert(QStringLiteral("targetKey"), d.target_key);
        m.insert(QStringLiteral("screenLabel"), host.devices().target_label(d));
        m.insert(QStringLiteral("name"), titles[i]);
        m.insert(QStringLiteral("live"), host.is_live(d));
        // Its window was closed while it stayed on (OutputWindowHost::is_closed).
        m.insert(QStringLiteral("closed"), host.is_closed(d.id));
        m.insert(QStringLiteral("missing"), host.devices().target_missing(d));
        m.insert(QStringLiteral("alpha"), d.alpha);
        m.insert(QStringLiteral("audio"), d.audio);
        m.insert(QStringLiteral("audioCapable"), render::destination_kind_carries_audio(d.kind));
        m.insert(QStringLiteral("sizable"), render::destination_kind_sizable(d.kind));
        m.insert(QStringLiteral("soundAloneCapable"),
                 render::destination_kind_sends_sound_alone(d.kind));
        m.insert(QStringLiteral("maxHeight"), d.max_height);
        m.insert(QStringLiteral("video"), d.video);
        const auto status = host.sink_status(d.id);
        switch (d.kind) {
        case render::DestinationKind::Bmd:
            m.insert(QStringLiteral("modeKey"), d.mode_key);
            m.insert(QStringLiteral("modeName"), status.mode_name);
            m.insert(QStringLiteral("keying"), status.keying);
            m.insert(QStringLiteral("driverPresent"), render::bmd_runtime_available());
            break;
        case render::DestinationKind::Ndi:
            m.insert(QStringLiteral("receivers"), status.receivers);
            m.insert(QStringLiteral("onProgram"), status.on_program);
            m.insert(QStringLiteral("onPreview"), status.on_preview);
            break;
        case render::DestinationKind::Display:
        case render::DestinationKind::Window:
            m.insert(QStringLiteral("audioDevice"), d.audio_device);
            m.insert(QStringLiteral("soundPlaying"), host.display_sound_playing(d.id));
            break;
        case render::DestinationKind::Vcam:
            break;
        case render::DestinationKind::Srt:
            m.insert(QStringLiteral("stream"), stream_row(d.stream));
            m.insert(QStringLiteral("streamState"),
                     status.stream_state.isEmpty() ? QStringLiteral("stopped")
                                                   : status.stream_state);
            m.insert(QStringLiteral("encoder"), status.encoder);
            m.insert(QStringLiteral("hardwareEncoder"), status.hardware_encoder);
            m.insert(QStringLiteral("bitrateKbps"), status.bitrate_kbps);
            m.insert(QStringLiteral("streamError"), status.error);
            break;
        }
        m.insert(QStringLiteral("conflictOutput"), registry.conflict_output(d));
        out.push_back(m);
    }
    return out;
}

QVariantList destination_size_rows(QSize canvas)
{
    QVariantList out;
    if (canvas.width() <= 0 || canvas.height() <= 0) {
        return out;
    }
    const auto row = [&out](QSize size) {
        out.push_back(QVariantMap{{QStringLiteral("w"), size.width()},
                                  {QStringLiteral("h"), size.height()}});
    };
    row(canvas);
    for (const int h : {2160, 1440, 1080, 720, 540, 480, 360}) {
        if (h < canvas.height()) {
            row(render::destination_send_size(canvas, h));
        }
    }
    return out;
}

QVariantList display_rows(const render::OutputWindowHost& host)
{
    const auto open = host.open_screens();
    QVariantList out;
    for (const auto& s : render::DeviceCatalog::screens()) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), s.key);
        m.insert(QStringLiteral("index"), s.index);
        m.insert(QStringLiteral("label"), s.label);
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("primary"), s.primary);
        m.insert(QStringLiteral("scaleFactor"), s.scale);
        m.insert(QStringLiteral("bounds"),
                 QVariantMap{{QStringLiteral("x"), s.bounds.x()},
                             {QStringLiteral("y"), s.bounds.y()},
                             {QStringLiteral("width"), s.bounds.width()},
                             {QStringLiteral("height"), s.bounds.height()}});
        m.insert(QStringLiteral("nativeWidth"), s.native.width());
        m.insert(QStringLiteral("nativeHeight"), s.native.height());
        const auto it = open.constFind(s.key);
        m.insert(QStringLiteral("assignedOutput"),
                 it != open.cend() ? host.registry().output_label(it.value()) : QString{});
        out.push_back(m);
    }
    return out;
}

QVariantList virtual_camera_rows(const render::OutputWindowHost* host)
{
    const auto assigned = host != nullptr
                              ? assigned_outputs(*host, render::DestinationKind::Vcam)
                              : QHash<QString, QString>{};
    QVariantList out;
    for (const auto& info : render::list_virtual_camera_devices()) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), info.path);
        m.insert(QStringLiteral("name"), info.card);
        m.insert(QStringLiteral("label"), info.label);
        if (host != nullptr) {
            m.insert(QStringLiteral("assignedOutput"), assigned.value(info.path, QString{}));
        }
        out.push_back(m);
    }
    return out;
}

QVariantList blackmagic_device_rows(const render::OutputWindowHost* host)
{
    const auto assigned = host != nullptr
                              ? assigned_outputs(*host, render::DestinationKind::Bmd)
                              : QHash<QString, QString>{};
    QVariantList out;
    for (const auto& d : render::bmd_list_devices()) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), d.id);
        m.insert(QStringLiteral("name"), d.name);
        m.insert(QStringLiteral("model"), d.model);
        m.insert(QStringLiteral("label"), d.label);
        m.insert(QStringLiteral("supportsInternalKeying"), d.supports_internal_key);
        m.insert(QStringLiteral("supportsExternalKeying"), d.supports_external_key);
        if (host != nullptr) {
            m.insert(QStringLiteral("assignedOutput"), assigned.value(d.id, QString{}));
        }
        out.push_back(m);
    }
    return out;
}

QVariantList blackmagic_mode_rows(const QString& device_id)
{
    QVariantList out;
    for (const auto& mode : render::bmd_list_display_modes(device_id)) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), mode.key);
        m.insert(QStringLiteral("name"), mode.name);
        m.insert(QStringLiteral("width"), mode.width);
        m.insert(QStringLiteral("height"), mode.height);
        m.insert(QStringLiteral("fps"), mode.fps());
        m.insert(QStringLiteral("interlaced"), mode.interlaced);
        out.push_back(m);
    }
    return out;
}

QVariantMap ndi_runtime_row()
{
    const bool available = render::ndi_runtime_available();
    QVariantMap m;
    m.insert(QStringLiteral("available"), available);
    m.insert(QStringLiteral("version"), render::ndi_runtime_version());
    m.insert(QStringLiteral("path"), render::ndi_runtime_path());
    m.insert(QStringLiteral("message"), available ? QString{} : render::ndi_missing_message());
    return m;
}

QVariantMap bmd_runtime_row()
{
    const bool available = render::bmd_runtime_available();
    const int devices = available ? int(render::bmd_list_devices().size()) : 0;
    QVariantMap m;
    m.insert(QStringLiteral("available"), available);
    m.insert(QStringLiteral("version"), render::bmd_runtime_version());
    m.insert(QStringLiteral("path"), render::bmd_runtime_path());
    m.insert(QStringLiteral("deviceCount"), devices);
    if (!available) {
        m.insert(QStringLiteral("message"), render::bmd_missing_message());
    } else if (devices == 0) {
        m.insert(QStringLiteral("message"), render::bmd_no_device_message());
    } else {
        m.insert(QStringLiteral("message"), QString{});
    }
    return m;
}

namespace {

/// The words of a name worth matching on: letters and digits, three or more.
QSet<QString> name_words(const QString& name)
{
    static const QRegularExpression word(QStringLiteral("[A-Za-z0-9]{3,}"));
    QSet<QString> out;
    for (auto it = word.globalMatch(name); it.hasNext();) {
        out.insert(it.next().captured().toLower());
    }
    return out;
}

}  // namespace

QVariantList sound_device_rows(const QString& screen_label,
                               const std::vector<audio::DeviceInfo>& devices)
{
    // A screen does not say which sound device is its own; one whose name
    // shares words with the screen's (its model, "HDMI") is likeliest.
    const QSet<QString> screen = name_words(screen_label);
    struct Ranked {
        int shared = 0;
        const audio::DeviceInfo* device = nullptr;
    };
    std::vector<Ranked> ranked;
    for (const auto& d : devices) {
        const auto words = name_words(QString::fromStdString(d.name));
        int shared = 0;
        for (const auto& w : words) {
            shared += screen.contains(w) ? 1 : 0;
        }
        ranked.push_back({shared, &d});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) { return a.shared > b.shared; });
    QVariantList out;
    // "None": the view names it.
    out.push_back(QVariantMap{{QStringLiteral("key"), QString()},
                              {QStringLiteral("label"), QString()},
                              {QStringLiteral("suggested"), false}});
    for (const auto& r : ranked) {
        out.push_back(QVariantMap{{QStringLiteral("key"), QString::fromStdString(r.device->id)},
                                  {QStringLiteral("label"), QString::fromStdString(r.device->name)},
                                  {QStringLiteral("suggested"), r.shared > 0}});
    }
    return out;
}

}  // namespace seventhslide::app
