#pragma once

// The words a person sees for a failure.
//
// Most of SeventhSlide's own errors are written for people ("This invite has
// expired. Ask for a new one.") and are shown as they are. Some are not: a
// database driver's text ("UNIQUE constraint failed: songs.id"), a JSON
// library's, the file system's. Those keep their words for the log and say
// something plainer to the person.
//
// Plain C++20 (no Qt).

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace seventhslide::util {

/// A failure whose ``what()`` is for the log (a driver's or a library's
/// words), with ``for_people`` what to show instead.
class InternalError : public std::runtime_error {
public:
    InternalError(const std::string& what, std::string for_people)
        : std::runtime_error(what), for_people_(std::move(for_people))
    {
    }
    [[nodiscard]] const std::string& for_people() const noexcept { return for_people_; }

private:
    std::string for_people_;
};

/// What to show a person for ``ex``: an InternalError's ``for_people``; for
/// a failure from a library (JSON, the file system, memory), a sentence of
/// SeventhSlide's own; otherwise ``what()``. Log ``what()`` beside it.
[[nodiscard]] std::string message_for_people(const std::exception& ex);

}  // namespace seventhslide::util
