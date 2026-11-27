#include "font_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "local_file_path.hpp"
#include "open_library.hpp"
#include "preview_resources.hpp"

#include <seventhslide/canvas/box.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/search_text.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/util/leaf_filename.hpp>
#include <seventhslide/util/strings.hpp>

#include <QFontDatabase>
#include <QMetaObject>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <system_error>
#include <utility>

namespace seventhslide::app {

FontController::FontController(OpenLibrary& library, PreviewResources& previews, QObject* parent)
    : QObject(parent), library_(library), previews_(previews)
{
    workers_.setMaxThreadCount(1);
    connect(this, &FontController::fontsChanged, this, &FontController::rowsChanged);
    connect(this, &FontController::missingChanged, this, &FontController::rowsChanged);
    auto& registry = previews_.fonts();
    registry.set_shipped_fonts(FontStore::shipped_index());
    registry.set_stand_in_family(std::string(canvas::k_default_font_family));
    // From any thread that lays out text, under the registry's lock: only post.
    registry.set_listener([this](const std::string& family, std::uint64_t library_revision) {
        QMetaObject::invokeMethod(
            this, [this, family, library_revision] { on_asked(family, library_revision); },
            Qt::QueuedConnection);
    });
}

FontController::~FontController()
{
    previews_.fonts().set_listener({});
    workers_.waitForDone();
    discard_waiting();
    releasePreviews();
}

std::optional<FontStore> FontController::store() const
{
    auto* db = library_.database();
    if (db == nullptr || library_.media_root().empty()) {
        return std::nullopt;
    }
    return std::optional<FontStore>(std::in_place, *db, library_.media_root());
}

void FontController::discard_waiting()
{
    if (adding_waiting_) {
        for (const auto& file : *adding_waiting_) {
            FontStore::discard(file.checked);
        }
    }
}

void FontController::forget_library()
{
    ++generation_;
    // Imports waiting were for this library alone.
    keeping_waiting_.clear();
    if (adding_waiting_) {
        discard_waiting();
        adding_waiting_.reset();
        adding_ = false;
        adding_keeps_ = false;
        // The worker is done with its files; one still reading keeps them
        // until it reports (``finish_adding``).
        adding_hold_.reset();
        emit addingChanged();
    }
    releasePreviews();
    missing_.clear();
    files_tried_.clear();
    files_changed_ = false;
    reload_waiting_ = false;
}

void FontController::library_opened()
{
    forget_library();
    auto opened = store();
    previews_.fonts().set_library_fonts(opened ? opened->library_index() : fonts::FontIndex{});
    read_named();
    // Another library's font may go by the same name.
    previews_.measurer().clear_cache();
    ++revision_;
    emit fontsChanged();
    emit missingChanged();
    // Once the open has settled, so whether this computer only views the
    // library is known, even where a library opens before sync is set up.
    QMetaObject::invokeMethod(
        this,
        [this, generation = generation_] {
            if (generation == generation_) {
                take_shipped();
            }
        },
        Qt::QueuedConnection);
}

void FontController::library_closed()
{
    forget_library();
    named_.clear();
    previews_.fonts().set_library_fonts({});
    previews_.measurer().clear_cache();
    ++revision_;
    emit fontsChanged();
    emit missingChanged();
}

void FontController::library_fonts_changed()
{
    reload_waiting_ = true;
    retry_waiting();
}

void FontController::note_live()
{
    if (const bool now = live(); now != was_live_) {
        was_live_ = now;
        emit liveChanged();
    }
}

void FontController::retry_waiting()
{
    note_live();
    if (!may_reload()) {
        return;
    }
    if (auto waiting = std::exchange(adding_waiting_, std::nullopt)) {
        commit_adding(std::move(*waiting));
    }
    if (reload_waiting_) {
        reload();
    }
}

void FontController::files_may_have_arrived()
{
    if (missing_file_arrived()) {
        files_changed_ = true;
        library_fonts_changed();
    } else if (failed_preview_arrived()) {
        ++revision_;
        emit fontsChanged();
    }
}

std::vector<std::string> FontController::files_not_here(const std::string& family) const
{
    std::vector<std::string> out;
    const auto index = previews_.fonts().library_fonts();
    const auto faces = index.find(util::to_lower(family));
    if (faces == index.end()) {
        return out;
    }
    for (const auto& face : faces->second) {
        std::error_code ec;
        if (!std::filesystem::exists(face.path, ec)) {
            out.push_back(std::string(model::k_fonts_subdir) + "/" + face.path.filename().string());
        }
    }
    return out;
}

QString FontController::arrivingRemoveNote(const QString& family, const QStringList& filenames) const
{
    if (!on_its_way_) {
        return {};
    }
    auto coming = files_not_here(family.toStdString());
    if (!filenames.isEmpty()) {
        std::set<std::string> picked;
        for (const auto& filename : filenames) {
            picked.insert(std::string(model::k_fonts_subdir) + "/"
                          + util::leaf_filename(filename.toStdString()));
        }
        std::erase_if(coming, [&](const std::string& path) { return !picked.contains(path); });
    }
    if (std::ranges::none_of(coming, on_its_way_)) {
        return {};
    }
    return tr("Its files are still on their way to this computer: the computers that have them "
              "lose them too.");
}

bool FontController::missing_file_arrived()
{
    if (missing_.empty()) {
        return false;
    }
    auto opened = store();
    if (!opened) {
        return false;
    }
    const auto index = opened->library_index();
    bool arrived = false;
    for (const auto& family : missing_) {
        const auto faces = index.find(util::to_lower(family));
        if (faces == index.end()) {
            continue;
        }
        for (const auto& face : faces->second) {
            std::error_code ec;
            if (std::filesystem::exists(face.path, ec) && files_tried_.insert(face.path).second) {
                arrived = true;
            }
        }
    }
    return arrived;
}

bool FontController::failed_preview_arrived()
{
    return std::erase_if(previews_loaded_, [this](const auto& preview) {
               std::error_code ec;
               return !preview.second.loaded && std::filesystem::exists(preview.first, ec)
                   && files_tried_.insert(preview.first).second;
           })
        > 0;
}

void FontController::reload()
{
    reload_waiting_ = false;
    auto opened = store();
    auto fonts = opened ? opened->library_index() : fonts::FontIndex{};
    if (fonts == previews_.fonts().library_fonts() && !files_changed_) {
        return;
    }
    files_changed_ = false;
    // What is held for a preview may be the file just replaced, of a name and
    // style Qt would then have two of.
    releasePreviews();
    previews_.fonts().set_library_fonts(std::move(fonts));
    previews_.measurer().clear_cache();
    // Each is asked for again, and told of again if it is still missing.
    missing_.clear();
    read_named();
    ++revision_;
    emit missingChanged();
    emit fontsChanged();
    emit fontsReloaded();
}

void FontController::read_named()
{
    auto* db = library_.database();
    if (db == nullptr) {
        named_.clear();
        return;
    }
    try {
        named_ = db::FontRepository(*db).families_named();
    } catch (const std::exception& ex) {
        // Only what the page has to say: text still shows, with its stand-in.
        qWarning("SeventhSlide: could not read which fonts the library names: %s", ex.what());
    }
}

void FontController::refresh()
{
    read_named();
    emit missingChanged();
}

void FontController::on_asked(const std::string& family, std::uint64_t library_revision)
{
    // Looked up before the fonts it had no font among were replaced: it is
    // looked up again, in those there are now, if it is still wanted.
    if (library_revision != previews_.fonts().library_revision()) {
        return;
    }
    if (missing_.insert(family).second) {
        qWarning("SeventhSlide: no font \"%s\" in this library; showing %.*s in its place",
                 family.c_str(), static_cast<int>(canvas::k_default_font_family.size()),
                 canvas::k_default_font_family.data());
        emit missingChanged();
    }
}

void FontController::take_shipped()
{
    auto opened = store();
    if (!opened || !may_write() || !may_take_shipped()) {
        return;
    }
    std::optional<std::vector<std::string>> families;
    try {
        families = opened->bundled_to_take();
    } catch (const std::exception& ex) {
        qWarning("SeventhSlide: could not read whether the shipped fonts were copied in: %s",
                 ex.what());
    }
    if (!families) {
        return;
    }
    workers_.start([this, families = std::move(*families), media_root = library_.media_root(),
                    generation = generation_] {
        auto checked = FontStore::write_bundled(families, media_root);
        QMetaObject::invokeMethod(
            this,
            [this, generation, checked = std::move(checked)] { finish_taking(generation, checked); },
            Qt::QueuedConnection);
    });
}

void FontController::finish_taking(std::uint64_t generation, const FontStore::Checked& checked)
{
    auto opened = store();
    // For a library that has gone, or turned view only here: it is copied in
    // the next time it opens where it may be.
    if (generation != generation_ || !opened || !may_write() || !may_take_shipped()) {
        FontStore::discard(checked);
        return;
    }
    const auto added = opened->take_bundled(checked);
    if (!added.error.empty()) {
        // Tried again the next time the library opens.
        qWarning("SeventhSlide: could not copy the shipped fonts in: %s", added.error.c_str());
    } else if (!added.faces.empty()) {
        library_fonts_changed();
    }
}

QStringList FontController::families() const
{
    if (families_ && families_->first == revision_) {
        return families_->second;
    }
    QStringList out;
    if (auto opened = store()) {
        for (const auto& family : opened->library_families()) {
            out.push_back(QString::fromStdString(family));
        }
    }
    std::ranges::sort(out, db::name_collator());
    families_.emplace(revision_, out);
    return out;
}

QStringList FontController::missing() const
{
    // By name, one each. What was asked for and could not be had is missing even
    // with a row in the library: its file is not here yet, or is damaged.
    std::map<std::string, QString> names;
    for (const auto& family : missing_) {
        names.emplace(util::to_lower(family), QString::fromStdString(family));
    }
    const auto library = previews_.fonts().library_fonts();
    for (const auto& family : named_) {
        const auto key = util::to_lower(family);
        if (!library.contains(key)) {
            names.emplace(key, QString::fromStdString(family));
        }
    }
    QStringList out;
    out.reserve(static_cast<qsizetype>(names.size()));
    for (const auto& [key, name] : names) {
        out.push_back(name);
    }
    std::ranges::sort(out, db::name_collator());
    return out;
}

QVariantList FontController::rows() const
{
    std::map<std::string, int> faces;
    std::map<std::string, QString> library;
    if (auto opened = store()) {
        for (const auto& face : opened->library_faces()) {
            const auto key = util::to_lower(face.family);
            ++faces[key];
            library.emplace(key, QString::fromStdString(face.family));
        }
    }
    std::set<std::string> named;
    for (const auto& family : named_) {
        named.insert(util::to_lower(family));
    }

    const auto row = [&](const QString& name, const char* state) {
        const auto key = util::to_lower(name.toStdString());
        const auto count = faces.find(key);
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("state"), QLatin1String(state)},
                           {QStringLiteral("faces"), count == faces.end() ? 0 : count->second},
                           {QStringLiteral("named"), named.contains(key)}};
    };
    QVariantList out;
    const auto missing_names = missing();
    std::set<std::string> said;
    for (const auto& name : missing_names) {
        said.insert(util::to_lower(name.toStdString()));
        out.push_back(row(name, "missing"));
    }
    QStringList in_library;
    for (const auto& [key, name] : library) {
        if (!said.contains(key)) {
            in_library.push_back(name);
        }
    }
    std::ranges::sort(in_library, db::name_collator());
    for (const auto& name : in_library) {
        out.push_back(row(name, "library"));
    }
    return out;
}

