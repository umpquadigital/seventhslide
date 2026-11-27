#include <seventhslide/remote/remote_protocol.hpp>

#include <seventhslide/json/read.hpp>
#include <seventhslide/util/error_text.hpp>
#include <seventhslide/util/find.hpp>

#include <QtGlobal>

#include <algorithm>
#include <exception>

namespace seventhslide::remote::protocol {

namespace {

using C = Command;
using S = Section;

/// Navigation inside the current item, on either side.
const std::vector<S> k_nav{S::Session, S::Lines, S::Images, S::Preview, S::PreviewImages};
/// Something new went live on a stack, or was cued in its Preview: the
/// rundown's live and cued rows may move with it.
const std::vector<S> k_live{S::Session, S::Lines,   S::Stacks,       S::Service,
                            S::Images,  S::Advance, S::Preview,      S::PreviewLines,
                            S::PreviewImages};
/// Media transport, on either side.
const std::vector<S> k_media{S::Session, S::Preview};
/// What a Preview holds: the stacks' cued flags, the cued row, the Preview,
/// and, a tab left with nothing showing another stack, all the tab shows.
const std::vector<S> k_cue{S::Session, S::Lines,   S::Stacks,       S::Service,
                           S::Images,  S::Preview, S::PreviewLines, S::PreviewImages};
/// Blank / freeze.
const std::vector<S> k_mask{S::Session, S::Outputs, S::Stacks};
/// A stack's blank, of what is live or of what its Preview has cued.
const std::vector<S> k_stack_blank{S::Session, S::Outputs, S::Stacks, S::Preview};
/// The rundown changed shape.
const std::vector<S> k_rundown{S::Service};
/// Wholesale change - a different service, or a focus switch.
const std::vector<S> k_everything{S::Session, S::Stacks,  S::Service, S::Lines,
                                  S::Outputs, S::Images,  S::Advance, S::Preview,
                                  S::PreviewLines, S::PreviewImages};

std::vector<CommandSpec> build_table()
{
    const ArgSpec stack_id{"stackId", ArgType::String, true};
    const ArgSpec at_index{"atIndex", ArgType::Int, false};
    // Which side of the tab's stack a command works: "program" (the default)
    // or "preview".
    const ArgSpec side{"side", ArgType::String, false};

    return {
        // ---- transport ----
        {C::NextSlide, "nextSlide", {side}, k_nav},
        {C::PrevSlide, "prevSlide", {side}, k_nav},
        {C::JumpToLine, "jumpToLine", {{"lineIndex", ArgType::Int, true}, side}, k_nav},
        {C::NextSlideOnStack, "nextSlideOnStack", {stack_id}, k_live},
        {C::JumpToLineOnStack, "jumpToLineOnStack",
         {stack_id, {"lineIndex", ArgType::Int, true}},
         k_live},
        {C::NextServiceSlide, "nextServiceSlide", {}, k_everything},

        // ---- blank / freeze ----
        {C::ToggleGlobalBlank, "toggleGlobalBlank", {}, k_mask},
        {C::ToggleGlobalFreeze, "toggleGlobalFreeze", {}, k_mask},
        {C::ToggleOutputBlank, "toggleOutputBlank", {{"outputId", ArgType::String, true}}, k_mask},
        {C::ToggleOutputFreeze, "toggleOutputFreeze", {{"outputId", ArgType::String, true}}, k_mask},
        {C::ToggleStackBlank, "toggleStackBlank", {stack_id, side}, k_stack_blank},
        {C::ToggleStackFreeze, "toggleStackFreeze", {stack_id}, k_mask},
        {C::ClearBlankFreeze, "clearBlankFreeze", {}, k_mask},

        // ---- stacks ----
        {C::SetFocusedStackId, "setFocusedStackId", {stack_id}, k_everything},
        {C::ArmSendTo, "armSendTo", {stack_id}, {S::Stacks}},
        {C::ClearStack, "clearStack", {stack_id}, k_live},
        {C::ClearAllStacks, "clearAllStacks", {}, k_everything},
        // The take: by the theme's transition, or by the one named.
        {C::Transition, "transition",
         {stack_id,
          {"type", ArgType::String, false},
          {"durationMs", ArgType::Int, false},
          {"direction", ArgType::String, false},
          {"easing", ArgType::String, false}},
         k_everything},
        {C::ClearPreview, "clearPreview", {stack_id}, k_cue},
        // This computer's, as Transition's menu on the console sets it.
        {C::SetTakeFade, "setTakeFade", {{"durationMs", ArgType::Int, true}}, {S::Stacks}},
        {C::SetServiceItemStack, "setServiceItemStack",
         {{"itemId", ArgType::Int, true}, stack_id},
         {S::Service, S::Stacks}},
        {C::SetServiceItemAlsoPlay, "setServiceItemAlsoPlay",
         {{"itemId", ArgType::Int, true}, {"companionIds", ArgType::IntArray, true}},
         {S::Service, S::Stacks}},
        {C::LinkServiceItems, "linkServiceItems",
         {{"itemIds", ArgType::IntArray, true}},
         {S::Service, S::Stacks}},
        {C::UnlinkServiceItem, "unlinkServiceItem",
         {{"itemId", ArgType::Int, true}},
         {S::Service, S::Stacks}},

        // ---- per-stack auto-advance ----
        {C::SetAutoAdvance, "setAutoAdvance",
         {stack_id,
          {"enabled", ArgType::Bool, false},
          {"intervalSec", ArgType::Int, false},
          {"loop", ArgType::Bool, false},
          {"through", ArgType::Bool, false},
          {"stopAtDividers", ArgType::Bool, false}},
         // advancingStackIds is in the session section.
         {S::Session, S::Advance}},

        // ---- media transport ----
        {C::ImageGoto, "imageGoto", {{"index", ArgType::Int, true}, side},
         {S::Session, S::Images, S::Preview, S::PreviewImages}},
        {C::VideoPlay, "videoPlay", {side}, k_media},
        {C::VideoPause, "videoPause", {side}, k_media},
        {C::VideoSeek, "videoSeek", {{"positionSec", ArgType::Number, true}, side}, k_media},
        {C::VideoRestart, "videoRestart", {side}, k_media},
        {C::WebBack, "webBack", {}, {S::Session}},
        {C::WebReload, "webReload", {}, {S::Session}},

        // ---- service rundown ----
        {C::OpenService, "openService", {{"serviceId", ArgType::Int, true}}, k_everything},
        {C::CloseService, "closeService", {}, k_everything},
        {C::SelectServiceItem, "selectServiceItem", {{"index", ArgType::Int, true}}, k_everything},
        {C::SelectServiceItemAtImage, "selectServiceItemAtImage",
         {{"index", ArgType::Int, true}, {"imageIndex", ArgType::Int, true}},
         k_everything},
        {C::AddSongToService, "addSongToService",
         {{"songId", ArgType::Int, true}, at_index}, k_rundown},
        {C::AddFreeformToService, "addFreeformToService",
         {{"freeformId", ArgType::Int, true}, at_index}, k_rundown},
        {C::AddBibleRefToService, "addBibleRefToService",
         {{"bibleId", ArgType::Int, true},
          {"book", ArgType::String, true},
          {"chapter", ArgType::Int, true},
          {"verseStart", ArgType::Int, true},
          {"verseEnd", ArgType::Int, true},
          at_index},
         k_rundown},
        {C::AddImageToService, "addImageToService",
         {{"filename", ArgType::String, true}, at_index}, k_rundown},
        {C::AddVideoToService, "addVideoToService",
         {{"filename", ArgType::String, true}, at_index}, k_rundown},
        {C::AddImageFolderToService, "addImageFolderToService",
         {{"folderId", ArgType::Int, true}, at_index}, k_rundown},
        {C::AddVideoFolderToService, "addVideoFolderToService",
         {{"folderId", ArgType::Int, true}, at_index}, k_rundown},
        {C::AddFreeformFolderToService, "addFreeformFolderToService",
         {{"folderId", ArgType::Int, true}, at_index}, k_rundown},
        {C::AddDividerToService, "addDividerToService",
         {{"title", ArgType::String, false}, at_index}, k_rundown},
        {C::RemoveServiceItem, "removeServiceItem",
         {{"itemId", ArgType::Int, true}},
         {S::Service, S::Session, S::Lines, S::Images, S::Preview, S::PreviewLines,
          S::PreviewImages}},
        {C::MoveServiceItemTo, "moveServiceItemTo",
         {{"fromIndex", ArgType::Int, true}, {"toIndex", ArgType::Int, true}},
         k_rundown},
        {C::ReorderServiceItems, "reorderServiceItems",
         {{"orderedItemIds", ArgType::IntArray, true}},
         k_rundown},

        // ---- service library: rename/delete; create stays on the console ----
        {C::RenameService, "renameService",
         {{"serviceId", ArgType::Int, true}, {"name", ArgType::String, true}},
         {S::Session}},
        {C::DeleteService, "deleteService", {{"serviceId", ArgType::Int, true}}, k_everything},

        // ---- library: play only, never mutate ----
        {C::PlaySong, "playSong", {{"songId", ArgType::Int, true}}, k_live},
        {C::PlayFreeform, "playFreeform", {{"freeformId", ArgType::Int, true}}, k_live},
        {C::PlayImage, "playImage", {{"filename", ArgType::String, true}}, k_live},
        {C::PlayImageFolder, "playImageFolder",
         {{"folderId", ArgType::Int, true}, {"startIndex", ArgType::Int, false}},
         k_live},
        {C::PlayVideo, "playVideo",
         {{"filename", ArgType::String, true}, {"loop", ArgType::Bool, false}},
         k_live},
        {C::PlayBibleRef, "playBibleRef",
         {{"bibleId", ArgType::Int, true},
          {"book", ArgType::String, true},
          {"chapter", ArgType::Int, true},
          {"verseStart", ArgType::Int, true},
          {"verseEnd", ArgType::Int, true}},
         k_live},
        {C::PlayAudio, "playAudio",
         {{"filename", ArgType::String, true}, {"loop", ArgType::Bool, false}},
         k_live},
        {C::PlayWebPage, "playWebPage", {{"pageId", ArgType::Int, true}}, k_live},
        {C::PlayLiveSource, "playLiveSource",
         {{"kind", ArgType::String, true}, {"name", ArgType::String, true}},
         k_live},
        {C::ResolveBibleRef, "resolveBibleRef",
         {{"reference", ArgType::String, true}, {"bibleId", ArgType::Int, false}},
         {}},
        {C::SuggestBibleBooks, "suggestBibleBooks",
         {{"prefix", ArgType::String, true}, {"bibleId", ArgType::Int, false}},
         {}},
    };
}

bool type_matches(ArgType type, const nlohmann::json& v)
{
    switch (type) {
    case ArgType::Int:
        return v.is_number_integer();
    case ArgType::Number:
        return v.is_number();
    case ArgType::Bool:
        return v.is_boolean();
    case ArgType::String:
        return v.is_string();
    case ArgType::IntArray:
        return v.is_array()
            && std::all_of(v.begin(), v.end(),
                           [](const nlohmann::json& e) { return e.is_number_integer(); });
    }
    return false;
}

std::string_view type_name(ArgType type)
{
    switch (type) {
    case ArgType::Int: return "an integer";
    case ArgType::Number: return "a number";
    case ArgType::Bool: return "a boolean";
    case ArgType::String: return "a string";
    case ArgType::IntArray: return "an array of integers";
    }
    return "a value";
}

} // namespace

const std::vector<CommandSpec>& command_table()
{
    static const std::vector<CommandSpec> table = build_table();
    return table;
}

const CommandSpec& command_spec(Command command)
{
    return command_table().at(static_cast<std::size_t>(command));
}

const CommandSpec* find_command(std::string_view name)
{
    return util::find_by(command_table(), name, &CommandSpec::name);
}

std::string validate_args(const CommandSpec& spec, const nlohmann::json& args)
{
    if (!args.is_object()) {
        return "arguments must be an object";
    }
    for (const ArgSpec& a : spec.args) {
        const std::string key(a.key);
        const auto it = args.find(key);
        if (it == args.end() || it->is_null()) {
            if (a.required) {
                return "missing required argument '" + key + "'";
            }
            continue;
        }
        if (!type_matches(a.type, *it)) {
            return "argument '" + key + "' must be " + std::string(type_name(a.type));
        }
    }
    // Reject unknown keys so a typo fails loudly instead of being ignored.
    for (const auto& [key, value] : args.items()) {
        const bool known = std::any_of(spec.args.begin(), spec.args.end(),
                                       [&key](const ArgSpec& a) { return a.key == key; });
        if (!known) {
            return "unknown argument '" + key + "'";
        }
    }
    return {};
}

Dispatch dispatch(IRemoteBridge& bridge,
                  std::string_view name,
                  const nlohmann::json& args,
                  View& view)
{
    const CommandSpec* spec = find_command(name);
    if (spec == nullptr) {
        return {CommandResult::fail("unknown command '" + std::string(name) + "'"), {}};
    }
    const nlohmann::json effective = args.is_null() ? nlohmann::json::object() : args;
    if (const std::string problem = validate_args(*spec, effective); !problem.empty()) {
        return {CommandResult::fail(problem), {}};
    }
    CommandResult result;
    try {
        result = bridge.invoke(spec->command, effective, view);
    } catch (const std::exception& ex) {
        // The phone is told in plain words; a database's own stay in the log.
        qWarning("SeventhSlide remote: %s failed: %s", std::string(spec->name).c_str(), ex.what());
        return {CommandResult::fail(util::message_for_people(ex)), {}};
    } catch (...) {
        return {CommandResult::fail("The command failed."), {}};
    }
    if (!result.ok) {
        return {std::move(result), {}};
    }
    return {std::move(result), spec->dirty};
}

DecodedMessage decode_client_message(std::string_view text)
{
    const auto doc = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        return {std::nullopt, "malformed message"};
    }
    if (!doc.is_object()) {
        return {std::nullopt, "message must be an object"};
    }
    ClientMessage msg;
    msg.type = json::str_or(doc, "type");
    msg.id = json::str_or(doc, "id");
    msg.name = json::str_or(doc, "name");
    // Kept as sent: validate_args reports a non-object with a proper reason.
    if (const auto* args = json::find(doc, "args"); args != nullptr && !args->is_null()) {
        msg.args = *args;
    }
    return {std::move(msg), {}};
}

nlohmann::json hello_frame(std::string_view boot)
{
    return {
        {"type", "hello"},
        {"protocol", k_protocol_version},
        {"boot", std::string(boot)},
        {"caps", nlohmann::json::array(
                     {"stacks", "freeform", "video", "autoAdvance", "audio", "web", "live",
                      "previewProgram"})},
    };
}

nlohmann::json snapshot_frame(const IRemoteBridge& bridge, const View& view, std::uint64_t rev)
{
    nlohmann::json state = nlohmann::json::object();
    for (const Section s : all_sections()) {
        state[std::string(section_name(s))] = bridge.section(s, view);
    }
    return {{"type", "snapshot"}, {"rev", rev}, {"state", std::move(state)}};
}

nlohmann::json ack_frame(const std::string& id, const CommandResult& result)
{
    nlohmann::json msg{{"type", "ack"}, {"id", id}, {"ok", result.ok}};
    if (!result.ok) {
        msg["error"] = result.error;
    } else if (!result.data.empty()) {
        msg["data"] = result.data;
    }
    return msg;
}

nlohmann::json error_frame(std::string_view text)
{
    return {{"type", "error"}, {"text", std::string(text)}};
}

} // namespace seventhslide::remote::protocol
