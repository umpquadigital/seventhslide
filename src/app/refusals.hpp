#pragma once

// The refusals more than one command gives, worded once.
//
// A command a view calls says why it refused in a sentence for the operator
// (entry_error.hpp). Most refusals are a command's own and are written where
// it refuses; these are the ones many commands share. Most name what the
// operator can see: a library that is not open, or a record the view still
// shows that is no longer there (another computer deleted it, or this one did
// in another window). The rest answer what a view should not have asked for
// (a kind of media there is not), still in words the operator can read.
//
// They are translated in a context of their own, "Refusal", as they belong
// to no one class.

#include <QCoreApplication>
#include <QString>

namespace seventhslide::app::refusal {

/// No library is open, so there is nothing to read or change.
[[nodiscard]] inline QString no_library()
{
    return QCoreApplication::translate("Refusal", "No library is open.");
}

/// One of the libraries this computer lists.
[[nodiscard]] inline QString library_gone()
{
    return QCoreApplication::translate("Refusal", "That library is no longer on this computer.");
}

/// The slide editor has nothing open to change.
[[nodiscard]] inline QString nothing_open()
{
    return QCoreApplication::translate("Refusal", "Nothing is open to edit.");
}

[[nodiscard]] inline QString song_gone()
{
    return QCoreApplication::translate("Refusal", "That song is no longer in the library.");
}

[[nodiscard]] inline QString bible_gone()
{
    return QCoreApplication::translate("Refusal", "That Bible is no longer in the library.");
}

[[nodiscard]] inline QString service_gone()
{
    return QCoreApplication::translate("Refusal", "That service is no longer in the library.");
}

/// No service is open, so there is no rundown to change or close.
[[nodiscard]] inline QString no_service_open()
{
    return QCoreApplication::translate("Refusal", "No service is open.");
}

/// An item of the open service (a row of the rundown).
[[nodiscard]] inline QString service_item_gone()
{
    return QCoreApplication::translate("Refusal", "That item is no longer in the service.");
}

[[nodiscard]] inline QString freeform_gone()
{
    return QCoreApplication::translate("Refusal",
                                       "That Freeform item is no longer in the library.");
}

[[nodiscard]] inline QString web_page_gone()
{
    return QCoreApplication::translate("Refusal", "That page is no longer in the library.");
}

/// A media file of the library (a picture, a video, a sound).
[[nodiscard]] inline QString media_file_gone()
{
    return QCoreApplication::translate("Refusal", "That file is no longer in the library.");
}

/// A folder of one of the library's lists (songs, media, Freeform, pages).
[[nodiscard]] inline QString folder_gone()
{
    return QCoreApplication::translate("Refusal", "That folder is no longer in the library.");
}

[[nodiscard]] inline QString profile_gone()
{
    return QCoreApplication::translate("Refusal", "That profile is no longer in the library.");
}

[[nodiscard]] inline QString output_gone()
{
    return QCoreApplication::translate("Refusal", "That output is no longer there.");
}

[[nodiscard]] inline QString theme_gone()
{
    return QCoreApplication::translate("Refusal", "That theme is no longer there.");
}

/// One of an output's destinations (a window, a stream, a device).
[[nodiscard]] inline QString destination_gone()
{
    return QCoreApplication::translate("Refusal", "That destination is no longer there.");
}

[[nodiscard]] inline QString stack_gone()
{
    return QCoreApplication::translate("Refusal", "That stack is no longer there.");
}

/// What the Bible commands ask for before a passage can be found.
[[nodiscard]] inline QString no_bible_chosen()
{
    return QCoreApplication::translate("Refusal", "Choose a Bible first.");
}

/// A book, chapter or verses the chosen Bible does not have.
[[nodiscard]] inline QString passage_not_found()
{
    return QCoreApplication::translate("Refusal", "That passage is not in this Bible.");
}

/// A name left empty where one is needed.
[[nodiscard]] inline QString name_required()
{
    return QCoreApplication::translate("Refusal", "Type a name.");
}

/// A kind of media file SeventhSlide does not have (the view asked for one
/// that is not pictures, videos or sounds).
[[nodiscard]] inline QString unknown_media_kind()
{
    return QCoreApplication::translate("Refusal",
                                       "That is not a kind of media file SeventhSlide keeps.");
}

/// A background theme's file slot the view named that is not one ("bg",
/// "bg_video").
[[nodiscard]] inline QString unknown_asset_slot()
{
    return QCoreApplication::translate("Refusal",
                                       "That is not a place in a theme for a picture or a video.");
}

/// A command given ids, of rows that are not all there any more.
[[nodiscard]] inline QString items_gone()
{
    return QCoreApplication::translate("Refusal", "Some of those items are no longer there.");
}

} // namespace seventhslide::app::refusal
