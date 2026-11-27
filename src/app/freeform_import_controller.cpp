// Import of Freeform files and service packages (see the header).

#include "freeform_import_controller.hpp"

#include "entry_error.hpp"
#include "font_controller.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "local_file_path.hpp"
#include "media_controller.hpp"
#include "open_library.hpp"
#include "refusals.hpp"

#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/exchange/adapt.hpp>
#include <seventhslide/exchange/bundle.hpp>
#include <seventhslide/exchange/bundle_io.hpp>
#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/geom/canvas_presets.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/library.hpp>
#include <seventhslide/model/live_source.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/model/web_page.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/util/file_copy.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/unique_name.hpp>

#include <QByteArray>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

Q_LOGGING_CATEGORY(lcImport, "seventhslide.import")

namespace fs = std::filesystem;
using nlohmann::json;

[[nodiscard]] QString stopped_text()
{
    return FreeformImportController::tr("The import was stopped.");
}

constexpr std::size_t k_hash_chunk = 1 << 20;
/// Progress reaches the GUI thread at most this often.
constexpr auto k_progress_interval = std::chrono::milliseconds(100);

[[nodiscard]] QString q(std::string_view s)
{
    return QString::fromUtf8(s);
}

[[nodiscard]] db::MediaKind kind_of(canvas::MediaRefKind kind) noexcept
{
    return kind == canvas::MediaRefKind::Video ? db::MediaKind::Video : db::MediaKind::Image;
}

/// The media kind a rundown row's files are, or nullopt for a row with none.
[[nodiscard]] std::optional<db::MediaKind> kind_of(model::ContentKind kind) noexcept
{
    switch (kind) {
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
        return db::MediaKind::Image;
    case model::ContentKind::Video:
        return db::MediaKind::Video;
    case model::ContentKind::Audio:
        return db::MediaKind::Audio;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::string extension_of(std::string_view name)
{
    return util::to_lower(fs::path(std::string(name)).extension().string());
}

/// ``path``'s SHA-256, or nullopt when it cannot be read or ``cancelled`` is set.
[[nodiscard]] std::optional<std::string> sha256_of(const fs::path& path, const std::atomic<bool>& cancelled)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    crypto::Sha256 sha;
    std::string chunk(k_hash_chunk, '\0');
    while (in) {
        if (cancelled.load()) {
            return std::nullopt;
        }
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (in.gcount() > 0) {
            sha.update(std::string_view(chunk.data(), static_cast<std::size_t>(in.gcount())));
        }
    }
    if (in.bad()) {
        return std::nullopt;
    }
    return sha.finish_hex();
}

/// A staging name in ``dir``, which the media folders' sweep clears after a crash.
[[nodiscard]] fs::path staging_in(const fs::path& dir)
{
    return dir
           / (std::string(library::k_staging_prefix) + QUuid::createUuid().toString(QUuid::Id128).toStdString()
              + std::string(library::k_staging_suffix));
}

void remove_quietly(const fs::path& path)
{
    if (!path.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
    }
}

[[nodiscard]] std::vector<exchange::LibraryOutput> library_outputs(const session::Session& session)
{
    std::vector<exchange::LibraryOutput> out;
    for (const auto& oc : session.outputs()) {
        out.push_back({oc.id, oc.name, model::output_canvas(oc)});
    }
    return out;
}

[[nodiscard]] QString size_text(geom::CanvasSize size)
{
    return QStringLiteral("%1×%2").arg(size.width).arg(size.height);
}

/// "Easter / Sunday": where the file's folder ``key`` is.
[[nodiscard]] QString folder_path(const exchange::Manifest& m, const std::string& key)
{
    QStringList parts;
    std::string at = key;
    for (std::size_t steps = 0; !at.empty() && steps <= m.folders.size(); ++steps) {
        const auto it = std::ranges::find(m.folders, at, &exchange::BundleFolder::key);
        if (it == m.folders.end()) {
            break;
        }
        parts.prepend(q(it->name));
        at = it->parent;
    }
    return parts.join(QStringLiteral(" / "));
}

/// ``yyyy-MM-ddTHH:mm:ss``, as a service's date is kept.
[[nodiscard]] bool looks_like_service_date(std::string_view s) noexcept
{
    constexpr std::string_view shape = "dddd-dd-ddTdd:dd:dd";
    if (s.size() != shape.size()) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (shape[i] == 'd' ? (s[i] < '0' || s[i] > '9') : s[i] != shape[i]) {
            return false;
        }
    }
    return true;
}

/// A loose file's title: its name without the row's number before it.
[[nodiscard]] std::string loose_title(std::string_view name)
{
    auto title = fs::path(std::string(name)).stem().string();
    const auto space = title.find(' ');
    if (space != std::string::npos && space > 0
        && std::all_of(title.begin(), title.begin() + static_cast<std::ptrdiff_t>(space),
                       [](char c) { return c >= '0' && c <= '9'; })) {
        title = title.substr(space + 1);
    }
    return title;
}

/// The preview of item ``n`` of ``file`` as a ``data:`` URL, or empty.
[[nodiscard]] QString preview_url(const exchange::OpenedFile& file, std::size_t n)
{
    const auto png = exchange::read_preview(file, n);
    if (png.empty()) {
        return {};
    }
    return QStringLiteral("data:image/png;base64,")
           + QString::fromLatin1(QByteArray(png.data(), static_cast<qsizetype>(png.size())).toBase64());
}

}  // namespace

/// One file the import needs: from the bundle (a picture or video an item
/// names) or the package (a row's own).
struct FreeformImportController::Need {
    std::string entry;
    bool loose = false;
    db::MediaKind kind = db::MediaKind::Image;
    std::string title;
    std::uint64_t size = 0;
};

/// What one needed file came to on the worker.
struct FreeformImportController::Got {
    fs::path staged;
    /// A library file with the same bytes, used in place of ``staged`` if it
    /// is still the library's when the import commits.
    std::string reuse;
    /// An earlier need with the same bytes, whose file this one is.
    std::optional<std::size_t> same_as;
};

/// An import's plan, made on the GUI thread, and what the worker made of it.
struct FreeformImportController::Extracted {
    std::shared_ptr<exchange::OpenedFile> file;
    std::string library;
    fs::path media_root;