QString FontController::painted(const QString& family, int /*revision*/) const
{
    return QString::fromStdString(
        previews_.measurer().faces().painted_family(family.toStdString()));
}

QString FontController::standIn()
{
    return QString::fromUtf8(canvas::k_default_font_family);
}

QVariantList FontController::faces(const QString& family, int /*revision*/)
{
    // A library's font is checked before Qt loads it.
    const auto preview = [this](const QString& path, const QString& filename, const QString& style) {
        const auto key = path.toStdString();
        auto loaded = previews_loaded_.find(key);
        if (loaded == previews_loaded_.end()) {
            loaded = previews_loaded_
                         .emplace(key, layout::QtFontFaces::load_file(
                                           fonts::FontMatch{key, fonts::FontSource::Library, false}))
                         .first;
        }
        const auto& font = loaded->second.font;
        return QVariantMap{{QStringLiteral("filename"), filename},
                           {QStringLiteral("style"), style},
                           {QStringLiteral("loaded"), loaded->second.loaded},
                           {QStringLiteral("qtFamily"), font.family()},
                           {QStringLiteral("qtStyle"), font.styleName()},
                           {QStringLiteral("weight"), font.weight()},
                           {QStringLiteral("italic"), font.italic()}};
    };

    const auto wanted = util::to_lower(family.toStdString());
    QVariantList out;
    if (auto opened = store()) {
        for (const auto& face : opened->library_faces()) {
            if (util::to_lower(face.family) == wanted) {
                out.push_back(preview(QString::fromStdString(opened->file_of(face).string()),
                                      QString::fromStdString(face.filename),
                                      QString::fromStdString(face.style)));
            }
        }
    }
    return out;
}

