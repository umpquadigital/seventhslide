#include "seventhslide/server/config_screen.hpp"

#include "commands.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace seventhslide::server {
namespace {

using namespace ftxui;

/// How often it asks how things stand.
constexpr auto k_look_every = std::chrono::seconds(2);

InputOption one_line(bool secret = false, std::function<void()> on_change = [] {}, std::function<void()> on_enter = [] {})
{
    auto option = InputOption::Default();
    option.multiline = false;
    option.password = secret;
    option.on_change = std::move(on_change);
    option.on_enter = std::move(on_enter);
    // Going to a field, typing goes on from the end of what is there.
    option.cursor_position = std::numeric_limits<int>::max();
    return option;
}

std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        out.push_back(std::move(line));
    }
    return out;
}

/// ``code`` in lines of ``width``: a code is one word, which would not wrap.
Element code_lines(const std::string& code, int width)
{
    Elements out;
    const auto each = static_cast<std::size_t>(std::max(width, 20));
    for (std::size_t at = 0; at < code.size(); at += each) {
        out.push_back(text(code.substr(at, each)));
    }
    return vbox(std::move(out));
}

Element labelled(const std::string& label, Element field, const std::string& note = {})
{
    Elements rows{hbox({text(label) | size(WIDTH, EQUAL, 26), std::move(field) | flex})};
    if (!note.empty()) {
        rows.push_back(hbox({text("") | size(WIDTH, EQUAL, 26), paragraph(note) | dim | flex}));
    }
    return vbox(std::move(rows));
}

}  // namespace

ConfigUi::ConfigUi(ConfigModel model, Run run, std::function<void()> quit)
    : model_(std::move(model)), run_(std::move(run)), quit_(std::move(quit))
{
    tabs_ = Toggle(&tab_labels_, &tab_);
    auto main = Container::Vertical(
        {tabs_, Container::Tab({make_status(), make_libraries(), make_settings()}, &tab_)});
    auto pages = Container::Tab({main, make_form(), make_done()}, &page_);
    auto drawn = Renderer(pages, [this, pages] {
        follow_picks();
        return vbox({draw_header(), separator(), pages->Render() | flex,
                     working_ ? text("Working...") | bold : text(""),
                     text(page_ != Main                ? "Tab moves, Esc goes back"
                          : settings_inputs_->Focused() ? "Typing goes in the field; Esc leaves it"
                                                        : "Arrows or Tab move, Enter chooses, 1 2 3 change page, q leaves")
                         | dim});
    });
    root_ = CatchEvent(drawn, [this](const Event& event) { return on_key(event); });
}

bool ConfigUi::on_key(const Event& event)
{
    // What is being worked on is left to finish; Ctrl-C is the app's.
    if (working_) {
        return true;
    }
    if (page_ == FormPage && event == Event::Escape) {
        close_form();
        return true;
    }
    if (page_ == DonePage && event == Event::Escape) {
        close_done();
        return true;
    }
    if (page_ != Main) {
        return false;
    }
    if (settings_inputs_->Focused() && tab_ == Settings) {
        if (event == Event::Escape) {
            tabs_->TakeFocus();
            return true;
        }
        return false;
    }
    if (event == Event::Character('q')) {
        quit_();
        return true;
    }
    for (const int tab : {Status, Libraries, Settings}) {
        if (event == Event::Character(std::to_string(tab + 1))) {
            tab_ = tab;
            // On the page names: a key pressed next is not typed in a field.
            tabs_->TakeFocus();
            return true;
        }
    }
    return false;
}

void ConfigUi::show(Snapshot now)
{
    now_ = std::move(now);
    status_note_.clear();
    libraries_ = library_rows(now_);
    library_labels_.clear();
    for (const auto& row : libraries_) {
        library_labels_.push_back(row.label);
    }
    library_labels_.push_back("Join a Synced Library…");
    library_ = std::clamp(library_, 0, static_cast<int>(library_labels_.size()) - 1);
    // Whatever is on them may have changed.
    picked_library_ = -1;
    if (!fields_touched_ && now_.settings.is_object()) {
        fields_ = settings_fields(now_);
    }
    follow_picks();
}

