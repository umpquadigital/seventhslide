// What is live, and moving through it (see the header).

#include "live_controller.hpp"

#include "entry_error.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "media_controller.hpp"

#include <seventhslide/json/read.hpp>
#include <seventhslide/model/output_library.hpp>

#include <utility>

namespace seventhslide::app {

namespace {

QString mode_string(session::SessionMode mode)
{
    switch (mode) {
    case session::SessionMode::Song:
        return QStringLiteral("song");
    case session::SessionMode::Bible:
        return QStringLiteral("bible");
    case session::SessionMode::Service:
        return QStringLiteral("service");
    case session::SessionMode::Freeform:
        return QStringLiteral("freeform");
    case session::SessionMode::Video:
        return QStringLiteral("video");
    case session::SessionMode::Audio:
        return QStringLiteral("audio");
    case session::SessionMode::Image:
        return QStringLiteral("image");
    case session::SessionMode::Web:
        return QStringLiteral("web");
    case session::SessionMode::Live:
        return QStringLiteral("live");
    case session::SessionMode::Empty:
        return QStringLiteral("empty");
    }
    return QStringLiteral("empty");
}

}  // namespace

LiveController::LiveController(session::Session& session,
                               LibraryController& library,
                               FreeformLibraryController& freeform_library,
                               const MediaController& media,
                               QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , freeform_library_(freeform_library)
    , media_(media)
{
}

namespace {

/// Where a send lands: straight on the outputs, or as its stack has it.
[[nodiscard]] session::Land land_of(bool live) noexcept
{
    return live ? session::Land::Live : session::Land::Cue;
}

}  // namespace

QString LiveController::mode() const
{
    return mode_string(session_.mode());
}

bool LiveController::selectServiceItem(int index, bool live)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.select_service_item(index, true, land_of(live)); },
                          [this] { changed(true); });
}

bool LiveController::selectServiceItemAtImage(int index, int image_index, bool live)
{
    return guarded_change(
        *this, __func__,
        [&] {
            const auto landed = session_.select_service_item(index, true, land_of(live));
            // It starts at that picture on the side it landed on, cued or live.
            if (auto* st = landed && image_index > 0 ? session_.runtime(*landed) : nullptr) {
                (void)st->jump_to_line(image_index);
            }
            return landed.has_value();
        },
        [this] { changed(true); });
}

bool LiveController::next_slide_on_stack(const QString& stack_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.next_slide_on({stack_id.trimmed().toStdString()}); },
                          [this] { changed(false); });
}

bool LiveController::jump_to_line_on_stack(const QString& stack_id, int line_index)
{
    return guarded_change(*this, __func__,
                          [&] {
                              return session_.jump_to_line_on({stack_id.trimmed().toStdString()},
                                                              line_index);
                          },
                          [this] { changed(false); });
}

QString LiveController::resolvePlannedStack(const QString& item_type,
                                            const QString& item_override) const
{
    return QString::fromStdString(session_.resolve_planned_stack(
        item_type.trimmed().toStdString(), item_override.trimmed().toStdString()));
}

// Blank and freeze change what outputs paint, not which rows the models hold.
void LiveController::toggleGlobalBlank()
try {
    session_.toggle_global_blank();
    changed(false);
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

void LiveController::toggleGlobalFreeze()
try {
    session_.toggle_global_freeze();
    changed(false);
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

bool LiveController::toggleOutputBlank(const QString& output_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.toggle_output_blank(output_id.toStdString()); },
                          [this] { changed(false); });
}

bool LiveController::toggleOutputFreeze(const QString& output_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.toggle_output_freeze(output_id.toStdString()); },
                          [this] { changed(false); });
}

