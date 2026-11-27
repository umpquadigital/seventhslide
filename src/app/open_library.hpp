#pragma once

// The library that is open, and the registry it was opened from.
//
// Holds what lives exactly as long as one library: the database and the
// repositories the application keeps beside the controllers' own (settings,
// style profiles), the media folder, and the settings read from it. The
// registry of libraries, the destination store, the Audio Mixer's store, the
// console store and this computer's own settings (the machine store) outlive
// any one library.
//
// SessionFacade owns it and opens and closes it; controllers borrow it for
// the facts they read (where media lives, the settings, the profile service).
//
// Seam: Qt Core + Qt Sql through db/. Call on the GUI thread.

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/db/style_profile_repository.hpp>
#include <seventhslide/db/style_profiles.hpp>
#include <seventhslide/library/audio_mix_store.hpp>
#include <seventhslide/library/console_store.hpp>
#include <seventhslide/library/destination_store.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/library/machine_store.hpp>
#include <seventhslide/model/app_settings.hpp>
#include <seventhslide/model/machine_settings.hpp>
#include <seventhslide/model/output_config.hpp>

#include <QString>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

class OpenLibrary {
public:
    OpenLibrary() = default;
    OpenLibrary(const OpenLibrary&) = delete;
    OpenLibrary& operator=(const OpenLibrary&) = delete;
    OpenLibrary(OpenLibrary&&) = delete;
    OpenLibrary& operator=(OpenLibrary&&) = delete;
    ~OpenLibrary();

    // --- the registry (for the whole run) ---------------------------------
    /// Load the registry, the destination store, the Audio Mixer's store, the
    /// console store and the machine store rooted at ``data_dir``, with a
    /// default library when there is none.
    void load_registry(const std::filesystem::path& data_dir);
    /// Null until ``load_registry`` (an in-memory database in tests has none).
    [[nodiscard]] library::LibraryRegistry* registry() noexcept { return registry_.get(); }
    [[nodiscard]] const library::LibraryRegistry* registry() const noexcept { return registry_.get(); }
    [[nodiscard]] library::DestinationStore* destination_store() noexcept
    {
        return destination_store_.get();
    }
    /// Each library's Audio Mixer on this computer. Null until ``load_registry``.
    [[nodiscard]] library::AudioMixStore* audio_mix_store() noexcept
    {
        return audio_mix_store_.get();
    }
    /// How this computer's console is set up for each library (which stacks
    /// have a Preview). Null until ``load_registry``.
    [[nodiscard]] library::ConsoleStore* console_store() noexcept { return console_store_.get(); }
    [[nodiscard]] const library::ConsoleStore* console_store() const noexcept
    {
        return console_store_.get();
    }
    /// The library whose destinations the output host holds; saves go here
    /// even while a switch is changing the active one.
    [[nodiscard]] const std::string& destinations_library_id() const noexcept
    {
        return destinations_library_id_;
    }
    void set_destinations_library_id(std::string id) { destinations_library_id_ = std::move(id); }

    // --- the database ------------------------------------------------------
    [[nodiscard]] bool is_open() const noexcept { return db_.has_value(); }
    /// Null while no library is open.
    [[nodiscard]] db::Database* database() noexcept { return db_ ? &*db_ : nullptr; }
    [[nodiscard]] db::ConfigRepository* config() noexcept { return config_.get(); }
    [[nodiscard]] db::StyleProfileRepository* profile_repository() noexcept
    {
        return profiles_.get();
    }
    [[nodiscard]] const db::StyleProfileRepository* profile_repository() const noexcept
    {
        return profiles_.get();
    }
    [[nodiscard]] db::StyleProfileService* profile_service() noexcept
    {
        return profile_service_.get();
    }
    [[nodiscard]] const db::StyleProfileService* profile_service() const noexcept
    {
        return profile_service_.get();
    }

