#include <seventhslide/render/ndi_runtime.hpp>

#include <seventhslide/render/ndi_abi.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStringList>

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace seventhslide::render {
namespace {

#if defined(_WIN32)
using LibHandle = HMODULE;
constexpr LibHandle k_null_handle = nullptr;
#else
using LibHandle = void*;
constexpr LibHandle k_null_handle = nullptr;
#endif

/// Env vars the NDI installers set, newest first.
const char* const k_runtime_env_vars[] = {
    "NDI_RUNTIME_DIR_V6",
    "NDI_RUNTIME_DIR_V5",
    "NDI_RUNTIME_DIR_V4",
};

/// Bare library names, newest ABI first. Resolved through the normal loader
/// search path (ldconfig / DYLD / PATH).
[[nodiscard]] QStringList library_names()
{
#if defined(_WIN32)
    return {QStringLiteral("Processing.NDI.Lib.x64.dll")};
#elif defined(__APPLE__)
    return {QStringLiteral("libndi.dylib"),
            QStringLiteral("libndi.6.dylib"),
            QStringLiteral("libndi.5.dylib"),
            QStringLiteral("libndi.4.dylib")};
#else
    return {QStringLiteral("libndi.so.6"),
            QStringLiteral("libndi.so.5"),
            QStringLiteral("libndi.so.4"),
            QStringLiteral("libndi.so")};
#endif
}

/// Directories the installers use when they do not touch the loader path.
[[nodiscard]] QStringList extra_directories()
{
#if defined(_WIN32)
    return {};
#elif defined(__APPLE__)
    return {QStringLiteral("/usr/local/lib"),
            QStringLiteral("/opt/homebrew/lib"),
            QStringLiteral("/Library/NDI SDK for Apple/lib/macOS"),
            QStringLiteral("/Library/Application Support/NewTek/NDI")};
#else
    return {QStringLiteral("/usr/local/lib"),
            QStringLiteral("/usr/lib64"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu"),
            QStringLiteral("/opt/ndi/lib")};
#endif
}

[[nodiscard]] QStringList candidate_paths()
{
    QStringList out;
    const auto names = library_names();
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 1. Wherever the installer said it put things.
    for (const char* var : k_runtime_env_vars) {
        const QString dir = env.value(QString::fromUtf8(var)).trimmed();
        if (dir.isEmpty()) {
            continue;
        }
        for (const QString& name : names) {
            out.push_back(QDir(dir).filePath(name));
        }
    }
#if defined(__APPLE__)
    // 2. A copy put in the app's own Contents/Frameworks (dlopen of a bare
    //    name never looks there).
    if (const QString exe = QCoreApplication::applicationDirPath(); !exe.isEmpty()) {
        const QString frameworks = QDir(exe).absoluteFilePath(QStringLiteral("../Frameworks"));
        for (const QString& name : names) {
            out.push_back(QDir::cleanPath(QDir(frameworks).filePath(name)));
        }
    }
#endif
    // 3. The normal loader search path.
    for (const QString& name : names) {
        out.push_back(name);
    }
    // 4. Well-known install directories.
    for (const QString& dir : extra_directories()) {
        for (const QString& name : names) {
            out.push_back(QDir(dir).filePath(name));
        }
    }
    out.removeDuplicates();
    return out;
}

[[nodiscard]] LibHandle open_library(const QString& path)
{
#if defined(_WIN32)
    return ::LoadLibraryW(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    return ::dlopen(path.toUtf8().constData(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void close_library(LibHandle handle)
{
    if (handle == k_null_handle) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(handle);
#else
    ::dlclose(handle);
#endif
}

template <typename Fn>
[[nodiscard]] Fn resolve(LibHandle handle, const char* name)
{
#if defined(_WIN32)
    // FARPROC is a function pointer of its own type: through void(*)() first,
    // or MinGW's -Wcast-function-type (in -Wextra) warns.
    return reinterpret_cast<Fn>(reinterpret_cast<void (*)()>(::GetProcAddress(handle, name)));
#else
    return reinterpret_cast<Fn>(::dlsym(handle, name));
#endif
}

/// Best-effort absolute path of a loaded handle, for support logs.
[[nodiscard]] QString resolved_path(LibHandle handle, const QString& tried)
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    if (::GetModuleFileNameW(handle, buf, MAX_PATH) > 0) {
        return QString::fromWCharArray(buf);
    }
#else
    Q_UNUSED(handle);
#endif
    const QFileInfo info(tried);
    return info.isAbsolute() ? info.absoluteFilePath() : tried;
}

struct Runtime {
    LibHandle handle = k_null_handle;
    ndi::Api api;
    bool available = false;
    bool probed = false;
    QString version;
    QString path;
    /// What it was set up to send over as it loaded.
    NdiTransport transport = NdiTransport::Automatic;
};

/// What the next load is set up with (ndi_set_transport).
struct TransportWanted {
    NdiTransport transport = NdiTransport::Automatic;
    QString config_dir;
    /// ``NDI_CONFIG_DIR`` as the app found it, once looked at: the operator's
    /// own, put back whenever the app's is not wanted.
    bool looked = false;
    std::optional<QByteArray> own_dir;
};

/// The variable the runtime reads its configuration's directory from.
constexpr const char* k_config_dir_var = "NDI_CONFIG_DIR";
constexpr const char* k_config_file = "ndi-config.v1.json";

std::mutex g_mutex;
Runtime g_runtime;
TransportWanted g_wanted;

/// The operator's own configuration file, where the runtime would look.
[[nodiscard]] QString own_config_file(const std::optional<QByteArray>& own_dir)
{
    if (own_dir && !own_dir->isEmpty()) {
        return QDir(QString::fromLocal8Bit(*own_dir)).filePath(QLatin1String(k_config_file));
    }
#if defined(_WIN32)
    return QDir(qEnvironmentVariable("ProgramData"))
        .filePath(QStringLiteral("NDI/") + QLatin1String(k_config_file));
#else
    return QDir::home().filePath(QStringLiteral(".ndi/") + QLatin1String(k_config_file));
#endif
}

/// Point the runtime about to load at the configuration ``g_wanted`` asks
/// for, and say what it will send over. Caller holds g_mutex.
[[nodiscard]] NdiTransport set_up_transport_locked()
{
    if (!g_wanted.looked) {
        g_wanted.looked = true;
        if (qEnvironmentVariableIsSet(k_config_dir_var)) {
            g_wanted.own_dir = qgetenv(k_config_dir_var);
        }
    }
    if (g_wanted.own_dir) {
        qputenv(k_config_dir_var, *g_wanted.own_dir);
    } else {
        qunsetenv(k_config_dir_var);
    }
    if (g_wanted.transport == NdiTransport::Automatic || g_wanted.config_dir.isEmpty()) {
        return NdiTransport::Automatic;
    }
    QFile own(own_config_file(g_wanted.own_dir));
    const QByteArray own_json = own.open(QIODevice::ReadOnly) ? own.readAll() : QByteArray{};
    QSaveFile out(QDir(g_wanted.config_dir).filePath(QLatin1String(k_config_file)));
    if (!QDir().mkpath(g_wanted.config_dir) || !out.open(QIODevice::WriteOnly)
        || out.write(ndi_config_sending_over(g_wanted.transport, own_json)) < 0
        || !out.commit()) {
        return NdiTransport::Automatic;
    }
    qputenv(k_config_dir_var, QDir::toNativeSeparators(g_wanted.config_dir).toLocal8Bit());
    return g_wanted.transport;
}

/// Resolve every entry point we need. Returns false if any required symbol is
/// missing, which means this is not a library we can drive.
[[nodiscard]] bool bind_api(LibHandle handle, ndi::Api* api)
{
    api->initialize = resolve<bool (*)()>(handle, "NDIlib_initialize");
    api->destroy = resolve<void (*)()>(handle, "NDIlib_destroy");
    api->version = resolve<const char* (*)()>(handle, "NDIlib_version");
    api->is_supported_CPU = resolve<bool (*)()>(handle, "NDIlib_is_supported_CPU");
    api->send_create =
        resolve<ndi::send_instance_t (*)(const ndi::send_create_t*)>(handle, "NDIlib_send_create");
    api->send_destroy =
        resolve<void (*)(ndi::send_instance_t)>(handle, "NDIlib_send_destroy");
    api->send_send_video_v2 =
        resolve<void (*)(ndi::send_instance_t, const ndi::video_frame_v2_t*)>(
            handle, "NDIlib_send_send_video_v2");
    api->send_send_video_async_v2 =
        resolve<void (*)(ndi::send_instance_t, const ndi::video_frame_v2_t*)>(
            handle, "NDIlib_send_send_video_async_v2");

    // Optional: present since NDI 3, but nullptr-checked at the call sites so a
    // stripped build still streams video.
    api->send_get_no_connections =
        resolve<int (*)(ndi::send_instance_t, std::uint32_t)>(
            handle, "NDIlib_send_get_no_connections");
    api->send_get_tally =
        resolve<bool (*)(ndi::send_instance_t, ndi::tally_t*, std::uint32_t)>(
            handle, "NDIlib_send_get_tally");

    // Optional: without it the destination streams pictures only.
    api->send_send_audio_v3 =
        resolve<void (*)(ndi::send_instance_t, const ndi::audio_frame_v3_t*)>(
            handle, "NDIlib_send_send_audio_v3");

    // Optional, all together: finding and receiving sources (step 5's live
    // sources). A runtime without them still sends.
    api->find_create_v2 = resolve<ndi::find_instance_t (*)(const ndi::find_create_t*)>(
        handle, "NDIlib_find_create_v2");
    api->find_destroy = resolve<void (*)(ndi::find_instance_t)>(handle, "NDIlib_find_destroy");
    api->find_get_current_sources =
        resolve<const ndi::source_t* (*)(ndi::find_instance_t, std::uint32_t*)>(
            handle, "NDIlib_find_get_current_sources");
    api->recv_create_v3 = resolve<ndi::recv_instance_t (*)(const ndi::recv_create_v3_t*)>(
        handle, "NDIlib_recv_create_v3");
    api->recv_destroy = resolve<void (*)(ndi::recv_instance_t)>(handle, "NDIlib_recv_destroy");
    api->recv_capture_v3 =
        resolve<std::int32_t (*)(ndi::recv_instance_t, ndi::video_frame_v2_t*,
                                 ndi::audio_frame_v3_t*, ndi::metadata_frame_t*, std::uint32_t)>(
            handle, "NDIlib_recv_capture_v3");
    api->recv_free_video_v2 = resolve<void (*)(ndi::recv_instance_t, const ndi::video_frame_v2_t*)>(
        handle, "NDIlib_recv_free_video_v2");
    api->recv_free_audio_v3 = resolve<void (*)(ndi::recv_instance_t, const ndi::audio_frame_v3_t*)>(
        handle, "NDIlib_recv_free_audio_v3");
    api->recv_get_no_connections =
        resolve<int (*)(ndi::recv_instance_t)>(handle, "NDIlib_recv_get_no_connections");

    return api->initialize != nullptr && api->destroy != nullptr && api->version != nullptr
           && api->is_supported_CPU != nullptr && api->send_create != nullptr
           && api->send_destroy != nullptr && api->send_send_video_v2 != nullptr
           && api->send_send_video_async_v2 != nullptr;
}

/// Caller holds g_mutex.
void probe_locked()
{
    g_runtime.probed = true;
    if (g_runtime.available) {
        return;  // Already loaded; never churn a live runtime.
    }
    const NdiTransport transport = set_up_transport_locked();
    for (const QString& path : candidate_paths()) {
        LibHandle handle = open_library(path);
        if (handle == k_null_handle) {
            continue;
        }
        ndi::Api api;
        if (!bind_api(handle, &api)) {
            close_library(handle);
            continue;
        }
        // is_supported_CPU guards against the runtime aborting on older CPUs
        // (NDI requires SSE4.2). initialize() also returns false there.
        if (!api.is_supported_CPU() || !api.initialize()) {
            close_library(handle);
            continue;
        }
        g_runtime.handle = handle;
        g_runtime.api = api;
        g_runtime.available = true;
        g_runtime.transport = transport;
        g_runtime.path = resolved_path(handle, path);
        const char* v = api.version();
        g_runtime.version = v != nullptr ? QString::fromUtf8(v).trimmed() : QString{};
        return;
    }
}

void ensure_probed()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_runtime.probed) {
        probe_locked();
    }
}

}  // namespace

bool ndi_runtime_available()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.available;
}

QString ndi_runtime_version()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.version;
}

QString ndi_runtime_path()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.path;
}

