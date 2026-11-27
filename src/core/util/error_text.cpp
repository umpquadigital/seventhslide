#include "seventhslide/util/error_text.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <new>

namespace seventhslide::util {

std::string message_for_people(const std::exception& ex)
{
    if (const auto* internal = dynamic_cast<const InternalError*>(&ex)) {
        return internal->for_people();
    }
    if (const auto* file = dynamic_cast<const std::filesystem::filesystem_error*>(&ex)) {
        // The system's reason ("Permission denied") is plain enough; the
        // library's framing around it is not.
        auto out = "A file could not be used (" + file->code().message() + ")";
        if (!file->path1().empty()) {
            out += ": " + file->path1().string();
        }
        return out;
    }
    if (dynamic_cast<const nlohmann::json::exception*>(&ex) != nullptr) {
        return "Some data could not be read: it is not in the form SeventhSlide expects.";
    }
    if (dynamic_cast<const std::bad_alloc*>(&ex) != nullptr) {
        return "SeventhSlide ran out of memory.";
    }
    return ex.what();
}

}  // namespace seventhslide::util