QVariantList FontController::uses(const QString& family, int /*revision*/)
{
    auto* db = library_.database();
    if (db == nullptr || family.isEmpty()) {
        return {};
    }
    std::vector<db::FontUse> found;
    try {
        found = db::FontRepository(*db).uses_of(family.toStdString());
    } catch (const std::exception& ex) {
        emit errorOccurred(report_entry_error(__func__, ex));
        return {};
    }
    const auto kind_of = [](db::FontUse::Kind kind) {
        switch (kind) {
        case db::FontUse::Kind::Theme: return QStringLiteral("theme");
        case db::FontUse::Kind::Output: return QStringLiteral("output");
        case db::FontUse::Kind::Profile: return QStringLiteral("profile");
        case db::FontUse::Kind::Song: return QStringLiteral("song");
        case db::FontUse::Kind::Freeform: return QStringLiteral("freeform");
        case db::FontUse::Kind::ServiceItem: return QStringLiteral("service");
        }
        return QString();
    };
    QVariantList out;
    out.reserve(static_cast<qsizetype>(found.size()));
    for (const auto& use : found) {
        out.push_back(QVariantMap{{QStringLiteral("kind"), kind_of(use.kind)},
                                  {QStringLiteral("name"), QString::fromStdString(use.name)},
                                  {QStringLiteral("within"), QString::fromStdString(use.within)},
                                  {QStringLiteral("id"), QString::fromStdString(use.id)},
                                  {QStringLiteral("type"), QString::fromStdString(use.type)}});
    }
    return out;
}