void ConfigUi::follow_picks()
{
    if (picked_library_ == library_ && picked_computer_ == computer_) {
        return;
    }
    const bool is_library = library_ < static_cast<int>(libraries_.size());
    if (picked_library_ != library_ || computers_.empty()) {
        computers_ = is_library ? computer_rows(now_, libraries_[static_cast<std::size_t>(library_)].uid)
                                : std::vector<ComputerRow>{};
        computer_labels_.clear();
        for (const auto& row : computers_) {
            computer_labels_.push_back(row.name);
        }
        computer_ = std::clamp(computer_, 0, std::max(0, static_cast<int>(computers_.size()) - 1));
    }
    actions_.clear();
    if (is_library) {
        const auto& uid = libraries_[static_cast<std::size_t>(library_)].uid;
        if (!computers_.empty()) {
            const auto& row = computers_[static_cast<std::size_t>(computer_)];
            actions_ = row.invite.empty() ? model_.computer_actions(now_, uid, row.machine)
                                          : model_.invite_actions(now_, uid, row.invite);
        }
        auto of_library = model_.library_actions(now_, uid);
        actions_.insert(actions_.end(), std::make_move_iterator(of_library.begin()),
                        std::make_move_iterator(of_library.end()));
    }
    action_labels_.clear();
    for (const auto& action : actions_) {
        action_labels_.push_back(action.label);
    }
    action_ = std::clamp(action_, 0, std::max(0, static_cast<int>(actions_.size()) - 1));
    picked_library_ = library_;
    picked_computer_ = computer_;
}

Element ConfigUi::draw_header() const
{
    std::string name = "this server";
    if (now_.settings.is_object()) {
        if (const auto it = now_.settings.find("now"); it != now_.settings.end()) {
            name = it->value("computer_name", name);
        } else {
            name = settings_fields(now_).name;
        }
    }
    return hbox({text("SeventhSlide server: ") | bold, text(name) | bold, filler(),
                 now_.running ? text("Running") | color(Color::Green) : text("Not running") | color(Color::Yellow)});
}

Element ConfigUi::draw_stopped() const
{
    return vbox({paragraph("Work on libraries needs the server running, and this one is not."),
                 text(""),
                 paragraph(now_.problem.empty() ? "Start it, and this page follows: sudo systemctl start "
                                                  "seventhslide-server, or seventhslide --server --data \""
                                                      + model_.data().string() + "\""
                                                : now_.problem)});
}

Component ConfigUi::make_status()
{
    auto sync = Button("Sync Now", [this] {
        work([this] { return model_.sync_now(); }, [this](Done done) { status_note_ = std::move(done.message); });
    }, ButtonOption::Ascii());
    auto lines = Renderer([this](bool /*focused: takes the keys that scroll*/) {
        Elements out;
        if (!now_.running) {
            out.push_back(draw_stopped());
        } else {
            const auto all = lines_of(describe_status(now_.status));
            status_top_ = std::clamp(status_top_, 0, std::max(0, static_cast<int>(all.size()) - 1));
            for (std::size_t i = static_cast<std::size_t>(status_top_); i < all.size(); ++i) {
                out.push_back(all[i].empty() ? text(" ") : paragraph(all[i]));
            }
        }
        return vbox(std::move(out)) | yframe | flex;
    });
    auto scrolled = CatchEvent(lines, [this](const Event& event) {
        if (event == Event::ArrowDown || event == Event::PageDown) {
            status_top_ += event == Event::PageDown ? 10 : 1;
            return true;
        }
        if (status_top_ > 0 && (event == Event::ArrowUp || event == Event::PageUp)) {
            status_top_ = std::max(0, status_top_ - (event == Event::PageUp ? 10 : 1));
            return true;
        }
        return false;
    });
    auto shown_sync = Maybe(sync, [this] { return now_.running; });
    auto both = Container::Vertical({shown_sync, scrolled});
    return Renderer(both, [this, shown_sync, scrolled] {
        Elements rows;
        if (now_.running) {
            rows.push_back(hbox({shown_sync->Render(), text("  "), paragraph(status_note_) | dim | flex}));
            rows.push_back(separator());
        }
        rows.push_back(scrolled->Render() | flex);
        return vbox(std::move(rows));
    });
}