void LiveController::clearBlankFreeze()
try {
    session_.clear_blank_freeze();
    changed(false);
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

bool LiveController::playFreeform(qint64 freeform_id, bool live)
try {
    auto content = freeform_library_.live_content(freeform_id);
    if (!content) {
        return false;
    }
    session_.play_freeform(std::move(*content), {}, land_of(live));
    changed(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

bool LiveController::playSong(qint64 song_id, bool live)
try {
    auto* repo = library_.song_repo();
    if (!repo || song_id < 0) {
        return false;
    }
    const auto song = repo->get(song_id);
    if (!song) {
        return false;
    }

    play_song(*song, land_of(live));
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

void LiveController::play_song(model::Song song, session::Land land)
{
    session_.play_song(std::move(song), {}, land);
    // A library song goes live on a stack; the open service stays open, so
    // its name and list selection are left alone (as every other play path does).
    changed(true);
}

bool LiveController::playImage(const QString& filename, bool live)
{
    return play_media_file(__func__, db::MediaKind::Image, filename, false, live);
}

bool LiveController::playVideo(const QString& filename, bool loop, bool live)
{
    return play_media_file(__func__, db::MediaKind::Video, filename, loop, live);
}

bool LiveController::playAudio(const QString& filename, bool loop, bool live)
{
    return play_media_file(__func__, db::MediaKind::Audio, filename, loop, live);
}

bool LiveController::play_media_file(const char* entry,
                                     db::MediaKind kind,
                                     const QString& filename,
                                     bool loop,
                                     bool live)
try {
    const auto fn = filename.trimmed().toStdString();
    if (fn.empty()) {
        return false;
    }
    session::LiveMediaContent media;
    switch (kind) {
    case db::MediaKind::Image:
        media.kind = model::ContentKind::Image;
        break;
    case db::MediaKind::Video:
        media.kind = model::ContentKind::Video;
        break;
    case db::MediaKind::Audio:
        media.kind = model::ContentKind::Audio;
        break;
    }
    media.data = media_.item_data(kind, fn, loop);
    session_.play_media(std::move(media), {}, land_of(live));
    changed(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(entry, ex));
    return false;
}

bool LiveController::playBible(int verse_start, int verse_end, bool live)
{
    // See addBibleToService: the browse selection is resolved here and the work
    // happens in the explicit-coordinate overload.
    if (library_.selectedBibleId() < 0) {
        return false;
    }
    return play_bible_ref(library_.selectedBibleId(), library_.selectedBibleBook(),
                          library_.selectedBibleChapter(), verse_start, verse_end,
                          land_of(live));
}

bool LiveController::play_bible_ref(qint64 bible_id,
                                    const QString& book_name,
                                    int chapter,
                                    int verse_start,
                                    int verse_end,
                                    session::Land land)
try {
    auto passage = library_.bible_passage(bible_id, book_name, chapter, verse_start, verse_end);
    if (!passage) {
        return false;
    }
    session_.play_bible(std::move(*passage), {}, land);
    changed(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

QString LiveController::title_of_stack(const session::StackRuntime& st) const
{
    const model::ServiceItem* item = nullptr;
    if (const auto id = st.service_item_id()) {
        for (const auto& it : session_.service_items()) {
            if (it.item_id == id) {
                item = &it;
                break;
            }
        }
    }
    if (st.bible_mode_active()) {
        const auto ref = QString::fromStdString(st.bible().ref);
        return ref.isEmpty() ? QStringLiteral("Bible") : ref;
    }
    if (st.freeform_mode_active()) {
        if (!st.freeform().name.empty()) {
            return QString::fromStdString(st.freeform().name);
        }
        if (item) {
            const auto name = json::str_or(item->data, "name");
            return QString::fromStdString(name.empty() ? item->title() : name);
        }
        return QStringLiteral("Freeform");
    }
    if (st.has_transport()) {
        return QString::fromStdString(st.playback().title);
    }
    if (item) {
        return QString::fromStdString(item->title());
    }
    if (st.content_kind() == model::ContentKind::Song) {
        return QString::fromStdString(st.song().title);
    }
    return {};
}

}  // namespace seventhslide::app

