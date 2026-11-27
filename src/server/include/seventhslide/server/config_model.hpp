#pragma once

// What ``seventhslide --server config`` knows and can do, with no screen in it, so
// a test can do all of it (``config_screen.hpp`` draws it). It is a client of the
// server's commands like any other, and keeps nothing: a ``Snapshot`` is how things
// stood when asked, and every action asks again. So it may be called off the
// screen's thread, and a password goes no further than the request it is for.
//
// What can be done is told as forms, in the words of the Sync page: a form says what
// to ask, and what to do with the answers, and the screen draws any form.
//
// Seam: the commands' socket through ``AskServer``; nlohmann/json.

#include "seventhslide/server/settings_edit.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace seventhslide::server {

/// What came of something asked for.
struct Done {
    bool ok = false;
    /// What to tell the operator, either way.
    std::string message;
    /// To copy: an invite, a recovery code. Empty for none.
    std::string code;
};

struct FormField {
    enum class Kind {
        Text,
        Secret,  ///< A password: not shown, and never kept.
    };
    Kind kind = Kind::Text;
    std::string label;
    std::string hint;
    /// What it holds to begin with.
    std::string value;
};

struct Form {
    std::string title;
    std::string intro;
    std::vector<FormField> fields;
    /// The button that does it.
    std::string go;
    /// The answers, in the order of ``fields``.
    std::function<Done(const std::vector<std::string>& answers)> submit;
};

struct Action {
    std::string label;
    std::string detail;
    Form form;
};

/// How things stood when asked.
struct Snapshot {
    /// A server took the request.
    bool running = false;
    /// One is there and did not answer, or the settings could not be read.
    std::string problem;
    /// ``status``'s reply; null when none is running.
    nlohmann::json status;
    /// The settings reply (``settings_edit.hpp``); null when it could not be had.
    nlohmann::json settings;
};

struct LibraryRow {
    std::string uid;
    /// The start of its id, as commands take it.
    std::string label;
    /// How it stands, in a sentence.
    std::string state;
};
[[nodiscard]] std::vector<LibraryRow> library_rows(const Snapshot& now);

/// A computer of a library, or an invite waiting for its computer.
struct ComputerRow {
    /// Empty for an invite.
    std::string machine;
    /// The id of an invite waiting; empty for a computer.
    std::string invite;
    std::string name;
    /// "Editor", "Viewer", "Viewer with the admin key".
    std::string role;
    std::string state;
    /// Where this server reaches it before the addresses it gave; for an
    /// invite, where it reaches out to the computer joining.
    std::vector<std::string> typed_addresses;
};
/// The computers of library ``uid``, those removed left out, then its invites
/// waiting.
[[nodiscard]] std::vector<ComputerRow> computer_rows(const Snapshot& now, const std::string& uid);

/// The settings screen's fields, as text.
struct SettingsFields {
    std::string name;
    std::string addresses;
    std::string port;
    bool discovery = true;
    bool sync_automatically = true;
    /// In megabits a second, as typed; empty for none.
    std::string upload_cap;
    bool share_address = true;
};
/// As saved: what the fields show. A name not chosen shows what it goes by.
[[nodiscard]] SettingsFields settings_fields(const Snapshot& now);
/// How the field for setting ``name`` is to be read beside its value: that a flag
/// sets it for this run, or that it takes effect at the next start. Empty for neither.
[[nodiscard]] std::string setting_note(const Snapshot& now, const std::string& name);

class ConfigModel {
public:
    /// ``ask`` reaches the server; by default, its commands' socket for ``data``.
    explicit ConfigModel(std::filesystem::path data, AskServer ask = {});

    [[nodiscard]] const std::filesystem::path& data() const noexcept { return data_; }
    /// May wait for the server, some seconds at most.
    [[nodiscard]] Snapshot snapshot() const;

    /// Sync every library now.
    [[nodiscard]] Done sync_now() const;
    /// Save the fields that differ from those of ``now``. A word on what takes
    /// effect when, or why it could not be done.
    [[nodiscard]] Done save_settings(const Snapshot& now, const SettingsFields& fields) const;

    [[nodiscard]] Form join_form() const;
    /// What can be done with library ``uid``, as this computer stands in it.
    [[nodiscard]] std::vector<Action> library_actions(const Snapshot& now, const std::string& uid) const;
    /// What can be done with computer ``machine`` of it.
    [[nodiscard]] std::vector<Action> computer_actions(const Snapshot& now, const std::string& uid,
                                                       const std::string& machine) const;
    /// What can be done with invite ``invite`` of it, waiting for its computer.
    [[nodiscard]] std::vector<Action> invite_actions(const Snapshot& now, const std::string& uid,
                                                     const std::string& invite) const;

private:
    /// ``request`` asked; ``said`` words the reply when it was done.
    [[nodiscard]] Done done_by(const nlohmann::json& request,
                               const std::function<Done(const nlohmann::json& reply)>& said) const;
    /// Work needing the admin password, which is the form's last field.
    [[nodiscard]] Form admin_form(std::string title, std::string intro, std::string go, std::vector<FormField> fields,
                                  std::function<nlohmann::json(const std::vector<std::string>&)> request,
                                  std::function<Done(const nlohmann::json&)> said) const;

    std::filesystem::path data_;
    AskServer ask_;
};

}  // namespace seventhslide::server