    struct Item {
        std::size_t index = 0;
        freeform::Document document;
    };
    /// The items chosen that land, their files still named by entry.
    std::vector<Item> items;
    std::map<std::string, std::size_t, std::less<>> item_of_entry;
    bool add_items = true;
    bool add_service = false;
    std::string folder_name;

    std::vector<Need> needs;
    std::vector<std::size_t> fonts;
    /// The library's files of each kind, to find one with the same bytes.
    std::map<db::MediaKind, std::vector<std::string>> library_files;
    /// Where the fonts are unpacked, kept until the font controller is done
    /// with them, whatever becomes of the import.
    std::shared_ptr<QTemporaryDir> font_dir;
    QStringList notes;

    // The worker's.
    std::vector<Got> got;
    std::vector<fs::path> font_files;
    QString error;
    bool stopped = false;

    void discard_staged()
    {
        for (auto& g : got) {
            remove_quietly(g.staged);
            g.staged.clear();
        }
    }
};

namespace {

using Extracted = FreeformImportController::Extracted;
using Need = FreeformImportController::Need;
using Got = FreeformImportController::Got;

/// Worker thread: each needed file out of the file, under a staging name in
/// its kind's media folder, matched to one the library or this import
/// already has with the same bytes; and the fonts into their folder.
void extract_files(Extracted& plan, const std::atomic<bool>& cancelled, const std::function<void(double)>& report)
{
    const auto& file = *plan.file;
    std::uint64_t total = 0;
    for (const auto& need : plan.needs) {
        total += need.size;
    }
    for (const auto i : plan.fonts) {
        total += file.manifest.fonts[i].size;
    }
    std::error_code ec;
    if (const auto space = fs::space(plan.media_root, ec); !ec && space.available < total + (64ull << 20)) {
        plan.error = FreeformImportController::tr("There is not room enough on the disk for the files it brings.");
        return;
    }
    std::uint64_t done = 0;
    const auto progress = [&](std::uintmax_t copied) {
        if (cancelled.load()) {
            return false;
        }
        report(total == 0 ? 1.0 : static_cast<double>(done + copied) / static_cast<double>(total));
        return true;
    };

    // The library's files of each size, read once, and the hashes known.
    std::map<db::MediaKind, std::multimap<std::uint64_t, std::string>> by_size;
    std::map<std::string, std::string> library_sha;
    // This import's own files by size, hashed only when another of a size
    // comes, so the same bytes come in once and a file alone is read once.
    std::multimap<std::uint64_t, std::size_t> taken;
    std::map<std::size_t, std::string> own_sha;
    const auto sha_of_need = [&](std::size_t n) -> const std::string& {
        auto& known = own_sha[n];
        if (known.empty()) {
            known = sha256_of(plan.got[n].staged, cancelled).value_or("-");
        }
        return known;
    };
    for (std::size_t n = 0; n < plan.needs.size(); ++n) {
        const auto& need = plan.needs[n];
        // A row's own file goes to the library; an item's picture or video is
        // the item's own (freeform/own_media.hpp), staged where those are and
        // never matched to a library file.
        const auto dir = need.loose ? plan.media_root / std::string(db::media_subdir(need.kind))
                                    : plan.media_root / std::string(model::k_freeform_media_subdir);
        fs::create_directories(dir, ec);
        if (need.loose && !by_size.contains(need.kind)) {
            auto& sizes = by_size[need.kind];
            for (const auto& name : plan.library_files[need.kind]) {
                std::error_code size_ec;
                const auto size = fs::file_size(dir / name, size_ec);
                if (!size_ec) {
                    sizes.emplace(static_cast<std::uint64_t>(size), name);
                }
            }
        }
        // A row's own file is in the package; a file without one has no such
        // row, and reads as damaged.
        const auto* zip = need.loose ? (file.package ? &*file.package : nullptr) : &file.bundle;
        const auto* entry = zip != nullptr ? zip->find(need.entry) : nullptr;
        if (entry == nullptr) {
            plan.error = FreeformImportController::tr("The file is damaged: some of its files are missing.");
            return;
        }
        Got got;
        got.staged = staging_in(dir);
        std::string why;
        const auto copied = zip->extract(*entry, got.staged, progress, why);
        if (copied == util::CopyOutcome::Stopped) {
            plan.stopped = true;
            return;
        }
        if (copied == util::CopyOutcome::Failed) {
            plan.error = q(why);
            return;
        }
        done += entry->size;
        plan.got.push_back(std::move(got));

        static const std::multimap<std::uint64_t, std::string> k_none;
        const auto& library = need.loose ? by_size[need.kind] : k_none;
        const auto [lib_lo, lib_hi] = library.equal_range(entry->size);
        const auto [own_lo, own_hi] = taken.equal_range(entry->size);
        if (lib_lo == lib_hi && own_lo == own_hi) {
            taken.emplace(entry->size, n);
            continue;
        }
        // A map's entries stay where they are as others are added.
        const std::string& sha = sha_of_need(n);
        if (cancelled.load()) {
            plan.stopped = true;
            return;
        }
        auto& here = plan.got[n];
        for (auto it = own_lo; it != own_hi && !here.same_as; ++it) {
            const auto& other = plan.needs[it->second];
            if (other.kind == need.kind && other.loose == need.loose && sha_of_need(it->second) == sha) {
                here.same_as = it->second;
            }
        }
        for (auto it = lib_lo; !here.same_as && it != lib_hi; ++it) {
            auto& known = library_sha[it->second];
            if (known.empty()) {
                known = sha256_of(dir / it->second, cancelled).value_or("-");
            }
            if (known == sha) {
                here.reuse = it->second;  // the staged copy stays until the commit
                break;
            }
        }
        if (here.same_as) {
            remove_quietly(here.staged);
            here.staged.clear();
        } else {
            taken.emplace(entry->size, n);
        }
    }
    for (const auto i : plan.fonts) {
        const auto& font = file.manifest.fonts[i];
        const auto* entry = file.bundle.find(font.entry);
        const auto to = local_path_from_qml(plan.font_dir->path()) / font.entry.substr(exchange::k_fonts_dir.size());
        std::string why;
        const auto copied =
            entry != nullptr ? file.bundle.extract(*entry, to, progress, why) : util::CopyOutcome::Failed;
        if (copied == util::CopyOutcome::Stopped) {
            plan.stopped = true;
            return;
        }
        if (copied == util::CopyOutcome::Failed) {
            plan.notes.push_back(FreeformImportController::tr("The font “%1” could not be read.").arg(q(font.family)));
            continue;
        }
        done += font.size;
        plan.font_files.push_back(to);
    }
}

/// The GUI thread's half of an import: in one transaction, the folders, the
/// files, the items and the service; and what to undo if it does not commit.
class Committer {
public:
    Committer(Extracted& plan, db::Database& db, db::FreeformRepository& freeform, db::MediaRepository& media,
              db::ServiceRepository& services, MediaController& media_controller,
              const std::vector<model::StackDef>& stacks)
        : plan_(plan)
        , file_(*plan.file)
        , db_(db)
        , freeform_(freeform)
        , media_(media)
        , services_(services)
        , media_controller_(media_controller)
        , stacks_(stacks)
    {
    }

