#include "seventhslide/server/data_folder.hpp"

#include "seventhslide/library/library.hpp"
#include "seventhslide/util/read_file.hpp"

#include <QString>
#include <QtGlobal>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <system_error>

#ifndef Q_OS_WIN
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seventhslide::server {

namespace fs = std::filesystem;

fs::path data_folder_pointer()
{
#ifdef Q_OS_WIN
    const auto program_data = qEnvironmentVariable("ProgramData", QStringLiteral("C:\\ProgramData"));
    return fs::path(program_data.toStdWString()) / "SeventhSlide" / "server.json";
#else
    return "/etc/seventhslide/server.json";
#endif
}

fs::path data_folder_from(const fs::path& pointer, const fs::path& fallback)
{
    std::error_code ec;
    if (!fs::exists(pointer, ec)) {
        return fallback;
    }
    const auto text = util::read_file(pointer);
    const auto j = text ? nlohmann::json::parse(*text, nullptr, /*allow_exceptions=*/false) : nlohmann::json(nullptr);
    const auto data = j.is_object() ? j.find("data") : j.end();
    if (!j.is_object() || data == j.end() || !data->is_string() || data->get<std::string>().empty()) {
        throw std::runtime_error(pointer.string() + " does not say where the server's data is. It should "
                                 "read {\"data\": \"/the/folder\"}; or give the folder with --data.");
    }
    return data->get<std::string>();
}

void prepare_data_folder(const fs::path& data)
{
    const auto shown = data.string();
    std::error_code ec;
    fs::create_directories(data, ec);
    if (ec || !fs::is_directory(data)) {
        throw std::runtime_error("The server's data folder could not be made: " + shown);
    }
    if (fs::exists(data / library::k_registry_filename, ec)) {
        // The app's: its key would be the app's, and its libraries its own.
        throw std::runtime_error(shown + " is SeventhSlide's own data folder, not a server's. Give "
                                         "the server a folder of its own with --data.");
    }
    // Its key and its libraries are this user's alone.
    fs::permissions(data, fs::perms::owner_all, fs::perm_options::replace, ec);
}

std::optional<std::string> wrong_user(unsigned me, unsigned owner, const std::string& owner_name, bool writes,
                                      const std::string& right_command)
{
    if (me == owner || (me == 0 && !writes)) {
        return std::nullopt;
    }
    if (me == 0) {
        return "As root this would leave files in the server's data folder that it cannot open, and "
               "stop the service. Run it as the user that owns the folder:\n\n    "
               + right_command;
    }
    return "The server's data folder belongs to " + owner_name + ". Run it as that user:\n\n    " + right_command;
}

std::optional<std::string> user_refused(const fs::path& data, bool writes, const std::string& command_after_server)
{
#ifdef Q_OS_WIN
    (void)data;
    (void)writes;
    (void)command_after_server;
    return std::nullopt;
#else
    struct stat about {};
    if (::stat(data.c_str(), &about) != 0) {
        return std::nullopt;
    }
    const auto* owner = ::getpwuid(about.st_uid);
    const std::string name = owner != nullptr ? owner->pw_name : "#" + std::to_string(about.st_uid);
    return wrong_user(static_cast<unsigned>(::geteuid()), static_cast<unsigned>(about.st_uid), name, writes,
                      "sudo -u " + name + " seventhslide --server " + command_after_server);
#endif
}

}  // namespace seventhslide::server