Component ConfigUi::make_libraries()
{
    auto on_library = [this] {
        if (library_ == static_cast<int>(libraries_.size())) {
            open(model_.join_form());
        }
    };
    auto library_option = MenuOption::Vertical();
    library_option.on_enter = on_library;
    auto libraries = Menu(&library_labels_, &library_, library_option);
    auto computers = Menu(&computer_labels_, &computer_, MenuOption::Vertical());
    auto action_option = MenuOption::Vertical();
    action_option.on_enter = [this] {
        if (action_ >= 0 && action_ < static_cast<int>(actions_.size())) {
            open(actions_[static_cast<std::size_t>(action_)].form);
        }
    };
    auto actions = Menu(&action_labels_, &action_, action_option);
    auto panes = Container::Horizontal({libraries, computers, actions});
    return Renderer(panes, [this, libraries, computers, actions] {
        if (!now_.running) {
            return draw_stopped();
        }
        Elements about;
        if (library_ < static_cast<int>(libraries_.size())) {
            const auto& row = libraries_[static_cast<std::size_t>(library_)];
            about.push_back(paragraph("Library " + row.uid));
            about.push_back(paragraph(row.state) | dim);
        } else {
            about.push_back(paragraph("Join one by an invite made on a computer of the library."));
        }
        if (computer_ < static_cast<int>(computers_.size())) {
            const auto& c = computers_[static_cast<std::size_t>(computer_)];
            about.push_back(text(" "));
            std::string typed;
            for (const auto& a : c.typed_addresses) {
                typed += (typed.empty() ? "" : ", ") + a;
            }
            about.push_back(paragraph(c.name + ": " + c.role + ". " + c.state
                                      + (typed.empty() ? ""
                                         : c.invite.empty() ? ". Reached at " + typed
                                                            : ". Reached out to at " + typed)));
        }
        if (action_ < static_cast<int>(actions_.size())) {
            about.push_back(text(" "));
            about.push_back(paragraph(actions_[static_cast<std::size_t>(action_)].detail) | dim);
        }
        return vbox({hbox({vbox({text("Libraries") | bold, libraries->Render() | yframe | flex}) | size(WIDTH, EQUAL, 26),
                           separator(),
                           vbox({text("Computers") | bold, computers->Render() | yframe | flex}) | flex,
                           separator(),
                           vbox({text("What to do") | bold, actions->Render() | yframe | flex}) | size(WIDTH, EQUAL, 36)})
                         | flex,
                     separator(), vbox(std::move(about))});
    });
}

Component ConfigUi::make_settings()
{
    const auto touched = [this] {
        fields_touched_ = true;
        settings_note_.clear();
    };
    const auto ticked = [touched] {
        auto option = CheckboxOption::Simple();
        option.on_change = touched;
        return option;
    };
    auto name = Input(&fields_.name, "", one_line(false, touched));
    auto addresses = Input(&fields_.addresses, "None", one_line(false, touched));
    auto port = Input(&fields_.port, "8766", one_line(false, touched));
    auto upload_cap = Input(&fields_.upload_cap, "None", one_line(false, touched));
    auto sync = Checkbox("", &fields_.sync_automatically, ticked());
    auto discovery = Checkbox("", &fields_.discovery, ticked());
    auto sharing = Checkbox("", &fields_.share_address, ticked());
    auto save = Button("Save", [this] {
        const auto now = now_;
        const auto fields = fields_;
        work([this, now, fields] { return model_.save_settings(now, fields); }, [this](Done done) {
            if (done.ok) {
                fields_touched_ = false;
            }
            settings_note_ = std::move(done.message);
        });
    }, ButtonOption::Ascii());
    settings_inputs_ = Container::Vertical({name, addresses, port, upload_cap});
    auto all = Container::Vertical({settings_inputs_, sync, discovery, sharing, save});
    return Renderer(all, [=, this] {
        return vbox(
            {labelled("Name", name->Render(), setting_note(now_, "computer_name")),
             labelled("Routable address", addresses->Render(),
                      !setting_note(now_, "addresses").empty()
                          ? setting_note(now_, "addresses")
                          : fields_.addresses.empty()
                                ? "Only for a computer with a port forwarded. Every computer in its libraries sees this."
                                : "Anyone on the internet can now try to connect. An editor's key can change a library "
                                  "if it is stolen, so a computer that faces the internet is safer as a viewer."),
             labelled("Port", port->Render(),
                      !setting_note(now_, "port").empty() ? setting_note(now_, "port")
                                                          : "Only needed to match a forward on your router"),
             labelled("Upload speed cap", upload_cap->Render(),
                      "Megabits a second, for media sent to computers not on this network, so its uplink keeps "
                      "room: for a livestream, say"),
             labelled("Sync automatically", sync->Render(), "Off, this computer syncs only when you press Sync Now"),
             labelled("Announce on the network", discovery->Render(),
                      !setting_note(now_, "discovery").empty()
                          ? setting_note(now_, "discovery")
                          : "Finds the computers on the church network, and they find it"),
             labelled("Share its address", sharing->Render(),
                      "The others of its libraries may be told where it is, to reach it directly. Off, only "
                      "those it connects to see it"),
             text(" "),
             hbox({save->Render(), text("  "), paragraph(settings_note_) | flex})});
    });
}

Component ConfigUi::make_form()
{
    form_inputs_ = Container::Vertical({});
    form_go_ = Button(&form_go_label_, [this] { submit(); }, ButtonOption::Ascii());
    auto back = Button("Back", [this] { close_form(); }, ButtonOption::Ascii());
    auto all = Container::Vertical({form_inputs_, Container::Horizontal({form_go_, back})});
    return Renderer(all, [this, back] {
        if (!form_) {
            return text("");
        }
        Elements rows{text(form_->title) | bold, text(" ")};
        if (!form_->intro.empty()) {
            rows.push_back(paragraph(form_->intro));
            rows.push_back(text(" "));
        }
        for (std::size_t i = 0; i < form_->fields.size(); ++i) {
            rows.push_back(labelled(form_->fields[i].label, form_inputs_->ChildAt(i)->Render(), form_->fields[i].hint));
        }
        rows.push_back(text(" "));
        rows.push_back(hbox({form_go_->Render(), text("  "), back->Render()}));
        if (!form_problem_.empty()) {
            rows.push_back(text(" "));
            rows.push_back(paragraph(form_problem_) | color(Color::Red));
        }
        return vbox(std::move(rows));
    });
}