    /// Take ``db`` (schema already checked) and open the repositories over it.
    /// It opens writable.
    db::Database& open(db::Database db);
    /// Nothing is written to the open library here (a viewer's): its
    /// connection refuses every write. False while none is open.
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }
    /// Make the open library's connection refuse every write, or take them
    /// again. Throws ``db::DatabaseError`` when the connection cannot be set.
    void set_read_only(bool read_only);
    /// Start switching style profiles over ``songs`` and the live ``outputs``,
    /// and make sure one profile is active (not on a read-only library). Call
    /// once the outputs are loaded.
    void start_profiles(db::SongRepository& songs, std::vector<model::OutputConfig>& outputs);
    /// Make sure one profile is active: what ``start_profiles`` left undone
    /// on a library read-only then. Throws when the write fails.
    void seed_profiles();
    /// Stop the profile service. It reads the song repository, so this comes
    /// before the controllers let go of theirs.
    void stop_profiles() noexcept;
    /// Drop the repositories and the database. The settings keep their last
    /// values until the next library loads its own.
    void close() noexcept;

    // --- the media folder --------------------------------------------------
    /// Root the media files sit under (``images/``, ``videos/`` beneath it).
    [[nodiscard]] const std::filesystem::path& media_root() const noexcept { return media_root_; }
    void set_media_root(std::filesystem::path root);
    /// ``media_root`` for a caller on another thread (Qt's pixmap reader).
    [[nodiscard]] std::filesystem::path media_root_any_thread() const;
    /// Absolute path of ``filename`` under ``subdir`` of the media root, or
    /// empty when it is not there. Only the leaf of ``filename`` counts, but
    /// for a Freeform's own file (``freeform::own_media_of``), which is in its
    /// owner's folder; the result never leaves the media root (symlinks
    /// included). Safe to call from any thread.
    [[nodiscard]] std::filesystem::path resolve_media_path(std::string_view subdir,
                                                           std::string_view filename) const;

    // --- settings ----------------------------------------------------------
    /// The open library's application settings (defaults while none is open).
    [[nodiscard]] model::AppSettings& settings() noexcept { return settings_; }
    [[nodiscard]] const model::AppSettings& settings() const noexcept { return settings_; }
    /// Re-read the settings from the database (defaults without one).
    void load_settings();

    /// This computer's settings: the same whichever library is open. Defaults
    /// until ``load_registry``.
    [[nodiscard]] const model::MachineSettings& machine_settings() const noexcept
    {
        return machine_settings_;
    }
    /// Keep ``settings`` as this computer's. False when the machine store
    /// cannot be written, and then nothing changes. Without a store (an
    /// in-memory library in tests) they last the run.
    [[nodiscard]] bool save_machine_settings(model::MachineSettings settings);
    /// Null until ``load_registry``.
    [[nodiscard]] const library::MachineStore* machine_store() const noexcept
    {
        return machine_store_.get();
    }

    // --- what went wrong ---------------------------------------------------
    /// Why the last open failed, or a profile write that failed on the way
    /// out. Empty when nothing did.
    [[nodiscard]] const QString& last_error() const noexcept { return last_error_; }
    void set_last_error(QString message) { last_error_ = std::move(message); }

private:
    std::unique_ptr<library::DestinationStore> destination_store_;
    std::unique_ptr<library::AudioMixStore> audio_mix_store_;
    std::unique_ptr<library::ConsoleStore> console_store_;
    std::unique_ptr<library::MachineStore> machine_store_;
    std::unique_ptr<library::LibraryRegistry> registry_;
    std::string destinations_library_id_;

    std::optional<db::Database> db_;
    std::unique_ptr<db::ConfigRepository> config_;
    std::unique_ptr<db::StyleProfileRepository> profiles_;
    std::unique_ptr<db::StyleProfileService> profile_service_;
    bool read_only_ = false;

    std::filesystem::path media_root_;
    /// The media root as other threads read it, and its ``weakly_canonical``
    /// form, worked out once the folder exists rather than on every lookup.
    mutable std::mutex shared_root_mutex_;
    std::filesystem::path shared_root_;
    mutable std::filesystem::path shared_resolved_root_;
    model::AppSettings settings_;
    model::MachineSettings machine_settings_;
    QString last_error_;
};

}  // namespace seventhslide::app
