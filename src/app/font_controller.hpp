#pragma once

// The open library's fonts as the rest of the app sees them (``session.fonts``):
// fills the FontRegistry that slides are drawn from, has a library take its copy
// of the shipped fonts (once, where it was made), keeps what no font could be
// found for, for the operator, and adds and removes fonts for Settings' Fonts
// page.
//
// A font changing under text changes how it wraps, so nothing that lays text out
// again happens while something is live: a sync bringing fonts and files that
// finish being added all wait (``retry_waiting``). The page adds and removes
// nothing while something is live.
//
// Files being added are read, checked and written on a worker: a collection can
// be large. Only putting them in the library is on the GUI thread.
//
// Seam: Qt Core, over db/ and fonts/. Call on the GUI thread.

#include "font_store.hpp"

#include <seventhslide/layout/qt_font_faces.hpp>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {

class OpenLibrary;
class PreviewResources;

class FontController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns the library's fonts: session.fonts")
    /// Every family a text can pick: the library's.
    Q_PROPERTY(QStringList families READ families NOTIFY fontsChanged)
    /// Families something asked for that there is no font for, shown in a
    /// stand-in.
    Q_PROPERTY(QStringList missing READ missing NOTIFY missingChanged)
    /// Changes when ``painted`` may answer otherwise, for a binding to follow.
    Q_PROPERTY(int revision READ revision NOTIFY fontsChanged)
    /// Settings' list: every family there is to say something about, the missing
    /// first, then the library's. ``{ name, state ("missing" | "library"), faces,
    /// named }`` each, ``named`` when something in the library names it.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    /// Files are being added.
    Q_PROPERTY(bool adding READ adding NOTIFY addingChanged)
    /// Something is live, so fonts cannot be added or removed now.
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    /// What shows for a family there is no font for.
    Q_PROPERTY(QString standIn READ standIn CONSTANT)
public:
    /// What a file being added came to; a synced library's size limit, as
    /// ``MediaController::ImportNote``.
    using ImportNote = std::function<QString(const QStringList& titles, std::int64_t bytes)>;

    FontController(OpenLibrary& library, PreviewResources& previews, QObject* parent = nullptr);
    ~FontController() override;

    /// Whether the library takes writes here (a viewer's does not).
    void set_may_write(std::function<bool()> may_write) { may_write_ = std::move(may_write); }
    /// Whether the library may take its copy of the shipped fonts here: only
    /// where it was made, not on a computer that joined it, which has the
    /// fonts the others have, no more and no less. Yes when unset.
    void set_may_take_shipped(std::function<bool()> may_take) { may_take_shipped_ = std::move(may_take); }
    /// Whether text may be laid out again now: nothing is live.
    void set_may_reload(std::function<bool()> may_reload) { may_reload_ = std::move(may_reload); }
    /// Files added may go over a synced library's size limit.
    void set_import_note(ImportNote note) { import_note_ = std::move(note); }
    /// Whether library file ``path`` ("fonts/<name>") is not here but on its
    /// way from another computer (``MediaController::on_its_way``).
    void set_on_its_way(std::function<bool(const std::string& path)> on_its_way)
    {
        on_its_way_ = std::move(on_its_way);
    }

    /// A library opened, with its database and media folder set.
    void library_opened();
    void library_closed();
    /// A sync changed ``font_files``.
    void library_fonts_changed();
    /// What was waiting for nothing to be live, if nothing now is.
    void retry_waiting();
    /// A sync may have brought files. A row can come ahead of its file, and text
    /// in it shows the stand-in until the file is here.
    void files_may_have_arrived();

    [[nodiscard]] QStringList families() const;
    [[nodiscard]] QStringList missing() const;
    [[nodiscard]] int revision() const noexcept { return revision_; }
    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] bool adding() const noexcept { return adding_; }
    [[nodiscard]] bool live() const { return !may_reload(); }
    [[nodiscard]] static QString standIn();
    /// The library's files for ``family`` that are not here (a row can come
    /// ahead of its file), by path under the media folder ("fonts/<name>").
    [[nodiscard]] std::vector<std::string> files_not_here(const std::string& family) const;

    /// The family name Qt knows ``family`` by, loaded and ready: the library's or
    /// the stand-in's, as slides show it. For a view that sets a font itself;
    /// ``revision`` is only there for its binding to follow.
    Q_INVOKABLE [[nodiscard]] QString painted(const QString& family, int revision = 0) const;

    /// Read again what the library names: a theme may have moved off a font.
    Q_INVOKABLE void refresh();
    /// ``family``'s faces, to show: ``{ filename, style, loaded, qtFamily,
    /// qtStyle, weight, italic }`` each, what it takes to set a ``font`` in QML.
    /// ``revision`` is only there for a binding to follow.
    Q_INVOKABLE [[nodiscard]] QVariantList faces(const QString& family, int revision = 0);
    /// What in the library names ``family``, for Settings to say where a font
    /// is used and open it: ``{ kind ("theme" | "output" | "profile" | "song" |
    /// "freeform" | "service"), name, within, id, type }`` each, as
    /// ``db::FontUse`` has them. Reads the whole library, so for a page to ask
    /// once per family it shows. ``revision`` is only there for a binding to
    /// follow. A library that cannot be read answers none, and says so by
    /// ``errorOccurred``: the page would otherwise say nothing uses it.
    Q_INVOKABLE [[nodiscard]] QVariantList uses(const QString& family, int revision = 0);
    /// Let go of what ``faces`` loaded into Qt.
    Q_INVOKABLE void releasePreviews();
    /// Add the font files at ``urls``. Says what each came to by ``addFinished``.
    Q_INVOKABLE void addFiles(const QStringList& urls);
    /// The same for an import: a face goes in only where the library has no
    /// face in its slot, as its file says when it is added
    /// (``FontStore::add_checked``'s ``keep_occupied``), so what the library
    /// shows is never changed. While another add is under way it waits its
    /// turn; while something is live it waits, as every add does. ``hold``
    /// keeps the files there (an import's unpacked copies) until this add is
    /// done with them, however it ends. Nothing is added to a library this
    /// computer may not write to, or once the library closes.
    void add_files_keeping(std::vector<std::filesystem::path> paths, std::shared_ptr<void> hold);
    /// Why faces could not be removed, in words for people, or empty.
    Q_INVOKABLE [[nodiscard]] QString removeFaces(const QStringList& filenames);
    Q_INVOKABLE [[nodiscard]] QString removeFamily(const QString& family);
    /// What a remove confirm adds when files of ``family``, or the faces
    /// ``filenames`` when any are given, are on their way: the computers that
    /// have them lose them too. Empty when none are.
    Q_INVOKABLE [[nodiscard]] QString arrivingRemoveNote(const QString& family,
                                                         const QStringList& filenames) const;