Component ConfigUi::make_done()
{
    done_close_ = Button("Close", [this] { close_done(); }, ButtonOption::Ascii());
    auto print = Checkbox("Print it when I leave, to copy it whole", &print_done_);
    auto all = Container::Vertical({Maybe(print, [this] { return !done_.code.empty(); }), done_close_});
    return Renderer(all, [this, print] {
        Elements rows{paragraph(done_.message), text(" ")};
        if (!done_.code.empty()) {
            rows.push_back(code_lines(done_.code, Terminal::Size().dimx));
            rows.push_back(text(" "));
            rows.push_back(print->Render());
        }
        rows.push_back(done_close_->Render());
        return vbox(std::move(rows));
    });
}

void ConfigUi::open(Form form)
{
    if (working_) {
        return;
    }
    form_inputs_->DetachAllChildren();
    answers_.clear();
    const auto count = form.fields.size();
    for (std::size_t i = 0; i < count; ++i) {
        answers_.push_back(form.fields[i].value);
        // Enter goes on to the next, and does it from the last.
        form_inputs_->Add(Input(&answers_.back(), "", one_line(form.fields[i].kind == FormField::Kind::Secret, [] {}, [this, i, count] {
            if (i + 1 < count) {
                form_inputs_->ChildAt(i + 1)->TakeFocus();
            } else {
                submit();
            }
        })));
    }
    form_problem_.clear();
    form_go_label_ = form.go;
    form_ = std::move(form);
    page_ = FormPage;
    (count > 0 ? form_inputs_->ChildAt(0) : form_go_)->TakeFocus();
}

void ConfigUi::close_form()
{
    form_.reset();
    form_inputs_->DetachAllChildren();
    answers_.clear();
    form_problem_.clear();
    page_ = Main;
}

void ConfigUi::close_done()
{
    if (print_done_ && !done_.code.empty()) {
        to_print_.push_back(done_.code);
    }
    print_done_ = false;
    done_ = {};
    page_ = Main;
}

void ConfigUi::submit()
{
    if (!form_ || working_) {
        return;
    }
    const std::vector<std::string> answers(answers_.begin(), answers_.end());
    // A password is not left on the screen's side, whatever comes of it.
    for (std::size_t i = 0; i < answers_.size(); ++i) {
        if (form_->fields[i].kind == FormField::Kind::Secret) {
            answers_[i].clear();
        }
    }
    work([submit = form_->submit, answers] { return submit(answers); }, [this](Done done) {
        if (!done.ok) {
            form_problem_ = std::move(done.message);
            return;
        }
        close_form();
        done_ = std::move(done);
        page_ = DonePage;
        done_close_->TakeFocus();
    });
}

void ConfigUi::work(std::function<Done()> what, std::function<void(Done)> then)
{
    working_ = true;
    auto result = std::make_shared<Done>();
    run_([what = std::move(what), result] { *result = what(); },
         [this, then = std::move(then), result] {
             working_ = false;
             then(std::move(*result));
         });
}

int run_config_screen(const std::filesystem::path& data)
{
    auto app = App::Fullscreen();
    app.TrackMouse(false);
    app.HandlePipedInput(false);

    std::mutex mutex;
    std::condition_variable wake;
    bool leaving = false;
    std::vector<std::thread> workers;

    ConfigModel model(data);
    ConfigUi ui(
        model,
        [&](std::function<void()> work, std::function<void()> then) {
            workers.emplace_back([&app, work = std::move(work), then = std::move(then)] {
                work();
                app.Post(then);
                app.PostEvent(Event::Custom);
            });
        },
        app.ExitLoopClosure());

    ui.show(model.snapshot());
    std::thread looking([&] {
        std::unique_lock lock(mutex);
        while (!wake.wait_for(lock, k_look_every, [&] { return leaving; })) {
            lock.unlock();
            auto now = std::make_shared<Snapshot>(model.snapshot());
            app.Post([&ui, now] { ui.show(std::move(*now)); });
            app.PostEvent(Event::Custom);
            lock.lock();
        }
    });

    app.Loop(ui.component());

    {
        const std::lock_guard lock(mutex);
        leaving = true;
    }
    wake.notify_all();
    looking.join();
    for (auto& worker : workers) {
        worker.join();
    }
    for (const auto& code : ui.to_print()) {
        std::cout << code << '\n';
    }
    return 0;
}

}  // namespace seventhslide::server
