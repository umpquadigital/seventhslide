#pragma once

// What goes live. QML: ``session.live``.
//
// Putting content on the stacks (playSong, playBible, playImage, playVideo,
// playAudio, playFreeform, selectServiceItem): each lands in its stack's
// Preview when it has one, or with ``live`` straight on the outputs (the
// operator's Shift+double-click). Moving a stack named by id
// (the web remote's), and blank / freeze of the outputs (a stack's own are
// StackController's). What a well shows of the focused stack, and moving
// within it, is SideView's (``session.program``). Each change announces
// liveChanged; the application reloads what follows and repaints. The web
// remote drives the same calls (session_remote_bridge.cpp), inside
// SessionFacade::run_in_view.

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>


namespace seventhslide::app {

class FreeformLibraryController;
class LibraryController;
class MediaController;

class LiveController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

public:
    LiveController(session::Session& session,
                   LibraryController& library,
                   FreeformLibraryController& freeform_library,
                   const MediaController& media,
                   QObject* parent = nullptr);

    /// "song", "bible", "service", "freeform", "video", "audio", "image" or
    /// "empty".
    [[nodiscard]] QString mode() const;
    /// Every output not exempt is blank (frozen): what the web remote's
    /// Blank and Freeze show.
    [[nodiscard]] bool global_blank() const { return session_.global_blank(); }
    [[nodiscard]] bool global_freeze() const { return session_.global_freeze(); }
    /// The live rundown row; -1 for none.
    [[nodiscard]] int service_item_index() const noexcept { return session_.service_item_index(); }
    /// What ``st`` is showing, as the remote names a stack.
    [[nodiscard]] QString title_of_stack(const session::StackRuntime& st) const;
    /// Test seam: what the focused stack is showing (under ``run_in_view``,
    /// the view's).
    [[nodiscard]] QString current_title() const
    {
        return title_of_stack(session_.focused_stack().program);
    }

    Q_INVOKABLE bool selectServiceItem(int index, bool live = false);
    /// Select a service item and, for ``image_folder``, start at ``image_index``.
    Q_INVOKABLE bool selectServiceItemAtImage(int index, int image_index, bool live = false);
    bool next_slide_on_stack(const QString& stack_id);
    bool jump_to_line_on_stack(const QString& stack_id, int line_index);
    /// Rundown plan: item pin, then type default. Ignores the armed target button.
    Q_INVOKABLE QString resolvePlannedStack(const QString& item_type,
                                            const QString& item_override = QString()) const;

    /// Blank or freeze every output not exempt (``Session::toggle_global_blank``).
    Q_INVOKABLE void toggleGlobalBlank();
    Q_INVOKABLE void toggleGlobalFreeze();
    /// Blank or freeze the output ``output_id``.
    Q_INVOKABLE bool toggleOutputBlank(const QString& output_id);
    Q_INVOKABLE bool toggleOutputFreeze(const QString& output_id);
    /// Lift every blank and freeze, of every output and every stack.
    Q_INVOKABLE void clearBlankFreeze();

    /// Standalone live Freeform from the library (Play).
    Q_INVOKABLE bool playFreeform(qint64 freeform_id, bool live = false);
    /// Standalone live song from the library.
    Q_INVOKABLE bool playSong(qint64 song_id, bool live = false);
    /// Standalone live image from the library.
    Q_INVOKABLE bool playImage(const QString& filename, bool live = false);
    /// Play a library video live. No open service required.
    Q_INVOKABLE bool playVideo(const QString& filename, bool loop = false, bool live = false);
    /// Play a library audio file live, on the audio stack. No open service required.
    Q_INVOKABLE bool playAudio(const QString& filename, bool loop = false, bool live = false);
    /// Standalone live bible from the current browse selection.
    Q_INVOKABLE bool playBible(int verse_start, int verse_end, bool live = false);
    /// Live bible from explicit coordinates, independent of the browse
    /// selection. playBible() forwards here; the web remote calls it directly so
    /// a phone picking a passage cannot move the operator's browse state.
    bool play_bible_ref(qint64 bible_id,
                        const QString& book_name,
                        int chapter,
                        int verse_start,
                        int verse_end,
                        session::Land land = session::Land::Cue);
    /// C++ / tests: play a standalone song (not exposed to QML).
    void play_song(model::Song song, session::Land land = session::Land::Cue);

signals:
    /// Live content moved. ``rebuildModels``: rows were added or removed.
    void liveChanged(bool rebuildModels);
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);

private:
    /// ``playImage`` / ``playVideo`` / ``playAudio``, which pass their own name
    /// as ``entry`` for the error report. ``loop`` is for video and audio.
    bool play_media_file(const char* entry,
                         db::MediaKind kind,
                         const QString& filename,
                         bool loop,
                         bool live);
    /// Announce a change of what is live.
    void changed(bool rebuild_models) { emit liveChanged(rebuild_models); }

    session::Session& session_;
    LibraryController& library_;
    FreeformLibraryController& freeform_library_;
    /// Titles the library files played from it.
    const MediaController& media_;
};

}  // namespace seventhslide::app