    /// Throws on a database error; ``undo`` then takes back what was moved.
    void run()
    {
        db::Transaction tx(db_);
        take_files();
        file_service_media();
        repoint_items();
        if (plan_.add_items) {
            make_items();
        }
        if (plan_.add_service) {
            make_service();
        }
        tx.commit();
    }

    /// After a throw: the files moved into the library's folders go again.
    void undo()
    {
        for (const auto& path : moved_) {
            remove_quietly(path);
        }
        for (const auto& owner : owners_) {
            std::error_code ec;
            fs::remove(freeform::own_media_dir(plan_.media_root, owner), ec);  // when empty
        }
    }

    [[nodiscard]] const std::vector<MediaController::StagedMedia>& adopted() const noexcept { return adopted_; }
    [[nodiscard]] const std::vector<std::string>& adopted_names() const noexcept { return adopted_names_; }
    [[nodiscard]] const std::set<db::MediaKind>& folders_made() const noexcept { return folders_made_; }
    [[nodiscard]] qint64 service_id() const noexcept { return service_id_; }
    [[nodiscard]] int items_made() const noexcept { return items_made_; }
    [[nodiscard]] int files_new() const noexcept
    {
        return static_cast<int>(adopted_names_.size()) + own_files_;
    }

private:
    std::int64_t service_folder(db::MediaKind kind)
    {
        if (const auto it = service_folders_.find(kind); it != service_folders_.end()) {
            return it->second;
        }
        auto tree = media_.folders(kind);
        const std::string rundown_name = file_.rundown ? file_.rundown->name : std::string();
        const std::string name = rundown_name.empty() ? std::string("Imported") : rundown_name;
        const auto taken = util::taken_names(tree.children(std::nullopt), &model::Folder::name);
        const auto made = tree.create(util::unique_name(name, name, taken), std::nullopt);
        service_folders_.emplace(kind, made);
        folders_made_.insert(kind);
        return made;
    }

    /// The files: a library file with the same bytes where it is still the
    /// library's, else the copy taken out of the file. Filed in no folder
    /// yet: ``file_service_media`` does, in each row's order.
    void take_files()
    {
        std::vector<std::size_t> adopted_need;
        for (std::size_t n = 0; n < plan_.got.size(); ++n) {
            const auto& got = plan_.got[n];
            const auto& need = plan_.needs[n];
            if (!need.loose) {
                // An item's own: placed in each item's folder (``own_files``).
                need_of_entry_.emplace(need.entry, n);
                continue;
            }
            if (got.same_as) {
                continue;
            }
            if (!got.reuse.empty() && media_.get_file(need.kind, got.reuse)) {
                name_of_.emplace(need.entry, got.reuse);
                continue;
            }
            MediaController::StagedMedia staged;
            staged.kind = need.kind;
            staged.staged = got.staged;
            staged.extension = extension_of(need.entry);
            staged.title = need.title;
            adopted_.push_back(std::move(staged));
            adopted_need.push_back(n);
        }
        adopted_names_ = media_controller_.adopt_staged_batch(adopted_, moved_);
        for (std::size_t a = 0; a < adopted_names_.size(); ++a) {
            const auto n = adopted_need[a];
            name_of_.emplace(plan_.needs[n].entry, adopted_names_[a]);
            plan_.got[n].staged.clear();  // the library's now
        }
        // The same bytes twice in the file: one library file.
        for (std::size_t n = 0; n < plan_.got.size(); ++n) {
            if (!plan_.needs[n].loose) {
                continue;
            }
            if (const auto same = plan_.got[n].same_as) {
                if (const auto it = name_of_.find(plan_.needs[*same].entry); it != name_of_.end()) {
                    const auto filename = it->second;
                    name_of_.emplace(plan_.needs[n].entry, filename);
                }
            }
        }
    }

    /// A service's own files, filed in a folder of each kind named for it,
    /// an image-folder row's in a folder of its own beneath, in the row's
    /// order; each file once a folder, and only files that came in.
    void file_service_media()
    {
        if (!plan_.add_service || !file_.rundown) {
            return;
        }
        std::set<std::pair<std::int64_t, std::string>> filed;
        const auto file_into = [&](db::MediaKind kind, std::int64_t folder, const std::string& filename) {
            if (filed.emplace(folder, filename).second) {
                (void)media_.add_folder_item(kind, folder, filename);
            }
        };
        const auto& rows = file_.rundown->rows;
        for (std::size_t at = 0; at < rows.size(); ++at) {
            const auto& row = rows[at];
            const auto kind_here = kind_of(row.kind);
            if (!kind_here) {
                continue;
            }
            const db::MediaKind kind = *kind_here;
            std::vector<std::string> names;
            for (const auto& file : row.files) {
                if (const auto it = name_of_.find(file); it != name_of_.end()) {
                    names.push_back(it->second);
                }
            }
            if (names.empty()) {
                continue;
            }
            if (row.kind == model::ContentKind::ImageFolder) {
                const auto parent = service_folder(kind);
                auto tree = media_.folders(db::MediaKind::Image);
                const auto wanted = row.data.value("folder_name", row.title);
                const auto name = wanted.empty() ? (row.title.empty() ? std::string("Pictures") : row.title) : wanted;
                const auto taken = util::taken_names(tree.children(parent), &model::Folder::name);
                const auto sub = tree.create(util::unique_name(name, name, taken), parent);
                row_folder_.emplace(at, sub);
                for (const auto& filename : names) {
                    file_into(kind, sub, filename);
                }
            } else {
                file_into(kind, service_folder(kind), names.front());
            }
        }
    }

