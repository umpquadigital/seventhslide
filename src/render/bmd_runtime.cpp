#include <seventhslide/render/bmd_runtime.hpp>

#include "bmd_internal.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

#if !defined(__APPLE__)
#include <dlfcn.h>
#include <link.h>
#endif

namespace seventhslide::render {
namespace {

using bmd::ComRef;

struct Runtime {
    bool available = false;
    bool probed = false;
    QString version;
    QString path;
};

std::mutex g_mutex;
Runtime g_runtime;

/// Where the driver library actually got loaded from, for support logs. The
/// SDK dispatch owns the handle and does not expose it, so ask the loader.
[[nodiscard]] QString driver_path()
{
#if defined(__APPLE__)
    // The SDK's dispatch loads the framework from this one place
    // (kDeckLinkAPI_BundlePath), and from nowhere else.
    return QStringLiteral("/Library/Frameworks/DeckLinkAPI.framework");
#else
    void* h = ::dlopen("libDeckLinkAPI.so", RTLD_NOLOAD | RTLD_LAZY);
    if (h == nullptr) {
        return {};
    }
    QString out;
    struct link_map* lm = nullptr;
    if (::dlinfo(h, RTLD_DI_LINKMAP, &lm) == 0 && lm != nullptr && lm->l_name != nullptr
        && lm->l_name[0] != '\0') {
        out = QString::fromUtf8(lm->l_name);
    }
    // NOLOAD still took a reference; drop ours and leave the dispatch's alone.
    ::dlclose(h);
    return out;
#endif
}

/// Caller holds g_mutex.
void probe_locked()
{
    g_runtime.probed = true;
    if (g_runtime.available) {
        return;  // Already up; never churn a live driver.
    }
    // The vendored dispatch loads libDeckLinkAPI.so (DeckLinkAPI.framework on
    // macOS) and returns nullptr when Desktop Video is not installed. That
    // NULL is the whole availability test.
    ComRef<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
    if (!it) {
        return;
    }
    g_runtime.available = true;

    ComRef<IDeckLinkAPIInformation> info(CreateDeckLinkAPIInformationInstance());
    if (info) {
        int64_t v = 0;
        if (info->GetInt(BMDDeckLinkAPIVersion, &v) == S_OK) {
            g_runtime.version = QStringLiteral("%1.%2.%3")
                                    .arg((v >> 24) & 0xff)
                                    .arg((v >> 16) & 0xff)
                                    .arg((v >> 8) & 0xff);
        }
    }
    g_runtime.path = driver_path();
}

void ensure_probed()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_runtime.probed) {
        probe_locked();
    }
}

}  // namespace

bool bmd_runtime_available()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.available;
}

QString bmd_runtime_version()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.version;
}

QString bmd_runtime_path()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.path;
}

void bmd_runtime_refresh()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    probe_locked();
}

QString bmd_missing_message()
{
    return QStringLiteral(
        "Blackmagic driver not found. Install the free Desktop Video software from "
        "blackmagicdesign.com/support, then reopen this dialog. SeventhSlide does not "
        "bundle it.");
}

QString bmd_no_device_message()
{
    return QStringLiteral(
        "No Blackmagic output device found. Desktop Video is installed, but no card "
        "with an output was detected. Connect a DeckLink or UltraStudio device, then "
        "reopen this dialog. On a card with switchable profiles, check that the "
        "sub-device is set to output.");
}

std::vector<BmdDeviceInfo> bmd_list_devices()
{
    std::vector<BmdDeviceInfo> out;
    if (!bmd_runtime_available()) {
        return out;
    }
    ComRef<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
    if (!it) {
        return out;
    }
    IDeckLink* raw = nullptr;
    while (it->Next(&raw) == S_OK && raw != nullptr) {
        ComRef<IDeckLink> device(raw);
        raw = nullptr;
        BmdDeviceInfo info = bmd::describe(device.get());
        if (info.can_output) {
            out.push_back(std::move(info));
        }
    }
    return out;
}

std::vector<BmdDisplayModeInfo> bmd_list_display_modes(const QString& device_id)
{
    std::vector<BmdDisplayModeInfo> out;
    if (!bmd_runtime_available()) {
        return out;
    }
    ComRef<IDeckLink> device = bmd::find_device(device_id);
    if (!device) {
        return out;
    }
    IDeckLinkOutput* raw_output = nullptr;
    if (device->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&raw_output)) != S_OK
        || raw_output == nullptr) {
        return out;
    }
    ComRef<IDeckLinkOutput> output(raw_output);

    ComRef<IDeckLinkDisplayModeIterator> modes;
    if (output->GetDisplayModeIterator(modes.receive()) != S_OK || !modes) {
        return out;
    }
    IDeckLinkDisplayMode* raw_mode = nullptr;
    while (modes->Next(&raw_mode) == S_OK && raw_mode != nullptr) {
        ComRef<IDeckLinkDisplayMode> mode(raw_mode);
        raw_mode = nullptr;

        BmdDisplayModeInfo info;
        bmd::SdkString s = nullptr;
        if (mode->GetName(&s) == S_OK) {
            info.name = bmd::adopt_string(s);
        }
        info.key = bmd::display_mode_key(mode->GetDisplayMode());
        info.width = int(mode->GetWidth());
        info.height = int(mode->GetHeight());
        BMDTimeValue duration = 0;
        BMDTimeScale scale = 0;
        if (mode->GetFrameRate(&duration, &scale) == S_OK) {
            info.frame_duration = duration;
            info.time_scale = scale;
        }
        const BMDFieldDominance dom = mode->GetFieldDominance();
        info.interlaced = dom == bmdLowerFieldFirst || dom == bmdUpperFieldFirst;
        if (info.name.isEmpty()) {
            info.name = QStringLiteral("%1×%2").arg(info.width).arg(info.height);
        }
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace seventhslide::render