signals:
    void fontsChanged();
    void missingChanged();
    void rowsChanged();
    void addingChanged();
    void liveChanged();
    /// The fonts under text changed: lay it out again.
    void fontsReloaded();
    /// Files added have been dealt with: ``{ file, ok, text }`` each, and what to
    /// add about them all, or "".
    void addFinished(const QVariantList& results, const QString& note);
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);

private:
    /// A file being added, and what the worker made of it.
    struct AddingFile {
        std::string name;
        FontStore::Checked checked;
    };
    void on_asked(const std::string& family, std::uint64_t library_revision);
    void reload();
    /// Have the library take its copy of the shipped fonts, if it has not and
    /// may be written to here: written on the worker, as files added are.
    void take_shipped();
    void finish_taking(std::uint64_t generation, const FontStore::Checked& checked);
    void read_named();
    void note_live();
    void start_adding(std::vector<std::filesystem::path> paths, bool keep_occupied, std::shared_ptr<void> hold);
    /// An add has ended: let go of its files, and start the next waiting.
    void adding_ended();
    void finish_adding(std::uint64_t generation, std::vector<AddingFile> files);
    void commit_adding(std::vector<AddingFile> files);
    /// Why nothing can be added or removed now, in words for people, or empty.
    [[nodiscard]] QString why_not_change() const;
    /// The open library's store, if fonts may be added or removed now; if
    /// not, nothing, and why_not_change() says why.
    [[nodiscard]] std::optional<FontStore> store_to_change() const;
    /// A library that was open is not any more: drop what was for it.
    void forget_library();
    /// Take back the files waiting for nothing to be live.
    void discard_waiting();
    /// A file text showed the stand-in for is here, not tried before.
    [[nodiscard]] bool missing_file_arrived();
    /// So is one ``faces`` could not load: forgets it, to be loaded again.
    [[nodiscard]] bool failed_preview_arrived();
    [[nodiscard]] bool may_write() const { return !may_write_ || may_write_(); }
    [[nodiscard]] bool may_take_shipped() const { return !may_take_shipped_ || may_take_shipped_(); }
    [[nodiscard]] bool may_reload() const { return !may_reload_ || may_reload_(); }
    /// Over the open library, when it has a media folder to keep fonts in.
    [[nodiscard]] std::optional<FontStore> store() const;

    /// families() at a revision: it queries the library, and the editor's
    /// font menus read it on every edit. Keyed by the revision, not cleared by
    /// a slot, as QML hears fontsChanged before any slot runs.
    mutable std::optional<std::pair<int, QStringList>> families_;
    OpenLibrary& library_;
    PreviewResources& previews_;
    std::function<bool()> may_write_;
    std::function<bool()> may_take_shipped_;
    std::function<bool()> may_reload_;
    ImportNote import_note_;
    std::function<bool(const std::string& path)> on_its_way_;
    std::set<std::string> missing_;
    /// What the library names (FontRepository::families_named).
    std::set<std::string> named_;
    /// By path, what ``faces`` has loaded into Qt.
    std::map<std::string, layout::QtFontFaces::LoadedFile> previews_loaded_;
    /// Files of missing families already tried, so a damaged one is tried once.
    std::set<std::filesystem::path> files_tried_;
    /// Files changed under the same rows: what Qt holds of them is out of date.
    bool files_changed_ = false;
    /// Files finished while something was live.
    std::optional<std::vector<AddingFile>> adding_waiting_;
    /// Which library's open: files arriving for another are taken back.
    std::uint64_t generation_ = 0;
    bool adding_ = false;
    /// The add under way keeps the library's faces in their slots.
    bool adding_keeps_ = false;
    /// What keeps the add under way's files there.
    std::shared_ptr<void> adding_hold_;
    /// Imports' adds waiting for the one under way.
    struct KeepingAdd {
        std::vector<std::filesystem::path> paths;
        std::shared_ptr<void> hold;
    };
    std::deque<KeepingAdd> keeping_waiting_;
    bool reload_waiting_ = false;
    bool was_live_ = false;
    int revision_ = 0;
    /// One thread, so files are added in the order they were asked for.
    QThreadPool workers_;
};

}  // namespace seventhslide::app
