#include <seventhslide/render/device_catalog.hpp>

#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>

#if defined(Q_OS_MACOS)
#include "output_window_mac.hpp"
#endif

#include <QGuiApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QWindow>
#include <QStringList>

#include <algorithm>

namespace seventhslide::render {
namespace {

[[nodiscard]] QSize native_size(const QScreen* screen)
{
    if (!screen) {
        return {};
    }
    const qreal s = screen->devicePixelRatio();
    const QRect g = screen->geometry();
    return QSize(qRound(g.width() * s), qRound(g.height() * s));
}

[[nodiscard]] QString human_screen_name(const QScreen* screen)
{
    if (!screen) {
        return {};
    }
    const QString model = screen->model().trimmed();
    const QString manufacturer = screen->manufacturer().trimmed();
    if (!model.isEmpty()) {
        if (!manufacturer.isEmpty() && !model.contains(manufacturer, Qt::CaseInsensitive)
            && !manufacturer.contains(model, Qt::CaseInsensitive)) {
            return QStringLiteral("%1 %2").arg(manufacturer, model);
        }
        return model;
    }
    const QString name = screen->name().trimmed();
    if (!DeviceCatalog::opaque_name(name)) {
        return name;
    }
    return {};
}

[[nodiscard]] QString friendly_label(const QScreen* screen, int index1, bool primary)
{
    QStringList parts;
    parts << QStringLiteral("Screen %1").arg(index1);
    const QString human = human_screen_name(screen);
    if (!human.isEmpty()) {
        parts << human;
    } else if (primary) {
        parts << QStringLiteral("primary");
    }
    const QSize n = native_size(screen);
    if (n.width() > 0 && n.height() > 0) {
        parts << QStringLiteral("%1×%2").arg(n.width()).arg(n.height());
    }
    return parts.join(QStringLiteral(" · "));
}

[[nodiscard]] QString disconnected_screen_label(const QString& label, const QString& bounds)
{
    if (!DeviceCatalog::opaque_name(label)) {
        return label;
    }
    const auto parts = bounds.split(QLatin1Char(','));
    if (parts.size() == 4) {
        return QStringLiteral("%1×%2").arg(parts.at(2).trimmed(), parts.at(3).trimmed());
    }
    return QStringLiteral("Not connected");
}

[[nodiscard]] QString screen_label_for(const QString& screen_key)
{
    QScreen* screen = DeviceCatalog::screen_by_key(screen_key);
    if (!screen) {
        return {};
    }
    const auto screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    for (int i = 0; i < screens.size(); ++i) {
        if (screens[i] == screen) {
            return friendly_label(screen, i + 1, screen == primary);
        }
    }
    const QString human = human_screen_name(screen);
    return human.isEmpty() ? QStringLiteral("Display") : human;
}

}  // namespace

struct DeviceCatalog::Snapshot {
    std::chrono::steady_clock::time_point taken;
    std::optional<std::vector<BmdDeviceInfo>> bmd;
    std::optional<std::vector<VirtualCameraDeviceInfo>> cameras;
    QHash<QString, std::vector<BmdDisplayModeInfo>> bmd_modes;

    const std::vector<BmdDeviceInfo>& bmd_devices()
    {
        if (!bmd) {
            bmd = bmd_list_devices();
        }
        return *bmd;
    }

    const std::vector<VirtualCameraDeviceInfo>& camera_devices()
    {
        if (!cameras) {
            cameras = list_virtual_camera_devices();
        }
        return *cameras;
    }