void FontController::releasePreviews()
{
    for (const auto& [path, loaded] : previews_loaded_) {
        if (loaded.qt_font_id >= 0) {
            QFontDatabase::removeApplicationFont(loaded.qt_font_id);
        }
    }
    previews_loaded_.clear();
}

QString FontController::why_not_change() const
{
    if (!store()) {
        return refusal::no_library();
    }
    if (!may_write()) {
        return tr("This library is view only here.");
    }
    if (!may_reload()) {
        return tr("Something is live. Fonts can change once it has stopped.");
    }
    return {};
}

std::optional<FontStore> FontController::store_to_change() const
{
    return why_not_change().isEmpty() ? store() : std::nullopt;
}

void FontController::addFiles(const QStringList& urls)
{
    if (urls.isEmpty() || adding_) {
        return;
    }
    if (const auto why = why_not_change(); !why.isEmpty()) {
        QVariantList results;
        for (const auto& url : urls) {
            results.push_back(QVariantMap{
                {QStringLiteral("file"),
                 QString::fromStdString(local_path_from_qml(url).filename().string())},
                {QStringLiteral("ok"), false},
                {QStringLiteral("text"), why}});
        }
        emit addFinished(results, {});
        return;
    }
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(urls.size()));
    for (const auto& url : urls) {
        paths.push_back(local_path_from_qml(url));
    }
    start_adding(std::move(paths), false, {});
}

void FontController::add_files_keeping(std::vector<std::filesystem::path> paths, std::shared_ptr<void> hold)
{
    // Nothing to add, or nowhere to: something live only makes it wait.
    if (paths.empty() || !store() || !may_write()) {
        return;
    }
    if (adding_) {
        keeping_waiting_.push_back({std::move(paths), std::move(hold)});
        return;
    }
    start_adding(std::move(paths), true, std::move(hold));
}

void FontController::adding_ended()
{
    adding_hold_.reset();
    if (adding_ || keeping_waiting_.empty()) {
        return;
    }
    auto next = std::move(keeping_waiting_.front());
    keeping_waiting_.pop_front();
    if (store() && may_write()) {
        start_adding(std::move(next.paths), true, std::move(next.hold));
    }
}