    /// Each item's files its own, in a folder of its own named by the uid
    /// it is stored under: an entry that did not come in is emptied.
    void repoint_items()
    {
        owners_.clear();
        for (auto& item : plan_.items) {
            const auto owner = freeform::new_own_media_owner();
            std::map<std::string, std::string, std::less<>> urls;
            for (const auto& entry : exchange::entries_named(item.document)) {
                if (auto url = own_file(entry, owner); !url.empty()) {
                    urls.emplace(entry, std::move(url));
                }
            }
            exchange::point_at_entries(item.document, urls);
            freeform::normalize(item.document);
            owners_.push_back(owner);
        }
    }

    /// The file ``entry`` names, in ``owner``'s folder: the staged copy moved
    /// there the first time, a link to (or copy of) it for each item after,
    /// under the one name. Its URL, or empty when it did not come in.
    [[nodiscard]] std::string own_file(const std::string& entry, const std::string& owner)
    {
        const auto found = need_of_entry_.find(entry);
        if (found == need_of_entry_.end()) {
            return {};
        }
        auto n = found->second;
        if (const auto same = plan_.got[n].same_as) {
            n = *same;
        }
        const auto& need = plan_.needs[n];
        auto& got = plan_.got[n];
        const auto dir = freeform::own_media_dir(plan_.media_root, owner);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (const auto placed = own_placed_.find(n); placed != own_placed_.end()) {
            const auto name = placed->second.filename().string();
            const auto to = dir / name;
            if (!fs::exists(to, ec)) {
                fs::create_hard_link(placed->second, to, ec);
                if (ec && util::copy_file_chunked(placed->second, to, [](std::uintmax_t) { return true; })
                              != util::CopyOutcome::Copied) {
                    throw std::runtime_error("Could not copy a picture into an imported item");
                }
                moved_.push_back(to);
            }
            return freeform::own_media_url({owner, name});
        }
        if (got.staged.empty()) {
            return {};
        }
        const auto name = freeform::new_own_media_name(need.title, extension_of(need.entry));
        const auto to = dir / name;
        fs::rename(got.staged, to, ec);
        if (ec) {
            throw std::runtime_error("Could not move a picture into an imported item");
        }
        got.staged.clear();  // the item's now
        moved_.push_back(to);
        own_placed_.emplace(n, to);
        ++own_files_;
        return freeform::own_media_url({owner, name});
    }

    /// The items, in their folders: a new folder named as asked, the file's
    /// folders beneath it.
    void make_items()
    {
        auto tree = freeform_.folders();
        std::optional<std::int64_t> top;
        const auto unique_at = [&tree](std::optional<std::int64_t> parent, const std::string& name) {
            const auto taken = util::taken_names(tree.children(parent), &model::Folder::name);
            return util::unique_name(name, name, taken);
        };
        if (!plan_.folder_name.empty() && !plan_.items.empty()) {
            top = tree.create(unique_at(std::nullopt, plan_.folder_name), std::nullopt);
        }
        std::map<std::string, std::int64_t, std::less<>> folder_of;
        const auto& m = file_.manifest;
        const auto folder_for = [&](const auto& self, const std::string& key) -> std::optional<std::int64_t> {
            if (key.empty()) {
                return top;
            }
            if (const auto it = folder_of.find(key); it != folder_of.end()) {
                return it->second;
            }
            const auto f = std::ranges::find(m.folders, key, &exchange::BundleFolder::key);
            if (f == m.folders.end()) {
                return top;
            }
            const auto parent = self(self, f->parent);
            const auto made = tree.create(unique_at(parent, f->name), parent);
            folder_of.emplace(key, made);
            return made;
        };
        for (std::size_t i = 0; i < plan_.items.size(); ++i) {
            const auto& item = plan_.items[i];
            const auto& listed = m.items[item.index];
            model::FreeformItem row;
            row.name = listed.name.empty() ? item.document.name : listed.name;
            row.folder_id = folder_for(folder_for, listed.folder);
            row.tags = listed.tags;
            row.document = item.document.to_json();
            // Under the uid its files' folder is named by.
            (void)freeform_.create_item(row, owners_[i]);
            ++items_made_;
        }
    }

    [[nodiscard]] std::string stack_here(const exchange::RundownRow& row) const
    {
        if (row.stack_id.empty()) {
            return {};
        }
        for (const auto& d : stacks_) {
            if (d.id == row.stack_id) {
                return d.id;
            }
        }
        for (const auto& d : stacks_) {
            if (!row.stack_name.empty() && util::iequals(d.name, row.stack_name)) {
                return d.id;
            }
        }
        return {};
    }

    [[nodiscard]] std::string library_name(const std::vector<std::string>& files) const
    {
        if (files.empty()) {
            return {};
        }
        const auto it = name_of_.find(files.front());
        return it != name_of_.end() ? it->second : std::string();
    }

    /// Row ``row``'s type and data as this build stores a row of its kind,
    /// through each kind's own codec; nullopt for a row not made (its item
    /// not imported).
    [[nodiscard]] std::optional<std::pair<std::string, json>> row_data(std::size_t at, const exchange::RundownRow& row)
    {
        std::string type(model::to_string(row.kind));
        json data;
        switch (row.kind) {
        case model::ContentKind::Song:
        case model::ContentKind::Bible:
        case model::ContentKind::Freeform: {
            const auto it = plan_.item_of_entry.find(row.item);
            if (it == plan_.item_of_entry.end()) {
                return std::nullopt;
            }
            model::FreeformItem snapshot;
            snapshot.name = row.title;
            snapshot.document = plan_.items[it->second].document.to_json();
            data = model::freeform_item_data(snapshot);
            type = std::string(model::k_item_type_freeform);
            break;
        }
        case model::ContentKind::Image:
            data = model::image_item_data(library_name(row.files), row.title);
            break;
        case model::ContentKind::Video:
        case model::ContentKind::Audio:
            // ``read_rundown`` kept these only as booleans.
            data = model::playback_item_data(library_name(row.files), row.title, row.data.value("loop", false));
            data["autoplay"] = row.data.value("autoplay", true);
            break;
        case model::ContentKind::ImageFolder: {
            // The folder's own codec, reading the folder as this transaction made it.
            const auto folder = row_folder_.find(at);
            auto made = folder != row_folder_.end() ? media_controller_.image_folder_data(folder->second)
                                                    : std::nullopt;
            data = made ? std::move(*made)
                        : json{{"folder_id", -1},
                               {"folder_name", row.data.value("folder_name", row.title)},
                               {"images", json::array()}};
            break;
        }
        case model::ContentKind::Web: {
            auto page = model::web_page_from_item_data(row.data);
            page.id.reset();
            if (!pages_) {
                pages_ = db::WebPageRepository(db_).all_items();
            }
            for (const auto& p : *pages_) {
                if (p.url == page.url) {
                    page.id = p.id;
                    break;
                }
            }
            data = model::web_item_data(page);
            break;
        }
        case model::ContentKind::Live:
            data = model::live_item_data(model::live_source_from_item_data(row.data));
            break;
        case model::ContentKind::Divider:
            data = {{"title", row.title}};
            break;
        case model::ContentKind::Empty:
            return std::nullopt;
        }
        if (const auto stack = stack_here(row); !stack.empty()) {
            data["stack_id"] = stack;
        }
        return std::pair{std::move(type), std::move(data)};
    }