    const std::vector<BmdDisplayModeInfo>& modes(const QString& device_id)
    {
        auto it = bmd_modes.find(device_id);
        if (it == bmd_modes.end()) {
            it = bmd_modes.insert(device_id, bmd_list_display_modes(device_id));
        }
        return it.value();
    }
};

DeviceCatalog::DeviceCatalog() = default;
DeviceCatalog::~DeviceCatalog() = default;

namespace {

/// "Dell U2720Q": what the screen says it is, or empty.
[[nodiscard]] QString screen_model(const QScreen* screen)
{
    return QStringLiteral("%1 %2").arg(screen->manufacturer(), screen->model()).trimmed();
}

/// The model a ``model:`` key names, or empty for another key.
[[nodiscard]] QString key_model(const QString& key)
{
    if (!key.startsWith(QLatin1String("model:"))) {
        return {};
    }
    const qsizetype at = key.lastIndexOf(QLatin1Char('@'));
    return key.mid(6, at < 0 ? -1 : at - 6);
}

}  // namespace

QString DeviceCatalog::screen_key(const QScreen* screen)
{
    if (!screen) {
        return {};
    }
    if (!screen->serialNumber().isEmpty()) {
        return QStringLiteral("serial:%1").arg(screen->serialNumber());
    }
#if defined(Q_OS_MACOS)
    // Qt's Cocoa plugin gives no serial (nor maker or model), and a key
    // with the geometry in it changes with the resolution or the arrangement.
    // The display's UUID does not. Kept on the screen, so a screen going away
    // (its display gone already) still has the key it had.
    static const char* const k_uuid_property = "_seventhslide_display_uuid";
    QString uuid = screen->property(k_uuid_property).toString();
    if (uuid.isEmpty()) {
        uuid = display_uuid(*screen);
        if (!uuid.isEmpty()) {
            const_cast<QScreen*>(screen)->setProperty(k_uuid_property, uuid);
        }
    }
    if (!uuid.isEmpty()) {
        return QStringLiteral("display:%1").arg(uuid);
    }
#endif
    if (const QString model = screen_model(screen); !model.isEmpty()) {
        return QStringLiteral("model:%1@%2").arg(model, bounds_string(screen));
    }
    if (!screen->name().isEmpty()) {
        return QStringLiteral("name:%1@%2").arg(screen->name(), bounds_string(screen));
    }
    return QStringLiteral("geom:%1").arg(bounds_string(screen));
}

QString DeviceCatalog::bounds_string(const QScreen* screen)
{
    if (!screen) {
        return {};
    }
    const QRect g = screen->geometry();
    return QStringLiteral("%1,%2,%3,%4")
        .arg(g.x())
        .arg(g.y())
        .arg(g.width())
        .arg(g.height());
}

QScreen* DeviceCatalog::screen_by_key(const QString& key)
{
    if (key.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        if (screen_key(s) == key) {
            return s;
        }
    }
    return nullptr;
}

namespace {

/// The operator's window, to tell its screen from the audience's.
QPointer<QWindow>& operator_window_ref()
{
    static QPointer<QWindow> window;
    return window;
}

}  // namespace

void DeviceCatalog::set_operator_window(QWindow* window)
{
    operator_window_ref() = window;
}

QScreen* DeviceCatalog::operator_screen()
{
    QWindow* window = operator_window_ref().data();
    return window != nullptr ? window->screen() : nullptr;
}

QScreen* DeviceCatalog::audience_screen()
{
    QScreen* own = operator_screen();
    const auto all = QGuiApplication::screens();
    for (QScreen* s : all) {
        if (s != own) {
            return s;
        }
    }
    return all.isEmpty() ? nullptr : all.front();
}

QScreen* DeviceCatalog::match_screen(const Destination& d)
{
    if (QScreen* by_key = screen_by_key(d.target_key)) {
        return by_key;
    }
    const auto screens = QGuiApplication::screens();
    if (const QString model = key_model(d.target_key); !model.isEmpty()) {
        QScreen* only = nullptr;
        int count = 0;
        for (QScreen* s : screens) {
            if (s->serialNumber().isEmpty() && screen_model(s) == model) {
                only = s;
                ++count;
            }
        }
        if (count == 1) {
            return only;
        }
    }
    if (!d.label.isEmpty()) {
        for (QScreen* s : screens) {
            if (s->name() == d.label) {
                return s;
            }
        }
    }
    if (!d.bounds.isEmpty()) {
        for (QScreen* s : screens) {
            if (bounds_string(s) == d.bounds) {
                return s;
            }
        }
    }
    return nullptr;
}

void DeviceCatalog::apply_screen(Destination& dest, const QScreen* screen)
{
    if (!screen) {
        return;
    }
    dest.target_key = screen_key(screen);
    dest.label = screen->name();
    dest.bounds = bounds_string(screen);
}

bool DeviceCatalog::opaque_name(const QString& name)
{
    const QString t = name.trimmed();
    if (t.isEmpty()) {
        return true;
    }
    // Wayland often exposes wl_output as a hex object id ("0x40a0").
    static const QRegularExpression hex_id(QStringLiteral("^0x[0-9A-Fa-f]+$"));
    return hex_id.match(t).hasMatch();
}

std::vector<DeviceCatalog::Screen> DeviceCatalog::screens()
{
    std::vector<Screen> out;
    const auto all = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    for (int i = 0; i < all.size(); ++i) {
        QScreen* s = all[i];
        const bool is_primary = (s == primary);
        out.push_back({
            .key = screen_key(s),
            .index = i + 1,
            .label = friendly_label(s, i + 1, is_primary),
            .name = s->name(),
            .primary = is_primary,
            .scale = s->devicePixelRatio(),
            .bounds = s->geometry(),
            .native = native_size(s),
        });
    }
    return out;
}

QString DeviceCatalog::camera_label(const QString& device_path)
{
    if (device_path.isEmpty()) {
        return {};
    }
    for (const auto& info : list_virtual_camera_devices()) {
        if (info.path == device_path) {
            return info.label;
        }
    }
    return device_path;
}

QString DeviceCatalog::bmd_device_label(const QString& device_id)
{
    for (const auto& info : bmd_list_devices()) {
        if (info.id == device_id) {
            return info.label;
        }
    }
    return {};
}

DeviceCatalog::Snapshot& DeviceCatalog::snapshot() const
{
    constexpr auto k_max_age = std::chrono::seconds(1);
    const auto now = std::chrono::steady_clock::now();
    if (!snapshot_ || now - snapshot_->taken > k_max_age) {
        snapshot_ = std::make_unique<Snapshot>();
        snapshot_->taken = now;
    }
    return *snapshot_;
}

QString DeviceCatalog::bmd_label_for(const Destination& dest, Snapshot& devices) const
{
    QString device;
    for (const auto& info : devices.bmd_devices()) {
        if (info.id == dest.target_key) {
            device = info.label;
            break;
        }
    }
    if (device.isEmpty()) {
        // Card unplugged, or a profile change took the sub-device away. Show
        // the name we last saw rather than an opaque handle.
        device = dest.label.isEmpty() ? dest.target_key : dest.label;
    }
    QString mode;
    for (const auto& m : devices.modes(dest.target_key)) {
        if (m.key == dest.mode_key) {
            mode = m.name;
            break;
        }
    }
    if (device.isEmpty()) {
        return mode;
    }
    return mode.isEmpty() ? device : QStringLiteral("%1 · %2").arg(device, mode);
}

QString DeviceCatalog::target_label(const Destination& d) const
{
    switch (d.kind) {
    case DestinationKind::Display: {
        const QString label = screen_label_for(d.target_key);
        return label.isEmpty() ? disconnected_screen_label(d.label, d.bounds) : label;
    }
    case DestinationKind::Ndi:
        return d.target_key;
    case DestinationKind::Bmd:
        return bmd_label_for(d, snapshot());
    case DestinationKind::Vcam:
        if (d.target_key.isEmpty()) {
            return d.label;
        }
        for (const auto& cam : snapshot().camera_devices()) {
            if (cam.path == d.target_key) {
                return cam.label;
            }
        }
        return d.target_key;
    case DestinationKind::Srt:
        return QString::fromStdString(stream::address_text(d.stream));
    case DestinationKind::Window:
        // Nothing to pick: it opens where the operator last left it.
        return QStringLiteral("Window");
    }
    return {};
}

bool DeviceCatalog::target_missing(const Destination& d) const
{
    switch (d.kind) {
    case DestinationKind::Display:
        return !d.target_key.isEmpty() && screen_by_key(d.target_key) == nullptr;
    case DestinationKind::Ndi:
        return !ndi_runtime_available() || d.target_key.trimmed().isEmpty();
    case DestinationKind::Bmd: {
        // Two different "missing" causes, and the UI says different things
        // about them: no driver at all, versus a driver with no card.
        const auto& cards = snapshot().bmd_devices();
        return !bmd_runtime_available() || d.target_key.isEmpty()
               || std::none_of(cards.begin(), cards.end(), [&d](const BmdDeviceInfo& card) {
                      return card.id == d.target_key;
                  });
    }
    case DestinationKind::Vcam: {
        if (d.target_key.isEmpty()) {
            return true;
        }
        const auto& cams = snapshot().camera_devices();
        const bool listed =
            std::any_of(cams.begin(), cams.end(), [&d](const VirtualCameraDeviceInfo& cam) {
                return cam.path == d.target_key;
            });
        // Not in the snapshot: ask the device node directly (cheap when absent).
        return !listed && !is_virtual_camera_device(d.target_key);
    }
    case DestinationKind::Srt:
        // Nothing to be plugged in: only FFmpeg's SRT can be missing.
        return !stream::srt_supported();
    case DestinationKind::Window:
        return false;
    }
    return false;
}

}  // namespace seventhslide::render