void ndi_runtime_refresh()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    probe_locked();
}

void ndi_set_transport(NdiTransport transport, const QString& config_dir)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted.transport = transport;
    g_wanted.config_dir = config_dir;
}

NdiTransport ndi_transport_in_effect()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.available ? g_runtime.transport : g_wanted.transport;
}

QByteArray ndi_config_sending_over(NdiTransport transport, const QByteArray& own)
{
    // What is there already is kept; a file that is not a JSON object is
    // written afresh.
    auto root = nlohmann::json::parse(own.constData(), own.constData() + own.size(), nullptr, false);
    if (!root.is_object()) {
        root = nlohmann::json::object();
    }
    const auto object_at = [](nlohmann::json& parent, const char* key) -> nlohmann::json& {
        auto& child = parent[key];
        if (!child.is_object()) {
            child = nlohmann::json::object();
        }
        return child;
    };
    auto& ndi = object_at(root, "ndi");
    // ``ndi.<way>.send.enable``, as the runtime reads it: "unicast" is its
    // plain UDP and "tcp" its multi-TCP. Only one way is left on, so a
    // receiver cannot settle on another: with none, it takes the single TCP
    // connection.
    const auto send_over = [&](const char* way, bool on) {
        object_at(object_at(ndi, way), "send")["enable"] = on;
    };
    send_over("rudp", transport == NdiTransport::ReliableUdp);
    send_over("unicast", transport == NdiTransport::Udp);
    send_over("multicast", false);
    send_over("tcp", transport == NdiTransport::MultiTcp);
    return QByteArray::fromStdString(root.dump(4));
}

const ndi::Api* ndi_api()
{
    ensure_probed();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runtime.available ? &g_runtime.api : nullptr;
}

QString ndi_missing_message()
{
#if defined(_WIN32)
    return QStringLiteral(
        "NDI runtime not found. Install the free NDI Tools (or the NDI SDK) from "
        "ndi.video, then reopen this dialog. SeventhSlide does not bundle NDI.");
#elif defined(__APPLE__)
    return QStringLiteral(
        "NDI runtime not found. Install the free NDI Tools (or the NDI SDK) from "
        "ndi.video, then reopen this dialog. SeventhSlide does not bundle NDI.");
#else
    return QStringLiteral(
        "NDI runtime not found. Install the NDI SDK for Linux from ndi.video and make "
        "sure libndi.so is on the library path, then reopen this dialog. SeventhSlide "
        "does not bundle NDI. For other machines to discover this source, avahi-daemon "
        "must also be running.");
#endif
}

}  // namespace seventhslide::render