    void make_service()
    {
        if (!file_.rundown) {
            return;
        }
        const auto& r = *file_.rundown;
        const auto taken = util::taken_names(services_.list_services(), &model::Service::name);
        const auto name = r.name.empty() ? std::string("Imported service") : r.name;
        service_id_ = services_.create_service(util::unique_name(name, name, taken));
        if (!r.tags.empty()) {
            services_.set_service_tags(service_id_, r.tags);
        }
        // A date as services keep them; any other is left off.
        if (looks_like_service_date(r.service_at)) {
            services_.set_service_at(service_id_, r.service_at);
        }
        if (r.audio_fades.is_object() && !r.audio_fades.empty()) {
            services_.set_service_audio_fades(service_id_, r.audio_fades);
        }
        std::map<std::size_t, std::int64_t> row_id;  // place in the rundown -> the row made
        std::vector<json> data_of(r.rows.size());
        for (std::size_t at = 0; at < r.rows.size(); ++at) {
            auto made = row_data(at, r.rows[at]);
            if (!made) {
                continue;
            }
            row_id.emplace(at, services_.add_item(service_id_, made->first, made->second));
            data_of[at] = std::move(made->second);
        }
        // Rows that go live together, among those made.
        for (std::size_t at = 0; at < r.rows.size(); ++at) {
            const auto made = row_id.find(at);
            if (made == row_id.end() || r.rows[at].also_play.empty()) {
                continue;
            }
            std::vector<std::int64_t> ids;
            for (const auto other : r.rows[at].also_play) {
                if (const auto it = row_id.find(other); it != row_id.end()) {
                    ids.push_back(it->second);
                }
            }
            if (!ids.empty()) {
                model::ServiceItem item;
                item.data = data_of[at];
                model::set_service_item_also_play(item, std::move(ids));
                services_.replace_item_data(made->second, item.data);
            }
        }
    }

    Extracted& plan_;
    const exchange::OpenedFile& file_;
    db::Database& db_;
    db::FreeformRepository& freeform_;
    db::MediaRepository& media_;
    db::ServiceRepository& services_;
    MediaController& media_controller_;
    const std::vector<model::StackDef>& stacks_;

    std::map<std::string, std::string, std::less<>> name_of_;  // entry -> library name
    /// An item's file by entry -> its need.
    std::map<std::string, std::size_t, std::less<>> need_of_entry_;
    /// Each need of an item's placed: where it was moved first.
    std::map<std::size_t, fs::path> own_placed_;
    /// Each item's owner of its files, by place in ``plan_.items``.
    std::vector<std::string> owners_;
    int own_files_ = 0;
    /// Each image-folder row's own folder, by place in the rundown.
    std::map<std::size_t, std::int64_t> row_folder_;
    std::map<db::MediaKind, std::int64_t> service_folders_;
    std::set<db::MediaKind> folders_made_;
    std::vector<MediaController::StagedMedia> adopted_;
    std::vector<std::string> adopted_names_;
    std::vector<fs::path> moved_;
    std::optional<std::vector<model::WebPage>> pages_;
    qint64 service_id_ = -1;
    int items_made_ = 0;
};

}  // namespace

FreeformImportController::FreeformImportController(session::Session& session,
                                                   OpenLibrary& open_library,
                                                   FreeformLibraryController& freeform,
                                                   MediaController& media,
                                                   FontController& fonts,
                                                   LibraryController& library,
                                                   QObject* parent)
    : QObject(parent)
    , session_(session)
    , open_library_(open_library)
    , freeform_(freeform)
    , media_(media)
    , fonts_(fonts)
    , library_(library)
{
    pool_.setMaxThreadCount(1);
}

FreeformImportController::~FreeformImportController()
{
    cancelled_->store(true);
    pool_.waitForDone();
}

void FreeformImportController::set_busy(bool busy)
{
    if (busy_ != busy) {
        busy_ = busy;
        emit busyChanged();
    }
    if (!busy) {
        set_progress(0.0);
    }
}

void FreeformImportController::set_progress(double progress)
{
    if (progress_ != progress) {
        progress_ = progress;
        emit progressChanged();
    }
}

std::string FreeformImportController::library_key() const
{
    auto* db = open_library_.database();
    if (db == nullptr) {
        return {};
    }
    try {
        return db::library_uid(*db) + "|" + open_library_.media_root().string();
    } catch (const std::exception&) {
        return {};
    }
}

QString FreeformImportController::freeformFilePatterns() const
{
    return QStringLiteral("*%1 *%2").arg(QString::fromUtf8(exchange::k_single_extension),
                                         QString::fromUtf8(exchange::k_bundle_extension));
}

QString FreeformImportController::packageFilePatterns() const
{
    return QStringLiteral("*") + QString::fromUtf8(exchange::k_package_extension);
}

QString FreeformImportController::why_not_here() const
{
    if (open_library_.database() == nullptr || open_library_.media_root().empty() || freeform_.repo() == nullptr) {
        return refusal::no_library();
    }
    if (open_library_.read_only()) {
        return tr("This computer is a viewer of this library, so nothing can be imported into it.");
    }
    return {};
}

