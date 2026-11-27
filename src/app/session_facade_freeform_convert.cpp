// SessionFacade: turning a song or Bible passage into a Freeform item.
//
// One slice of SessionFacade (the class is declared in session_facade.hpp):
// "Save as Freeform" from a library song and from a Bible reference. Each
// converts, copies into the library any file a background theme owns that its
// slides name, then stores the result through the library.

#include "session_facade.hpp"

#include "background_theme_copier.hpp"
#include "entry_error.hpp"
#include "refusals.hpp"
#include "freeform_library_controller.hpp"
#include "font_controller.hpp"
#include "library_controller.hpp"
#include "media_controller.hpp"

#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/freeform_convert.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace seventhslide::app {

QString SessionFacade::why_fonts_still_coming(const freeform::Document& doc) const
{
    // A Freeform item keeps where each line broke: laid out in the stand-in,
    // it would keep the stand-in's breaks once the font came.
    for (const auto& family : db::font_families_named_in(doc.to_json())) {
        for (const auto& path : fonts_->files_not_here(family)) {
            if (media_->on_its_way(path)) {
                return tr("The font “%1” is still on its way from another computer. Save as "
                          "Freeform once it has come, so the words are laid out in it.")
                    .arg(QString::fromStdString(family));
            }
        }
    }
    return {};
}

QVariantMap SessionFacade::saveSongAsFreeform(qint64 song_id, const QString& name)
try {
    QVariantMap out;
    out.insert(QStringLiteral("id"), static_cast<qint64>(-1));
    if (!freeform_library_->repo() || !song_repo()) {
        out.insert(QStringLiteral("error"), refusal::no_library());
        return out;
    }
    const auto song = song_id < 0 ? std::nullopt : song_repo()->get(song_id);
    if (!song) {
        out.insert(QStringLiteral("error"), refusal::song_gone());
        return out;
    }
    build::SlideBuildInput in;
    in.kind = build::BuildKind::Song;
    in.selection.kind = model::ContentKind::Song;
    in.selection.theme_priority = session_.theme_priority();
    in.outputs = session_.outputs();
    in.song = *song;
    in.ccli_licence_number = open_library_.settings().ccli_licence_number;
    const auto built = build::build_slides(in, previews_.measurer());
    scene::FreeformConvertOptions opts;
    const auto trimmed = name.trimmed();
    opts.name = trimmed.isEmpty() ? song->title : trimmed.toStdString();
    if (opts.name.empty()) {
        opts.name = "Untitled";
    }
    // The tokens go out whatever the toggle says - a title design may use
    // ``{song-title}`` or ``{authors}`` for its own reasons. The toggle only
    // decides whether the notice elements paint.
    opts.copyright_vars =
        scene::copyright_token_values(*song, open_library_.settings().ccli_licence_number);
    opts.song_copyright = song->show_copyright;
    auto doc =
        scene::document_from_build(built, in.outputs, previews_.measurer(), opts);
    if (const auto why = why_fonts_still_coming(doc); !why.isEmpty()) {
        out.insert(QStringLiteral("error"), why);
        return out;
    }
    return save_converted(std::move(doc));
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

QVariantMap SessionFacade::saveBibleAsFreeform(int verse_start, int verse_end,
                                              const QString& name)
try {
    QVariantMap out;
    out.insert(QStringLiteral("id"), static_cast<qint64>(-1));
    const qint64 bible_id = library_->selectedBibleId();
    if (bible_id < 0) {
        out.insert(QStringLiteral("error"), refusal::no_bible_chosen());
        return out;
    }
    return save_bible_ref_as_freeform(bible_id, library_->selectedBibleBook(),
                                      library_->selectedBibleChapter(), verse_start, verse_end,
                                      name);
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

QVariantMap SessionFacade::save_bible_ref_as_freeform(qint64 bible_id,
                                                      const QString& book_name,
                                                      int chapter,
                                                      int verse_start,
                                                      int verse_end,
                                                      const QString& name)
{
    QVariantMap out;
    out.insert(QStringLiteral("id"), static_cast<qint64>(-1));
    if (!freeform_library_->repo()) {
        out.insert(QStringLiteral("error"), refusal::no_library());
        return out;
    }
    auto live = library_->bible_passage(bible_id, book_name, chapter, verse_start, verse_end);
    if (!live) {
        out.insert(QStringLiteral("error"), refusal::passage_not_found());
        return out;
    }
    build::SlideBuildInput in;
    in.kind = build::BuildKind::Bible;
    in.selection.kind = model::ContentKind::Bible;
    in.selection.theme_priority = session_.theme_priority();
    in.outputs = session_.outputs();
    in.bible.chapter = live->chapter;
    in.bible.verses = live->verses;
    const auto built = build::build_slides(in, previews_.measurer());
    scene::FreeformConvertOptions opts;
    const auto trimmed = name.trimmed();
    opts.name = trimmed.isEmpty() ? live->ref : trimmed.toStdString();
    if (opts.name.empty()) {
        opts.name = "Untitled";
    }
    opts.bible_ref_vars = scene::bible_ref_token_values(*live);
    auto doc =
        scene::document_from_build(built, in.outputs, previews_.measurer(), opts);
    if (const auto why = why_fonts_still_coming(doc); !why.isEmpty()) {
        out.insert(QStringLiteral("error"), why);
        return out;
    }
    return save_converted(std::move(doc));
}

QVariantMap SessionFacade::save_converted(freeform::Document doc)
{
    // Saved once its slides own their backgrounds: at once,
    // or, where a background theme's own picture or video is copied into the
    // library first, once that is in, and only into the library it came from.
    struct Saving {
        bool waited = false;
        std::optional<qint64> id;
    };
    auto saving = std::make_shared<Saving>();
    const std::string library = db::library_uid(*open_library_.database());
    bg_copier_->own_theme_files(std::move(doc), [this, saving, library](
                                                    freeform::Document owned,
                                                    const QStringList& errors) {
        for (const auto& e : errors) {
            emit errorOccurred(e);
        }
        if (!saving->waited) {
            saving->id = freeform_library_->add_document(std::move(owned));
            return;
        }
        // Not under the caller's catch: a failure is reported here.
        try {
            auto* db = open_library_.database();
            if (db == nullptr || db::library_uid(*db) != library) {
                emit errorOccurred(tr("The Freeform item was not saved: its library was "
                                      "closed while its backgrounds were copied in."));
                return;
            }
            if (freeform_library_->add_document(std::move(owned)) < 0) {
                emit errorOccurred(tr("The Freeform item was not saved: no library is open."));
            }
        } catch (const std::exception& ex) {
            emit errorOccurred(report_entry_error("save_converted", ex));
        }
    });
    QVariantMap out;
    out.insert(QStringLiteral("id"), static_cast<qint64>(-1));
    out.insert(QStringLiteral("error"), QString{});
    if (!saving->id) {
        saving->waited = true;
        return out;
    }
    if (*saving->id < 0) {
        out.insert(QStringLiteral("error"), tr("The Freeform item was not saved: no library is open."));
        return out;
    }
    out.insert(QStringLiteral("id"), *saving->id);
    return out;
}

} // namespace seventhslide::app