void FontController::start_adding(std::vector<std::filesystem::path> paths, bool keep_occupied,
                                  std::shared_ptr<void> hold)
{
    adding_ = true;
    adding_keeps_ = keep_occupied;
    adding_hold_ = std::move(hold);
    emit addingChanged();
    workers_.start([this, paths = std::move(paths), media_root = library_.media_root(),
                    generation = generation_] {
        std::vector<AddingFile> files;
        files.reserve(paths.size());
        for (const auto& path : paths) {
            files.push_back(AddingFile{path.filename().string(),
                                       FontStore::check_and_write(path, media_root)});
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, files = std::move(files)]() mutable {
                finish_adding(generation, std::move(files));
            },
            Qt::QueuedConnection);
    });
}

void FontController::finish_adding(std::uint64_t generation, std::vector<AddingFile> files)
{
    if (generation != generation_) {
        // The library these were for has gone.
        QVariantList results;
        for (const auto& file : files) {
            FontStore::discard(file.checked);
            results.push_back(QVariantMap{{QStringLiteral("file"), QString::fromStdString(file.name)},
                                          {QStringLiteral("ok"), false},
                                          {QStringLiteral("text"), tr("The library was closed.")}});
        }
        adding_ = false;
        adding_keeps_ = false;
        emit addingChanged();
        emit addFinished(results, {});
        adding_ended();
        return;
    }
    if (!may_reload()) {
        adding_waiting_ = std::move(files);
        return;
    }
    commit_adding(std::move(files));
}

void FontController::commit_adding(std::vector<AddingFile> files)
{
    QVariantList results;
    QStringList titles;
    std::int64_t bytes = 0;
    // A library can turn view only on this computer while its files are read.
    auto opened = store_to_change();
    const auto why = opened ? QString{} : why_not_change();
    for (const auto& file : files) {
        const auto name = QString::fromStdString(file.name);
        if (!opened) {
            FontStore::discard(file.checked);
            results.push_back(QVariantMap{{QStringLiteral("file"), name},
                                          {QStringLiteral("ok"), false},
                                          {QStringLiteral("text"), why}});
            continue;
        }
        const auto added = opened->add_checked(file.checked, adding_keeps_);
        QString text;
        if (!added.error.empty()) {
            text = QString::fromStdString(added.error);
        } else if (added.faces.empty() && !added.kept.empty()) {
            text = tr("The library's own %1 is kept.").arg(QString::fromStdString(added.kept.front()));
        } else if (added.already == added.faces.size() && file.checked.written.empty()) {
            text = tr("Already in this library.");
        } else {
            // "Lato: Regular, Bold", by family: a collection can hold several.
            std::map<std::string, QStringList> styles;
            for (const auto& face : added.faces) {
                styles[face.family].push_back(QString::fromStdString(face.style));
            }
            QStringList parts;
            for (const auto& [family, list] : styles) {
                parts.push_back(tr("%1 (%2)").arg(QString::fromStdString(family),
                                                  list.join(QStringLiteral(", "))));
            }
            text = tr("Added %1.").arg(parts.join(QStringLiteral(", ")));
            if (added.replaced > 0) {
                text += QLatin1Char(' ')
                        + (added.replaced == 1
                               ? tr("It replaced a style the library had.")
                               : tr("It replaced %1 styles the library had.").arg(added.replaced));
            }
            titles.push_back(name);
            std::error_code ec;
            for (const auto& written : file.checked.written) {
                const auto size = std::filesystem::file_size(written, ec);
                bytes += ec ? 0 : static_cast<std::int64_t>(size);
                files_changed_ = true;
            }
        }
        results.push_back(QVariantMap{{QStringLiteral("file"), name},
                                      {QStringLiteral("ok"), added.error.empty()},
                                      {QStringLiteral("text"), text}});
    }
    adding_ = false;
    adding_keeps_ = false;
    emit addingChanged();
    library_fonts_changed();
    emit addFinished(results, import_note_ ? import_note_(titles, bytes) : QString{});
    adding_ended();
}

QString FontController::removeFaces(const QStringList& filenames)
{
    auto opened = store_to_change();
    if (!opened) {
        return why_not_change();
    }
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(filenames.size()));
    for (const auto& filename : filenames) {
        names.push_back(filename.toStdString());
    }
    auto error = QString::fromStdString(opened->remove_faces(names));
    library_fonts_changed();
    return error;
}

QString FontController::removeFamily(const QString& family)
{
    auto opened = store_to_change();
    if (!opened) {
        return why_not_change();
    }
    auto error = QString::fromStdString(opened->remove_family(family.toStdString()));
    library_fonts_changed();
    return error;
}

}  // namespace seventhslide::app