qint64 FreeformImportController::refuse_read(const QString& why)
{
    const qint64 id = ++last_request_id_;
    QMetaObject::invokeMethod(
        this, [this, id, why] { emit fileRead(id, {{QStringLiteral("error"), why}}); }, Qt::QueuedConnection);
    return id;
}

qint64 FreeformImportController::refuse_import(const QString& why)
{
    const qint64 id = ++last_request_id_;
    QMetaObject::invokeMethod(
        this,
        [this, id, why] {
            emit importFinished(id, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), why}});
        },
        Qt::QueuedConnection);
    return id;
}

void FreeformImportController::discardRead()
{
    read_.reset();
    read_name_.clear();
    read_library_.clear();
    read_previews_.clear();
}

void FreeformImportController::cancel()
{
    cancelled_->store(true);
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
}

void FreeformImportController::library_closing()
{
    cancel();
    discardRead();
}

qint64 FreeformImportController::readFile(const QString& url)
{
    if (busy_) {
        return refuse_read(tr("An import is already under way."));
    }
    if (const auto why = why_not_here(); !why.isEmpty()) {
        return refuse_read(why);
    }
    const auto path = local_path_from_qml(url);
    if (path.empty()) {
        return refuse_read(tr("Choose a file to import."));
    }
    discardRead();
    const qint64 id = ++last_request_id_;
    set_busy(true);
    pool_.start([this, id, path, key = library_key(), cancelled = cancelled_] {
        std::string refusal;
        std::shared_ptr<exchange::OpenedFile> file;
        QStringList previews;
        try {
            if (auto opened = exchange::open_freeform_file(path, refusal)) {
                file = std::make_shared<exchange::OpenedFile>(std::move(*opened));
                for (std::size_t i = 0; i < file->manifest.items.size() && !cancelled->load(); ++i) {
                    previews.push_back(preview_url(*file, i));
                }
            }
        } catch (const std::exception& ex) {
            qCWarning(lcImport) << "reading" << path.string().c_str() << ex.what();
            file.reset();
            refusal = FreeformImportController::tr("The file could not be read.").toStdString();
        }
        QMetaObject::invokeMethod(
            this,
            [this, id, path, key, cancelled, file = std::move(file), refusal = std::move(refusal),
             previews = std::move(previews)] {
                set_busy(false);
                if (cancelled->load()) {
                    emit fileRead(id, {{QStringLiteral("error"), stopped_text()}});
                    return;
                }
                if (key != library_key()) {
                    emit fileRead(id, {{QStringLiteral("error"), tr("The library changed while the file was read.")}});
                    return;
                }
                if (!file) {
                    emit fileRead(id, {{QStringLiteral("error"), q(refusal)}});
                    return;
                }
                read_ = file;
                read_name_ = path.stem().string();
                read_library_ = key;
                read_previews_ = previews;
                emit fileRead(id, summary());
            },
            Qt::QueuedConnection);
    });
    return id;
}

/// ``{fileName, defaultFolderName, kind, items: [{index, name, folder, preview,
/// refusal}], outputs: [{index, id, name, size, shape, choices: [{id, name,
/// label}], suggested}], pictures, videos, bytes, fonts, notIncluded,
/// service: {name, rows, notes}}``; ``service`` only for a rundown.
QVariantMap FreeformImportController::summary() const
{
    QVariantMap out;
    if (!read_) {
        out.insert(QStringLiteral("error"), tr("Nothing has been read."));
        return out;
    }
    const auto& m = read_->manifest;
    out.insert(QStringLiteral("fileName"), q(read_name_));
    out.insert(QStringLiteral("kind"), read_->package ? QStringLiteral("package")
                                       : m.items.size() == 1 ? QStringLiteral("item")
                                                             : QStringLiteral("bundle"));
    out.insert(QStringLiteral("defaultFolderName"),
               m.items.size() == 1 && !read_->rundown ? QString() : q(read_name_));

    QVariantList items;
    for (std::size_t i = 0; i < m.items.size(); ++i) {
        const auto at = static_cast<qsizetype>(i);
        items.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<int>(i)},
            {QStringLiteral("name"), q(m.items[i].name)},
            {QStringLiteral("folder"), folder_path(m, m.items[i].folder)},
            {QStringLiteral("preview"), at < read_previews_.size() ? read_previews_[at] : QString()},
            {QStringLiteral("refusal"), q(read_->items[i].refusal)},
        });
    }
    out.insert(QStringLiteral("items"), items);

    const auto outputs = library_outputs(session_);
    const auto suggested = exchange::suggest_mapping(m.outputs, outputs);
    QVariantList rows;
    for (std::size_t i = 0; i < m.outputs.size(); ++i) {
        const auto& o = m.outputs[i];
        QVariantList choices;
        for (const auto at : exchange::outputs_that_fit(o, outputs)) {
            const auto& lib = outputs[at];
            choices.push_back(QVariantMap{
                {QStringLiteral("id"), q(lib.id)},
                {QStringLiteral("name"), q(lib.name)},
                {QStringLiteral("label"), tr("%1 · %2").arg(q(lib.name), size_text(lib.canvas))},
            });
        }
        rows.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<int>(i)},
            {QStringLiteral("id"), q(o.id)},
            {QStringLiteral("name"), q(o.name)},
            {QStringLiteral("size"), size_text(o.canvas)},
            {QStringLiteral("shape"), q(geom::canvas_aspect_label(o.canvas))},
            {QStringLiteral("choices"), choices},
            {QStringLiteral("suggested"), q(suggested[i])},
        });
    }
    out.insert(QStringLiteral("outputs"), rows);

    int pictures = 0;
    int videos = 0;
    qint64 bytes = 0;
    for (const auto& f : m.media) {
        (f.kind == canvas::MediaRefKind::Video ? videos : pictures) += 1;
        bytes += static_cast<qint64>(f.size);
    }
    out.insert(QStringLiteral("pictures"), pictures);
    out.insert(QStringLiteral("videos"), videos);
    out.insert(QStringLiteral("fonts"), static_cast<int>(m.fonts.size()));
    QStringList missing;
    if (m.media_not_included.size() > 3) {
        missing.push_back(tr("%n picture(s) or video(s) were not in the library it came from.", "",
                             static_cast<int>(m.media_not_included.size())));
    } else {
        for (const auto& t : m.media_not_included) {
            missing.push_back(t.empty() ? tr("A picture or video was not in the library it came from.")
                                        : tr("“%1” was not in the library it came from.").arg(q(t)));
        }
    }
    for (const auto& f : m.fonts_not_included) {
        missing.push_back(tr("The font “%1” was not in the library it came from.").arg(q(f)));
    }
    out.insert(QStringLiteral("notIncluded"), missing);

    if (read_->rundown) {
        const auto& r = *read_->rundown;
        QStringList notes;
        std::set<std::string> said;
        const auto& defs = session_.stack_defs();
        std::uint64_t loose_bytes = 0;
        int without_files = 0;
        for (const auto& row : r.rows) {
            if (!row.stack_id.empty() && said.insert("stack:" + row.stack_id).second) {
                const bool by_id = std::ranges::any_of(defs, [&](const auto& d) { return d.id == row.stack_id; });
                const bool by_name = std::ranges::any_of(defs, [&](const auto& d) {
                    return !row.stack_name.empty() && util::iequals(d.name, row.stack_name);
                });
                if (!by_id && !by_name) {
                    notes.push_back(tr("The stack “%1” is not in this library: its rows play on their own stack.")
                                        .arg(q(row.stack_name.empty() ? row.stack_id : row.stack_name)));
                }
            }
            if (row.kind == model::ContentKind::Live) {
                const auto source = model::live_source_from_item_data(row.data);
                if (!source.empty() && source.kind != model::k_live_kind_stream) {
                    notes.push_back(tr("“%1” is a source of the computer it came from: it plays here "
                                       "if this computer can see it.")
                                        .arg(q(source.name)));
                }
            }
            bool all_here = true;
            for (const auto& name : row.files) {
                const auto* e = read_->package ? read_->package->find(name) : nullptr;
                if (e == nullptr) {
                    all_here = false;
                } else if (said.insert("size:" + name).second) {
                    loose_bytes += e->size;
                }
            }
            if (!all_here || (kind_of(row.kind) && row.files.empty())) {
                ++without_files;
            }
        }
        if (without_files > 0) {
            notes.push_back(tr("%n row(s) come in without their files: they are not with this file.", "",
                               without_files));
        }
        out.insert(QStringLiteral("bytes"), bytes + static_cast<qint64>(loose_bytes));
        out.insert(QStringLiteral("service"), QVariantMap{
                                                  {QStringLiteral("name"), q(r.name)},
                                                  {QStringLiteral("rows"), static_cast<int>(r.rows.size())},
                                                  {QStringLiteral("notes"), notes},
                                              });
    } else {
        out.insert(QStringLiteral("bytes"), bytes);
    }
    return out;
}

