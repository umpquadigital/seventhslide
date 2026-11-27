#pragma once

// Shared DeckLink internals for bmd_runtime.cpp and bmd_output.cpp.
//
// PRIVATE to src/render - deliberately not under include/. It pulls in the
// vendored SDK headers, and nothing outside this library may see those (see
// bmd_runtime.hpp). Header-only so there is no second translation unit to keep
// in step.

#include <seventhslide/render/bmd_runtime.hpp>

#include "DeckLinkAPI.h"

#include <QString>

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace seventhslide::render::bmd {

// SDK 11.x renamed IDeckLinkAttributes to IDeckLinkProfileAttributes when
// profiles arrived. Aliased here so the upgrade is a two-line change - see
// third_party/decklink/README.md.
using Attributes = IDeckLinkAttributes;
#define SEVENTHSLIDE_IID_DECKLINK_ATTRIBUTES IID_IDeckLinkAttributes

/// Minimal COM refcount guard. DeckLink hands back raw AddRef'd pointers and
/// these paths have several early exits; releasing by hand in each one is how
/// leaks get in.
template <typename T>
class ComRef {
public:
    ComRef() = default;
    explicit ComRef(T* p) noexcept : p_(p) {}
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;
    ComRef(ComRef&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ComRef& operator=(ComRef&& o) noexcept
    {
        if (this != &o) {
            reset();
            p_ = std::exchange(o.p_, nullptr);
        }
        return *this;
    }
    ~ComRef() { reset(); }

    void reset() noexcept
    {
        if (p_ != nullptr) {
            p_->Release();
            p_ = nullptr;
        }
    }
    [[nodiscard]] T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    /// For the ``**`` out-parameters the SDK uses.
    T** receive() noexcept
    {
        reset();
        return &p_;
    }

private:
    T* p_ = nullptr;
};

/// IUnknown's IID, for a callback's QueryInterface: the Linux SDK's COM shim
/// names it IID_IUnknown, macOS's CFPlugInCOM gives it as a CFUUID.
[[nodiscard]] inline REFIID iid_unknown()
{
#if defined(__APPLE__)
    return CFUUIDGetUUIDBytes(IUnknownUUID);
#else
    return IID_IUnknown;
#endif
}

/// The type of the strings the SDK hands out: a malloc'd ``const char*`` on
/// Linux, a ``CFStringRef`` on macOS (Windows' ``BSTR`` when it is wired up).
#if defined(__APPLE__)
using SdkString = CFStringRef;
#else
using SdkString = const char*;
#endif

/**
 * Adopt a string the SDK just handed us.
 *
 * Platform-dependent and easy to get wrong: Linux yields a malloc'd
 * ``const char*`` the caller frees, macOS a ``CFStringRef`` to CFRelease,
 * Windows a ``BSTR`` to SysFreeString. Every SDK string goes through here so
 * the ownership rule lives in one place.
 */
[[nodiscard]] inline QString adopt_string(SdkString s)
{
    if (s == nullptr) {
        return {};
    }
#if defined(__APPLE__)
    const QString out = QString::fromCFString(s);
    CFRelease(s);
    return out;
#else
    const QString out = QString::fromUtf8(s);
    std::free(const_cast<char*>(s));
    return out;
#endif
}

/// Four-character code as a stable, persistable key ("Hi59").
[[nodiscard]] inline QString fourcc_key(std::uint32_t code)
{
    const char chars[4] = {
        static_cast<char>((code >> 24) & 0xff),
        static_cast<char>((code >> 16) & 0xff),
        static_cast<char>((code >> 8) & 0xff),
        static_cast<char>(code & 0xff),
    };
    return QString::fromLatin1(chars, 4);
}

/// Inverse of ``fourcc_key``. 0 when the key is not a four-character code.
[[nodiscard]] inline std::uint32_t fourcc_from_key(const QString& key)
{
    const QByteArray raw = key.toLatin1();
    if (raw.size() != 4) {
        return 0;
    }
    return (std::uint32_t(std::uint8_t(raw[0])) << 24)
           | (std::uint32_t(std::uint8_t(raw[1])) << 16)
           | (std::uint32_t(std::uint8_t(raw[2])) << 8)
           | std::uint32_t(std::uint8_t(raw[3]));
}

// A display mode is a four-character code. The Linux and macOS SDKs declare
// BMDDisplayMode as a uint32_t, the Windows one (from its IDL) as an enum, so
// the conversions live here and cast only where the type needs it.

/// ``mode`` as its persistable key.
[[nodiscard]] inline QString display_mode_key(BMDDisplayMode mode)
{
    const std::uint32_t code = mode;  // an unscoped enum converts implicitly too
    return fourcc_key(code);
}

/// Inverse of ``display_mode_key``. A template so the cast an enum needs is
/// never compiled where the mode is already an integer.
template <typename Mode = BMDDisplayMode>
[[nodiscard]] Mode display_mode_from_key(const QString& key)
{
    const std::uint32_t code = fourcc_from_key(key);
    if constexpr (std::is_enum_v<Mode>) {
        return static_cast<Mode>(code);
    } else {
        return code;
    }
}

/// Fill the attribute-derived fields of ``out``. A missing attribute is not an
/// error: older devices simply do not carry all of them.
inline void read_attributes(IDeckLink* device, BmdDeviceInfo* out)
{
    Attributes* raw = nullptr;
    if (device->QueryInterface(SEVENTHSLIDE_IID_DECKLINK_ATTRIBUTES,
                               reinterpret_cast<void**>(&raw))
            != S_OK
        || raw == nullptr) {
        return;
    }
    ComRef<Attributes> attrs(raw);

    int64_t io = 0;
    if (attrs->GetInt(BMDDeckLinkVideoIOSupport, &io) == S_OK) {
        out->can_output = (io & bmdDeviceSupportsPlayback) != 0;
        out->can_capture = (io & bmdDeviceSupportsCapture) != 0;
    }

    bool flag = false;
    if (attrs->GetFlag(BMDDeckLinkSupportsInternalKeying, &flag) == S_OK) {
        out->supports_internal_key = flag;
    }
    if (attrs->GetFlag(BMDDeckLinkSupportsExternalKeying, &flag) == S_OK) {
        out->supports_external_key = flag;
    }

    // Identity, best first. Never the enumeration index: it reshuffles on
    // hot-plug and when a multi-profile card changes profile.
    SdkString handle = nullptr;
    if (attrs->GetString(BMDDeckLinkDeviceHandle, &handle) == S_OK) {
        out->id = adopt_string(handle);
    }
    if (out->id.isEmpty()) {
        int64_t persistent = 0;
        if (attrs->GetInt(BMDDeckLinkPersistentID, &persistent) == S_OK && persistent != 0) {
            out->id = QStringLiteral("persistent:%1").arg(persistent, 0, 16);
        }
    }
}

/// Names + identity + capabilities for one device.
[[nodiscard]] inline BmdDeviceInfo describe(IDeckLink* device)
{
    BmdDeviceInfo info;
    SdkString s = nullptr;
    if (device->GetDisplayName(&s) == S_OK) {
        info.name = adopt_string(s);
    }
    s = nullptr;
    if (device->GetModelName(&s) == S_OK) {
        info.model = adopt_string(s);
    }
    read_attributes(device, &info);

    // Some devices report no IO-support attribute at all. Fall back to the
    // question we actually care about: can this thing be an output?
    if (!info.can_output) {
        IDeckLinkOutput* output = nullptr;
        if (device->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&output)) == S_OK
            && output != nullptr) {
            info.can_output = true;
            output->Release();
        }
    }
    if (!info.can_capture) {
        IDeckLinkInput* input = nullptr;
        if (device->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(&input)) == S_OK
            && input != nullptr) {
            info.can_capture = true;
            input->Release();
        }
    }
    if (info.name.isEmpty()) {
        info.name = info.model;
    }
    if (info.id.isEmpty()) {
        info.id = info.name;
    }
    info.label = info.model.isEmpty() || info.model == info.name
                     ? info.name
                     : QStringLiteral("%1 · %2").arg(info.name, info.model);
    return info;
}

/// The device whose identity matches ``device_id``, still AddRef'd. Null when
/// it is gone - an unplugged card, or a profile change that took the
/// sub-device away.
[[nodiscard]] inline ComRef<IDeckLink> find_device(const QString& device_id)
{
    if (device_id.isEmpty()) {
        return {};
    }
    ComRef<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
    if (!it) {
        return {};
    }
    IDeckLink* raw = nullptr;
    while (it->Next(&raw) == S_OK && raw != nullptr) {
        ComRef<IDeckLink> device(raw);
        raw = nullptr;
        if (describe(device.get()).id == device_id) {
            return device;
        }
    }
    return {};
}

}  // namespace seventhslide::render::bmd
