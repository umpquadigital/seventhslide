#pragma once

// How a QML entry point reports a failure.
//
// Every Q_INVOKABLE that can throw wraps its body in a function-try-block and
// ends in report_entry_error, which logs the failure and returns its message,
// in words for the operator (a database's own words, "UNIQUE constraint
// failed: ...", stay in the log). A refusal (no library open, a song that is
// no longer there) is a sentence for people too: tr() where the command
// refuses, or, for one many commands share, refusals.hpp. Neither is ever
// what() or a literal (tools/entry_errors.py, which CI runs, finds one where
// it is returned, refused with, or kept as an error).
// A refusal core makes and words itself (an EditResult's error, a model
// edit's message: "Cannot delete the last stack") is shown as core words it:
// core is plain C++, with no Qt to translate it. Where the app refuses before
// it asks core, it says so in its own words: a stack that is not there is
// refusal::stack_gone() for every stack command.
//
// Which way the message reaches the operator follows from what the entry point
// returns:
//
//   * a QString (empty on success) or a map with an "error" key (left out on
//     success) that its caller shows: the entry point returns the message
//     there, for the calling dialog to show;
//   * the id of a job (an export, an import, a conversion, an upload): the
//     message is among the errors of the job's finished signal, as every
//     other outcome of the job is;
//   * anything else (bool, void, a count, a list, a name, or a record the
//     caller opens, {} when there is none): the entry point emits
//     errorOccurred(message), which SessionFacade forwards to the shell's
//     dialog. A refusal there is the answer itself (false, -1, {}), which
//     the view acts on.
//
// A failure is therefore never only in the log, but for what a view can do
// without: a note it adds when it can (a warning beside a confirm, which
// asks without it) and a thumbnail are left out and logged, each with a
// comment where it is. The [[nodiscard]] below makes the compiler hold every
// call site to reporting or to saying, by a cast to void, that it does not.
//
// Which entry points are wrapped: those that reach the database, files, the
// network, stored JSON, or a rebuild and present (layout, compositing, the
// video backend). Those that read in-memory state, or edit an in-memory
// document through an editor that reports expected failures as an
// EditResult, are not: nothing there throws short of running out of memory.
// Stored data is read with the tolerant json::*_or readers (json/read.hpp),
// so a malformed row reads as missing instead of throwing. Image providers
// run on Qt's loader thread and cannot report to the operator; they log and
// return a blank image, and call_on_gui_thread never lets an exception out of
// the event loop.
//
// guarded_change is that wrapper for the commonest shape: a bool entry point
// that makes one change and, when the change took, tells the UI.

#include "seventhslide/db/database.hpp"
#include "seventhslide/util/error_text.hpp"

#include <QObject>
#include <QString>

#include <QtGlobal>

#include <exception>

namespace seventhslide::app {

/// What the operator is shown for ``ex``: util::message_for_people, so a
/// database's or a library's own words stay in the log. A write refused
/// because the library is a viewer's (sync opens it with nothing to be
/// written: SessionFacade::set_read_only) says why.
[[nodiscard]] inline QString message_for_operator(const std::exception& ex)
{
    if (const auto* db_error = dynamic_cast<const db::DatabaseError*>(&ex); db_error && db_error->read_only()) {
        return QObject::tr("This library is view only on this computer: it receives the library "
                           "from the others, and nothing changed here would reach them.");
    }
    return QString::fromStdString(util::message_for_people(ex));
}

/// Log ``entry`` failing with ``ex`` and return what the operator is shown
/// (``message_for_operator``).
[[nodiscard]] inline QString report_entry_error(const char* entry, const std::exception& ex)
{
    qWarning("SeventhSlide: %s failed: %s", entry, ex.what());
    return message_for_operator(ex);
}

/// Entry point ``entry`` on ``owner``: run ``change``; when it returns true,
/// run ``then`` (the refresh that follows it) and return true. A throw from
/// either is reported through ``owner.errorOccurred`` and returns false.
template <typename Owner, typename Change, typename Then>
bool guarded_change(Owner& owner, const char* entry, Change change, Then then)
{
    try {
        if (!change()) {
            return false;
        }
        then();
        return true;
    } catch (const std::exception& ex) {
        emit owner.errorOccurred(report_entry_error(entry, ex));
        return false;
    }
}

} // namespace seventhslide::app