qint64 FreeformImportController::importRead(const QVariantMap& choices)
try {
    if (busy_) {
        return refuse_import(tr("An import is already under way."));
    }
    if (!read_) {
        return refuse_import(tr("Choose a file to import first."));
    }
    if (const auto why = why_not_here(); !why.isEmpty()) {
        return refuse_import(why);
    }
    if (read_library_ != library_key()) {
        discardRead();
        return refuse_import(tr("The library changed after the file was read. Choose it again."));
    }
    const auto& m = read_->manifest;
    const auto outputs = library_outputs(session_);

    auto plan = std::make_shared<Extracted>();
    plan->file = read_;
    plan->library = read_library_;
    plan->media_root = open_library_.media_root();
    plan->add_items = choices.value(QStringLiteral("addItemsToLibrary"), true).toBool();
    plan->add_service = read_->rundown && choices.value(QStringLiteral("addService"), true).toBool();
    plan->folder_name = util::trim(choices.value(QStringLiteral("folderName")).toString().toStdString());
    if (!plan->add_items && !plan->add_service) {
        return refuse_import(tr("Choose to add the items, the service, or both."));
    }

    std::set<std::size_t> chosen;
    for (const auto& v : choices.value(QStringLiteral("items")).toList()) {
        const auto i = v.toInt();
        if (i >= 0 && static_cast<std::size_t>(i) < m.items.size()
            && read_->items[static_cast<std::size_t>(i)].document) {
            chosen.insert(static_cast<std::size_t>(i));
        }
    }
    // Where the decks go matters only when there are items to land.
    exchange::OutputMapping mapping;
    for (const auto& v : choices.value(QStringLiteral("mapping")).toList()) {
        mapping.push_back(v.toString().toStdString());
    }
    if (!chosen.empty()) {
        if (const auto why = exchange::mapping_refusal(m.outputs, outputs, mapping); !why.empty()) {
            return refuse_import(q(why));
        }
    }

    // Each item chosen, landed on the outputs chosen; its files stay named
    // by entry until they are the library's.
    std::map<std::string, std::string, std::less<>> entries;
    for (const auto& f : m.media) {
        entries.emplace(f.entry, f.entry);
    }
    std::set<std::string> families;
    std::set<std::string> named;
    std::set<std::string> said;  // items already named in a note
    for (const auto i : chosen) {
        // Only items that read are chosen (above).
        const auto& document = read_->items[i].document;
        if (!document) {
            continue;
        }
        auto adapted = exchange::adapt_document(*document, m.outputs, outputs, mapping, entries);
        if (!adapted.document) {
            plan->notes.push_back(tr("“%1” was left out. %2").arg(q(m.items[i].name), q(adapted.refusal)));
            said.insert(m.items[i].entry);
            continue;
        }
        if (adapted.document->name.empty()) {
            adapted.document->name = m.items[i].name;
        }
        for (const auto& e : exchange::entries_named(*adapted.document)) {
            named.insert(e);
        }
        for (const auto& f : db::font_families_named_in(adapted.document->to_json())) {
            families.insert(f);
        }
        plan->item_of_entry.emplace(m.items[i].entry, plan->items.size());
        plan->items.push_back({i, std::move(*adapted.document)});
    }
    if (plan->add_service && read_->rundown) {
        for (const auto& row : read_->rundown->rows) {
            if (!row.item.empty() && !plan->item_of_entry.contains(row.item) && !said.contains(row.item)) {
                plan->notes.push_back(tr("The row “%1” was left out: its item is not imported.").arg(q(row.title)));
            }
        }
    }
    if (plan->items.empty() && !plan->add_service) {
        return refuse_import(plan->notes.isEmpty() ? tr("Choose at least one item to import.")
                                                   : plan->notes.join(QStringLiteral("\n")));
    }

    // The files the items still name, and the service's own.
    for (const auto& f : m.media) {
        if (!named.contains(f.entry)) {
            continue;
        }
        const auto kind = kind_of(f.kind);
        const std::string title = !f.title.empty() ? f.title
                                  : kind == db::MediaKind::Video ? std::string("Imported video")
                                                                 : std::string("Imported picture");
        if (!db::is_media_filename(kind, f.entry)) {
            plan->notes.push_back(tr("“%1” is not a kind of file the library takes, so it is left out.").arg(q(title)));
        } else if (f.size > db::max_import_bytes(kind)) {
            plan->notes.push_back(tr("“%1” is larger than the library takes, so it is left out.").arg(q(title)));
        } else {
            plan->needs.push_back({f.entry, false, kind, title, f.size});
        }
    }
    if (plan->add_service && read_->rundown && read_->package) {
        std::set<std::string> loose;
        for (const auto& row : read_->rundown->rows) {
            const auto kind = kind_of(row.kind);
            if (!kind) {
                continue;
            }
            for (const auto& name : row.files) {
                const auto* e = read_->package->find(name);
                if (e == nullptr || !loose.insert(name).second) {
                    continue;
                }
                const auto leaf = fs::path(name).filename().string();
                if (!db::is_media_filename(*kind, leaf)) {
                    plan->notes.push_back(
                        tr("“%1” is not a kind of file the library takes, so it is left out.").arg(q(leaf)));
                } else if (e->size > db::max_import_bytes(*kind)) {
                    plan->notes.push_back(tr("“%1” is larger than the library takes, so it is left out.").arg(q(leaf)));
                } else {
                    plan->needs.push_back({name, true, *kind, loose_title(name), e->size});
                }
            }
        }
    }

    auto* db = open_library_.database();
    {
        db::FontRepository fonts(*db);
        std::vector<std::string> kept;
        plan->fonts = exchange::fonts_to_take(m, families, fonts.list_all(), kept);
        for (const auto& k : kept) {
            plan->notes.push_back(tr("The library's own font “%1” is kept.").arg(q(k)));
        }
    }
    if (auto* repo = media_.media_repo()) {
        for (const auto kind : db::k_media_kinds) {
            auto& names = plan->library_files[kind];
            for (const auto& file : repo->list_files(kind, false)) {
                names.push_back(file.filename);
            }
        }
    }
    if (!plan->fonts.empty()) {
        plan->font_dir = std::make_shared<QTemporaryDir>();
        if (!plan->font_dir->isValid()) {
            return refuse_import(tr("There is nowhere to unpack the file's fonts."));
        }
    }

    const qint64 id = ++last_request_id_;
    set_busy(true);
    pool_.start([this, id, plan, cancelled = cancelled_] {
        auto last = std::chrono::steady_clock::now() - k_progress_interval;
        const auto report = [this, &last](double fraction) {
            const auto now = std::chrono::steady_clock::now();
            if (fraction < 1.0 && now - last < k_progress_interval) {
                return;
            }
            last = now;
            QMetaObject::invokeMethod(this, [this, fraction] { set_progress(fraction); }, Qt::QueuedConnection);
        };
        try {
            extract_files(*plan, *cancelled, report);
        } catch (const std::exception& ex) {
            qCWarning(lcImport) << "unpacking" << ex.what();
            plan->error = tr("The file could not be unpacked.");
        }
        if (cancelled->load()) {
            plan->stopped = true;
        }
        if (plan->stopped || !plan->error.isEmpty()) {
            plan->discard_staged();
        }
        QMetaObject::invokeMethod(
            this, [this, id, plan, cancelled] { commit(id, plan, cancelled); }, Qt::QueuedConnection);
    });
    return id;
} catch (const std::exception& ex) {
    return refuse_import(report_entry_error(__func__, ex));
}

