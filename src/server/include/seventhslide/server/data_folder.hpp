#pragma once

// Which folder a server's data is in, and who may use it. Without ``--data`` a
// command goes by what the setup left in a small system-wide file (``{"data":
// "/var/lib/seventhslide-server"}``: packaging/server/server.json), so an operator
// types no path, and otherwise by this user's default.
//
// Seam: the file system, and the platform's users; plain C++20 otherwise.

#include <filesystem>
#include <optional>
#include <string>

namespace seventhslide::server {

/// Where the setup says the server's data is: /etc/seventhslide/server.json, on
/// Windows under %ProgramData%.
[[nodiscard]] std::filesystem::path data_folder_pointer();

/// The folder ``pointer`` names; ``fallback`` when there is no such file. Throws
/// ``std::runtime_error`` when one is there and says nothing usable: going by the
/// default instead would quietly be another server, with another key.
[[nodiscard]] std::filesystem::path data_folder_from(const std::filesystem::path& pointer,
                                                     const std::filesystem::path& fallback);

/// Ready to keep a server's data: made if it was not there, this user's alone.
/// Throws ``std::runtime_error``, in words for the operator, when it cannot be made
/// or is the data folder of a SeventhSlide used at the screen.
void prepare_data_folder(const std::filesystem::path& data);

/// Why the user ``me`` should not run a command on a folder ``owner`` owns: root
/// that writes there leaves files the service cannot open, and anyone else cannot
/// get in. ``writes``: whether this command writes there itself, rather than ask the
/// running server. Nothing when it may; else what to say, naming ``right_command``.
[[nodiscard]] std::optional<std::string> wrong_user(unsigned me, unsigned owner, const std::string& owner_name,
                                                    bool writes, const std::string& right_command);

/// ``wrong_user`` for this process and ``data`` as it is; nothing for a folder not
/// there yet, or where the platform has no such owners.
[[nodiscard]] std::optional<std::string> user_refused(const std::filesystem::path& data, bool writes,
                                                      const std::string& command_after_server);

}  // namespace seventhslide::server
