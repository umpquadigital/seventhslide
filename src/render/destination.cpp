#include <seventhslide/render/destination.hpp>

#include <seventhslide/audio/pipewire_source.hpp>

#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/virtual_camera.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>

#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cstddef>

namespace seventhslide::render {
namespace {

/// What there is to know about each kind that is not behaviour.
struct KindInfo {
    DestinationKind kind;
    const char* name;
    const char* title;
    bool offscreen;
    /// Its picture's size is the destination's to choose (a Blackmagic
    /// card's video mode decides its own).
    bool sizable;
    /// It can be sent as sound alone, with no picture.
    bool sound_alone;
    bool (*available)();
};

/// In ``DestinationKind`` order, which is also ``k_destination_kinds``'.
constexpr std::array<KindInfo, 6> k_kinds{{
    {DestinationKind::Display, "display", "Display", false, true, false, [] { return true; }},
    // NDI lights up only when the operator has installed a runtime.
    {DestinationKind::Ndi, "ndi", "NDI", true, true, true,
     [] { return ndi_runtime_available(); }},
    // Blackmagic lights up on the DRIVER being installed; a driver with no
    // card attached is a live row with a missing device.
    {DestinationKind::Bmd, "bmd", "Blackmagic", true, false, false,
     [] { return bmd_runtime_available(); }},
    {DestinationKind::Vcam, "vcam", "Virtual Camera", true, true, false,
     [] { return virtual_camera_implemented(); }},
    // Where FFmpeg was built with libsrt.
    {DestinationKind::Srt, "srt", "SRT", true, true, false, [] { return stream::srt_supported(); }},
    {DestinationKind::Window, "window", "Window", false, true, false, [] { return true; }},
}};

[[nodiscard]] const KindInfo& kind_info(DestinationKind kind)
{
    const auto& info = k_kinds[static_cast<std::size_t>(kind)];
    Q_ASSERT(info.kind == kind);
    return info;
}

}  // namespace

QString destination_kind_name(DestinationKind kind)
{
    return QString::fromUtf8(kind_info(kind).name);
}

std::optional<DestinationKind> destination_kind_from_name(const QString& name)
{
    for (const auto& info : k_kinds) {
        if (name == QLatin1String(info.name)) {
            return info.kind;
        }
    }
    return std::nullopt;
}

QString destination_kind_title(DestinationKind kind)
{
    return QString::fromUtf8(kind_info(kind).title);
}

bool destination_kind_offscreen(DestinationKind kind)
{
    return kind_info(kind).offscreen;
}

bool destination_kind_windowed(DestinationKind kind)
{
    return !kind_info(kind).offscreen;
}

bool destination_kind_available(DestinationKind kind)
{
    return kind_info(kind).available();
}

bool destination_kind_built(DestinationKind kind)
{
    return kind != DestinationKind::Vcam || virtual_camera_implemented();
}

bool destination_kind_sizable(DestinationKind kind)
{
    return kind_info(kind).sizable;
}

bool destination_kind_sends_sound_alone(DestinationKind kind)
{
    return kind_info(kind).sound_alone;
}

QSize destination_send_size(QSize full, int max_height)
{
    if (full.width() <= 0 || full.height() <= 0) {
        return full;
    }
    if (max_height <= 0 || max_height >= full.height()) {
        return full;
    }
    // Even, as video wants, and never under 2: the width to the nearest even
    // number in the full shape.
    const int h = std::max(2, max_height & ~1);
    const qint64 halves = (static_cast<qint64>(full.width()) * h + full.height())
                          / (2 * static_cast<qint64>(full.height()));
    return {std::max(2, static_cast<int>(2 * halves)), h};
}

QString bounds_text(QRect r)
{
    return QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QRect desktop_window_geometry(const QString& bounds, QSize canvas, const QList<QRect>& screens,
                              QRect home)
{
    const QStringList parts = bounds.split(QLatin1Char(','));
    if (parts.size() == 4) {
        bool ok[4] = {};
        const QRect kept(parts[0].trimmed().toInt(&ok[0]), parts[1].trimmed().toInt(&ok[1]),
                         parts[2].trimmed().toInt(&ok[2]), parts[3].trimmed().toInt(&ok[3]));
        // Enough on a screen to take hold of: its top (where the title bar
        // is) and some of its width. The screen showing most of that is the
        // one it opens on.
        const QRect top(kept.x(), kept.y(), kept.width(), std::min(kept.height(), 40));
        const QRect* on = nullptr;
        int most = 0;
        for (const QRect& screen : screens) {
            const QRect seen = screen.intersected(top);
            if (seen.width() >= 80 && seen.height() >= std::min(top.height(), 20)
                && seen.width() * seen.height() > most) {
                most = seen.width() * seen.height();
                on = &screen;
            }
        }
        if (ok[0] && ok[1] && ok[2] && ok[3] && kept.width() > 0 && kept.height() > 0
            && on != nullptr) {
            // No larger than that screen (left on a larger one), so its
            // edges are there to size it by.
            return {kept.topLeft(),
                    QSize(std::min(kept.width(), on->width()), std::min(kept.height(), on->height()))};
        }
    }
    if (canvas.width() <= 0 || canvas.height() <= 0) {
        canvas = QSize(1920, 1080);
    }
    if (home.isEmpty()) {
        home = QRect(0, 0, 1920, 1080);
    }
    int w = std::min(960, std::max(160, home.width() / 2));
    int h = static_cast<int>(static_cast<qint64>(w) * canvas.height() / canvas.width());
    // A tall canvas: as tall as half the screen instead.
    if (h > home.height() / 2 && home.height() >= 180) {
        h = home.height() / 2;
        w = static_cast<int>(static_cast<qint64>(h) * canvas.width() / canvas.height());
    }
    w = std::max(w, 160);
    h = std::max(h, 90);
    return {home.x() + (home.width() - w) / 2, home.y() + (home.height() - h) / 2, w, h};
}

bool destination_kind_carries_audio(DestinationKind kind)
{
    switch (kind) {
    case DestinationKind::Ndi:
    case DestinationKind::Bmd:
    case DestinationKind::Srt:
        return true;
    case DestinationKind::Vcam: {
        // A microphone beside the camera, where PipeWire answers. Asked once:
        // the probe connects to the server.
        static const bool pipewire = audio::pipewire_available();
        return virtual_camera_implemented() && pipewire;
    }
    case DestinationKind::Display:
    case DestinationKind::Window:
        // Its sound goes to a device of its own (audio_device), not "Send
        // audio".
        return false;
    }
    return false;
}

}  // namespace seventhslide::render