void FreeformImportController::commit(qint64 id, const std::shared_ptr<Extracted>& plan,
                                      const CancelFlag& cancelled)
{
    set_busy(false);
    const auto fail = [&](const QString& why) {
        plan->discard_staged();
        emit importFinished(id, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), why}});
    };
    if (plan->stopped || cancelled->load()) {
        return fail(stopped_text());
    }
    if (!plan->error.isEmpty()) {
        return fail(plan->error);
    }
    if (plan->library != library_key() || !why_not_here().isEmpty()) {
        return fail(tr("The library changed while the file was imported."));
    }
    auto* db = open_library_.database();
    auto* freeform_repo = freeform_.repo();
    auto* media_repo = media_.media_repo();
    auto* service_repo = library_.service_repo();
    if (db == nullptr || freeform_repo == nullptr || media_repo == nullptr || service_repo == nullptr) {
        return fail(refusal::no_library());
    }

    Committer committer(*plan, *db, *freeform_repo, *media_repo, *service_repo, media_, session_.stack_defs());
    try {
        committer.run();
    } catch (const std::exception& ex) {
        committer.undo();
        return fail(report_entry_error("import", ex));
    }
    // A reused file's copy is not needed.
    plan->discard_staged();

    // What shows the new rows is read again; the import has happened
    // whatever a refresh says.
    try {
        media_.announce_adopted(committer.adopted(), committer.adopted_names());
        for (const auto kind : committer.folders_made()) {
            media_.refresh_folders(kind);
        }
        freeform_.rebuild_freeform_browser();
        if (committer.service_id() >= 0) {
            library_.refresh_services();
        }
    } catch (const std::exception& ex) {
        plan->notes.push_back(report_entry_error("import refresh", ex));
    }
    if (!plan->font_files.empty()) {
        // The font controller holds the folder until it has read them.
        fonts_.add_files_keeping(plan->font_files, plan->font_dir);
    }
    if (plan->file == read_) {
        discardRead();
    }
    emit importFinished(id, {
                                {QStringLiteral("ok"), true},
                                {QStringLiteral("items"), committer.items_made()},
                                {QStringLiteral("files"), committer.files_new()},
                                {QStringLiteral("fonts"), static_cast<int>(plan->font_files.size())},
                                {QStringLiteral("serviceId"), committer.service_id()},
                                {QStringLiteral("notes"), plan->notes},
                            });
}

}  // namespace seventhslide::app
