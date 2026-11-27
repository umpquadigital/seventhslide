#pragma once

// ``seventhslide --server config``: the Sync page for a computer with no window,
// over ``ConfigModel``, which knows all of what it does. These screens only draw it
// and pass on keys, so what they add is little: which page is showing, what is
// picked, and what has been typed.
//
// No mouse, so a terminal's own selection copies over SSH. A code to copy gets a
// page with nothing else on it, and may be printed on leaving, where the terminal
// wraps it and does not break it.
//
// Seam: FTXUI.

#include "seventhslide/server/config_model.hpp"

#include <ftxui/component/component_base.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::server {

class ConfigUi {
public:
    /// Runs the first off the screen's thread, then the second back on it: asking
    /// the server may take seconds.
    using Run = std::function<void(std::function<void()> work, std::function<void()> then)>;

    ConfigUi(ConfigModel model, Run run, std::function<void()> quit);

    [[nodiscard]] ftxui::Component component() const { return root_; }
    /// How things stand now; on the screen's thread.
    void show(Snapshot now);
    /// The codes asked to be printed on leaving.
    [[nodiscard]] const std::vector<std::string>& to_print() const noexcept { return to_print_; }

private:
    enum Tab { Status, Libraries, Settings };
    enum Page { Main, FormPage, DonePage };

    [[nodiscard]] ftxui::Component make_status();
    [[nodiscard]] ftxui::Component make_libraries();
    [[nodiscard]] ftxui::Component make_settings();
    [[nodiscard]] ftxui::Component make_form();
    [[nodiscard]] ftxui::Component make_done();
    [[nodiscard]] ftxui::Element draw_header() const;
    [[nodiscard]] ftxui::Element draw_stopped() const;
    /// The computers and actions of what is picked.
    void follow_picks();
    void open(Form form);
    void submit();
    void close_form();
    void close_done();
    void work(std::function<Done()> what, std::function<void(Done)> then);
    bool on_key(const ftxui::Event& event);

    ConfigModel model_;
    Run run_;
    std::function<void()> quit_;
    Snapshot now_;
    ftxui::Component root_;
    ftxui::Component tabs_;

    int tab_ = Status;
    int page_ = Main;
    bool working_ = false;
    std::vector<std::string> tab_labels_{"Status", "Libraries", "Settings"};

    std::string status_note_;
    int status_top_ = 0;

    std::vector<LibraryRow> libraries_;
    std::vector<std::string> library_labels_;
    int library_ = 0;
    std::vector<ComputerRow> computers_;
    std::vector<std::string> computer_labels_;
    int computer_ = 0;
    std::vector<Action> actions_;
    std::vector<std::string> action_labels_;
    int action_ = 0;
    int picked_library_ = -1;
    int picked_computer_ = -1;

    SettingsFields fields_;
    /// Typed in and not saved: what the server says does not replace it.
    bool fields_touched_ = false;
    std::string settings_note_;
    ftxui::Component settings_inputs_;

    std::optional<Form> form_;
    /// One for each field; a deque, as inputs keep where theirs is.
    std::deque<std::string> answers_;
    std::string form_go_label_;
    std::string form_problem_;
    ftxui::Component form_inputs_;
    ftxui::Component form_go_;

    Done done_;
    bool print_done_ = false;
    ftxui::Component done_close_;
    std::vector<std::string> to_print_;
};

/// Run the screens on this terminal until the operator leaves; the exit status.
[[nodiscard]] int run_config_screen(const std::filesystem::path& data);

}  // namespace seventhslide::server
