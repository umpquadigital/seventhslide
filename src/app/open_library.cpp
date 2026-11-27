#include "open_library.hpp"

#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/util/leaf_filename.hpp>

#include <system_error>
#include <utility>

namespace seventhslide::app {

OpenLibrary::~OpenLibrary()
{
    stop_profiles();
    close();
}

void OpenLibrary::load_registry(const std::filesystem::path& data_dir)
{
    destination_store_ = std::make_unique<library::DestinationStore>(data_dir);
    audio_mix_store_ = std::make_unique<library::AudioMixStore>(data_dir);
    console_store_ = std::make_unique<library::ConsoleStore>(data_dir);
    machine_store_ = std::make_unique<library::MachineStore>(data_dir);
    machine_store_->load();
    machine_settings_ = model::MachineSettings::from_json(
        machine_store_->settings().value_or(nlohmann::json::object()));
    registry_ = std::make_unique<library::LibraryRegistry>(data_dir);
    registry_->load();
    registry_->ensure_default();
}

db::Database& OpenLibrary::open(db::Database db)
{
    // A service still running reads the repositories replaced below.
    stop_profiles();
    db_ = std::move(db);
    read_only_ = false;
    config_ = std::make_unique<db::ConfigRepository>(*db_);
    profiles_ = std::make_unique<db::StyleProfileRepository>(*db_);
    return *db_;
}

void OpenLibrary::set_read_only(bool read_only)
{
    if (db_) {
        db_->exec(read_only ? "PRAGMA query_only = 1" : "PRAGMA query_only = 0");
    }
    read_only_ = read_only && db_.has_value();
}

void OpenLibrary::start_profiles(db::SongRepository& songs,
                                 std::vector<model::OutputConfig>& outputs)
{
    profile_service_ =
        std::make_unique<db::StyleProfileService>(*profiles_, songs, *config_, outputs);
    // A read-only library keeps the profiles its editors wrote.
    if (!read_only_) {
        profile_service_->ensure_seeded();
    }
}

void OpenLibrary::seed_profiles()
{
    if (profile_service_) {
        profile_service_->ensure_seeded();
    }
}

void OpenLibrary::stop_profiles() noexcept
{
    profile_service_.reset();
}

void OpenLibrary::close() noexcept
{
    profile_service_.reset();
    profiles_.reset();
    config_.reset();
    db_.reset();
    read_only_ = false;
}

void OpenLibrary::set_media_root(std::filesystem::path root)
{
    {
        const std::lock_guard lock(shared_root_mutex_);
        shared_root_ = root;
        shared_resolved_root_.clear();
    }
    media_root_ = std::move(root);
}

std::filesystem::path OpenLibrary::media_root_any_thread() const
{
    const std::lock_guard lock(shared_root_mutex_);
    return shared_root_;
}

std::filesystem::path OpenLibrary::resolve_media_path(std::string_view subdir,
                                                      std::string_view filename) const
{
    namespace fs = std::filesystem;
    fs::path root;
    fs::path resolved_root;
    {
        const std::lock_guard lock(shared_root_mutex_);
        root = shared_root_;
        if (shared_resolved_root_.empty() && !root.empty()) {
            std::error_code ec;
            if (fs::is_directory(root, ec)) {
                shared_resolved_root_ = fs::weakly_canonical(root, ec);
                if (ec) {
                    shared_resolved_root_.clear();
                }
            }
        }
        resolved_root = shared_resolved_root_;
    }
    // No resolved root: the folder does not exist, so nothing is under it.
    if (resolved_root.empty() || subdir.empty()) {
        return {};
    }
    fs::path candidate;
    if (const auto own = freeform::own_media_of(filename)) {
        // A Freeform's own file, in its owner's folder whatever ``subdir`` says.
        candidate = freeform::own_media_path(root, *own);
    } else {
        const auto leaf = util::leaf_filename(filename);
        if (leaf.empty()) {
            return {};
        }
        candidate = root / std::string(subdir) / leaf;
    }
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec)
        || !platform::Paths::path_is_within_resolved(candidate, resolved_root)) {
        return {};
    }
    return candidate;
}

bool OpenLibrary::save_machine_settings(model::MachineSettings settings)
{
    if (machine_store_ && !machine_store_->save_settings(settings.to_json())) {
        return false;
    }
    machine_settings_ = std::move(settings);
    return true;
}

void OpenLibrary::load_settings()
{
    settings_ = config_ ? model::AppSettings::from_json(config_->load_settings())
                        : model::AppSettings{};
}

}  // namespace seventhslide::app
