#include "seventhslide/exchange/bundle_io.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/model/static_media.hpp"
#include "seventhslide/util/crc32.hpp"
#include "seventhslide/util/strings.hpp"
#include "seventhslide/util/zip_writer.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <set>
#include <system_error>
#include <tuple>

namespace seventhslide::exchange {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::size_t k_chunk = 1 << 20;
constexpr std::string_view k_not_ours =
    "The file is not a Freeform file or a service package from SeventhSlide.";

struct Hashed {
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
    std::string sha256;
};

/// Counts bytes done against a total, and asks whether to go on.
class Tally {
public:
    Tally(const Progress& progress, std::uint64_t total) : progress_(progress), total_(total) {}
    [[nodiscard]] bool add(std::uint64_t bytes)
    {
        done_ += bytes;
        if (!progress_) {
            return true;
        }
        if (!progress_(std::min(done_, total_), total_)) {
            stopped_ = true;
        }
        return !stopped_;
    }
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }
    [[nodiscard]] std::uint64_t done() const noexcept { return done_; }

private:
    const Progress& progress_;
    std::uint64_t total_ = 0;
    std::uint64_t done_ = 0;
    bool stopped_ = false;
};

/// The file's size, CRC and SHA-256 in one read, or nullopt when it cannot
/// be read (or the tally stops it).
[[nodiscard]] std::optional<Hashed> hash_file(const fs::path& path, Tally& tally)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    Hashed h;
    util::Crc32 crc;
    crypto::Sha256 sha;
    std::string chunk(k_chunk, '\0');
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        const std::string_view piece(chunk.data(), got);
        crc.update(piece);
        sha.update(piece);
        h.size += got;
        if (!tally.add(got)) {
            return std::nullopt;
        }
    }
    if (in.bad()) {
        return std::nullopt;
    }
    h.crc = crc.value();
    h.sha256 = sha.finish_hex();
    return h;
}

[[nodiscard]] std::uint64_t size_of(const fs::path& path)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

[[nodiscard]] util::ZipTime now_local()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

/// Write ``file`` with ``write`` (which fills a ZipWriter), removing it on
/// anything but success.
template <class Fn>
WriteOutcome write_zip(const fs::path& file, WriteOutcome outcome, Fn&& write)
{
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            outcome.error = "The file could not be written there.";
            return outcome;
        }
        util::ZipWriter zip(out, now_local());
        if (!write(zip, outcome) || !zip.finish()) {
            if (outcome.error.empty() && !outcome.stopped) {
                outcome.error = zip.error().empty() ? "The file could not be written." : zip.error();
            }
            out.close();
            std::error_code ec;
            fs::remove(file, ec);
            outcome.ok = false;
            return outcome;
        }
        out.close();
        if (!out) {
            std::error_code ec;
            fs::remove(file, ec);
            outcome.error = "The file could not be written: is the disk full?";
            return outcome;
        }
    }
    outcome.ok = true;
    return outcome;
}

}  // namespace

WriteOutcome write_bundle(const BundleSource& source, const fs::path& file, const Progress& progress)
{
    WriteOutcome outcome;

    // Every file is read twice, once to name it and once to pack it.
    std::uint64_t total = 0;
    std::set<fs::path> paths;
    for (const auto& [ref, media] : source.media) {
        if (!media.path.empty() && paths.insert(media.path).second) {
            total += size_of(media.path);
        }
    }
    for (const auto& font : source.fonts) {
        if (paths.insert(font.path).second) {
            total += size_of(font.path);
        }
    }
    total *= 2;
    Tally tally(progress, total);

    Manifest m;
    m.freeform_schema_version = freeform::k_schema_version;
    m.made_by = source.made_by;
    m.made_at = source.made_at;
    m.source_library = source.source_library;
    m.outputs = source.outputs;
    m.folders = source.folders;
    m.has_rundown = source.rundown.has_value();
    m.fonts_not_included = source.fonts_not_included;

    // Name each file by its bytes: the same bytes, by any reference, go once.
    struct Packed {
        fs::path path;
        Hashed hash;
    };
    std::map<std::string, std::string, std::less<>> entry_of;  // reference -> entry
    std::map<std::string, Packed, std::less<>> packed;         // entry -> file
    std::map<fs::path, std::optional<std::string>> entry_of_path;
    std::set<std::string> missing_titles;
    for (const auto& [ref, media] : source.media) {
        std::optional<std::string> entry;
        if (!media.path.empty()) {
            if (const auto known = entry_of_path.find(media.path); known != entry_of_path.end()) {
                entry = known->second;
            } else {
                const auto hash = hash_file(media.path, tally);
                if (tally.stopped()) {
                    outcome.stopped = true;
                    return outcome;
                }
                if (hash) {
                    entry = media_entry(hash->sha256, media.path.extension().string());
                    if (!packed.contains(*entry)) {
                        packed.emplace(*entry, Packed{media.path, *hash});
                        m.media.push_back({*entry, media.kind, media.title, hash->size, hash->sha256});
                    }
                }
                entry_of_path.emplace(media.path, entry);
            }
        }
        if (entry) {
            entry_of.emplace(ref, *entry);
        } else if (!media.path.empty()) {
            outcome.media_unreadable.push_back(media.title.empty() ? std::string(model::media_basename(ref))
                                                                   : media.title);
        } else {
            missing_titles.insert(media.title.empty() ? std::string(model::media_basename(ref)) : media.title);
        }
    }
    m.media_not_included.assign(missing_titles.begin(), missing_titles.end());

    std::map<std::string, Packed, std::less<>> packed_fonts;
    for (const auto& font : source.fonts) {
        const auto hash = hash_file(font.path, tally);
        if (tally.stopped()) {
            outcome.stopped = true;
            return outcome;
        }
        if (!hash) {
            outcome.font_files_unreadable.push_back(font.font.display_name);
            continue;
        }
        const auto entry = font_entry(hash->sha256, font.path.extension().string());
        if (packed_fonts.contains(entry)) {
            continue;
        }
        packed_fonts.emplace(entry, Packed{font.path, *hash});
        m.fonts.push_back({entry, font.font.family, font.font.style, font.font.bold, font.font.italic,
                           font.font.variable, hash->size, hash->sha256});
    }

    // A family whose every face was unreadable is not in the file either.
    for (const auto& font : source.fonts) {
        const auto family = util::to_lower(font.font.family);
        const bool packed_any = std::ranges::any_of(m.fonts, [&](const BundleFont& f) {
            return util::to_lower(f.family) == family;
        });
        if (!packed_any && std::ranges::none_of(m.fonts_not_included, [&](const std::string& f) {
                return util::to_lower(f) == family;
            })) {
            m.fonts_not_included.push_back(font.font.family);
        }
    }

    std::vector<std::string> documents;
    for (std::size_t i = 0; i < source.items.size(); ++i) {
        const auto& item = source.items[i];
        auto doc = item.document;
        point_at_entries(doc, entry_of);
        documents.push_back(doc.to_json().dump());
        BundleItem listed;
        listed.entry = item_entry(i + 1);
        listed.preview = item.preview_png.empty() ? std::string() : preview_entry(i + 1);
        listed.name = item.name;
        listed.folder = item.folder;
        listed.tags = item.tags;
        m.items.push_back(std::move(listed));
    }

    outcome.media_not_included = m.media_not_included;
    // The families the library had no face of: one whose faces could not be
    // read is said as that, not as this.
    for (const auto& family : m.fonts_not_included) {
        const bool unreadable = std::ranges::any_of(source.fonts, [&](const FontToPack& f) {
            return util::iequals(f.font.family, family);
        });
        if (!unreadable) {
            outcome.fonts_not_included.push_back(family);
        }
    }
    for (const auto& title : outcome.media_unreadable) {
        m.media_not_included.push_back(title);
    }
    outcome.media_files = m.media.size();
    outcome.font_files = m.fonts.size();

    return write_zip(file, std::move(outcome), [&](util::ZipWriter& zip, WriteOutcome& out) {
        if (!zip.add(k_manifest_entry, to_json(m).dump(2))) {
            return false;
        }
        if (source.rundown && !zip.add(k_rundown_entry, to_json(*source.rundown).dump(2))) {
            return false;
        }
        for (std::size_t i = 0; i < documents.size(); ++i) {
            if (!zip.add(m.items[i].entry, documents[i])) {
                return false;
            }
            if (!m.items[i].preview.empty() && !zip.add(m.items[i].preview, source.items[i].preview_png)) {
                return false;
            }
        }
        const auto pack = [&](const std::string& entry, const Packed& p) {
            const std::uint64_t before = tally.done();
            const bool ok = zip.add_file(entry, p.path, p.hash.size, p.hash.crc,
                                         [&](std::uintmax_t copied) {
                                             return tally.add(before + copied - tally.done());
                                         });
            if (!ok && tally.stopped()) {
                out.stopped = true;
            }
            return ok;
        };
        for (const auto& [entry, p] : packed) {
            if (!pack(entry, p)) {
                return false;
            }
        }
        for (const auto& [entry, p] : packed_fonts) {
            if (!pack(entry, p)) {
                return false;
            }
        }
        if (progress) {
            (void)progress(total, total);
        }
        return true;
    });
}

WriteOutcome write_package(const fs::path& file, std::string_view bundle_name, const fs::path& bundle,
                           const std::vector<LooseFile>& loose, const Progress& progress)
{
    WriteOutcome outcome;
    std::uint64_t total = size_of(bundle);
    for (const auto& f : loose) {
        total += size_of(f.path);
    }
    Tally tally(progress, total * 2);
    struct Packed {
        std::string name;
        fs::path path;
        Hashed hash;
    };
    std::vector<Packed> packed;
    const auto hash_one = [&](std::string name, const fs::path& path) {
        const auto hash = hash_file(path, tally);
        if (!hash) {
            return false;
        }
        packed.push_back({std::move(name), path, *hash});
        return true;
    };
    if (!hash_one(std::string(bundle_name), bundle)) {
        outcome.stopped = tally.stopped();
        outcome.error = outcome.stopped ? std::string() : "The service's Freeform file could not be read.";
        return outcome;
    }
    for (const auto& f : loose) {
        if (!hash_one(f.name, f.path)) {
            if (tally.stopped()) {
                outcome.stopped = true;
                return outcome;
            }
            outcome.media_not_included.push_back(f.name);
        }
    }
    return write_zip(file, std::move(outcome), [&](util::ZipWriter& zip, WriteOutcome& out) {
        for (const auto& p : packed) {
            const std::uint64_t before = tally.done();
            if (!zip.add_file(p.name, p.path, p.hash.size, p.hash.crc, [&](std::uintmax_t copied) {
                    return tally.add(before + copied - tally.done());
                })) {
                out.stopped = tally.stopped();
                return false;
            }
        }
        out.media_files = packed.size() - 1;
        return true;
    });
}

std::optional<OpenedFile> open_freeform_file(const fs::path& path, std::string& refusal)
{
    auto outer = util::ZipReader::open(path, refusal);
    if (!outer) {
        return std::nullopt;
    }
    std::optional<util::ZipReader> package;
    std::optional<util::ZipReader> bundle;
    if (outer->find(k_manifest_entry) != nullptr) {
        bundle = std::move(outer);
    } else {
        // A package: one Freeform file at its top.
        const util::ZipEntry* inner = nullptr;
        for (const auto& e : outer->entries()) {
            if (e.name.find('/') == std::string::npos && e.name.size() > k_bundle_extension.size()
                && util::iequals(std::string_view(e.name).substr(e.name.size() - k_bundle_extension.size()), k_bundle_extension)) {
                if (inner != nullptr) {
                    refusal = std::string(k_not_ours);
                    return std::nullopt;
                }
                inner = &e;
            }
        }
        if (inner == nullptr) {
            refusal = std::string(k_not_ours);
            return std::nullopt;
        }
        bundle = util::ZipReader::open_range(outer->path(), outer->base() + inner->data_offset, inner->size,
                                             refusal);
        if (!bundle) {
            return std::nullopt;
        }
        package = std::move(outer);
    }

    const auto* manifest_entry = bundle->find(k_manifest_entry);
    if (manifest_entry == nullptr) {
        refusal = std::string(k_not_ours);
        return std::nullopt;
    }
    const auto read_json = [&refusal](const util::ZipReader& zip, const util::ZipEntry& e,
                                      std::uint64_t max) -> std::optional<json> {
        const auto text = zip.read(e, max, refusal);
        if (!text) {
            return std::nullopt;
        }
        auto j = parse_json_limited(*text);
        if (!j) {
            refusal = std::string(k_not_ours);
        }
        return j;
    };
    const auto manifest_json = read_json(*bundle, *manifest_entry, k_max_manifest_bytes);
    if (!manifest_json) {
        return std::nullopt;
    }
    auto manifest = read_manifest(*manifest_json, refusal);
    if (!manifest) {
        return std::nullopt;
    }
    if (manifest->freeform_schema_version != freeform::k_schema_version) {
        refusal = manifest->freeform_schema_version > freeform::k_schema_version
            ? "The file was made by a newer SeventhSlide. Update SeventhSlide to import it."
            : "The file was made by an older SeventhSlide, whose Freeform items this one cannot read.";
        return std::nullopt;
    }
    // Every entry the manifest names is there, at the size it says.
    for (const auto& m : manifest->media) {
        const auto* e = bundle->find(m.entry);
        if (e == nullptr || e->size != m.size) {
            refusal = "The file is damaged: some of its pictures or videos are missing.";
            return std::nullopt;
        }
    }
    for (const auto& f : manifest->fonts) {
        const auto* e = bundle->find(f.entry);
        if (e == nullptr || e->size != f.size) {
            refusal = "The file is damaged: some of its fonts are missing.";
            return std::nullopt;
        }
    }

    OpenedFile opened{std::move(*bundle), std::move(package), std::move(*manifest), std::nullopt, {}};
    if (opened.manifest.has_rundown) {
        const auto* e = opened.bundle.find(k_rundown_entry);
        if (e == nullptr) {
            refusal = std::string(k_not_ours);
            return std::nullopt;
        }
        const auto j = read_json(opened.bundle, *e, k_max_manifest_bytes);
        if (!j) {
            return std::nullopt;
        }
        opened.rundown = read_rundown(*j, opened.manifest.items, refusal);
        if (!opened.rundown) {
            return std::nullopt;
        }
    }

    // A document larger than a record may be is refused as it is read, and
    // a file whose documents together are more than a reader holds at once
    // is refused whole.
    const std::uint64_t max_document = db::k_largest_record;
    std::uint64_t documents = 0;
    for (const auto& item : opened.manifest.items) {
        if (const auto* e = opened.bundle.find(item.entry)) {
            documents += std::min<std::uint64_t>(e->size, max_document + 1);
        }
    }
    if (documents > k_max_documents_bytes) {
        refusal = "The file holds more Freeform items than can be imported at once. Export fewer at a time.";
        return std::nullopt;
    }
    for (const auto& item : opened.manifest.items) {
        OpenedFile::Item loaded;
        const auto* e = opened.bundle.find(item.entry);
        std::string why;
        std::optional<json> j;
        if (e == nullptr) {
            loaded.refusal = "It is missing from the file.";
        } else if (e->size > max_document) {
            loaded.refusal = "It is larger than a library item may be.";
        } else if (const auto text = opened.bundle.read(*e, max_document, why); !text) {
            loaded.refusal = why;
        } else if (j = parse_json_limited(*text); j) {
            if (auto refused = freeform::document_refusal(*j); !refused.empty()) {
                loaded.refusal = std::move(refused);
            } else if (auto doc = freeform::load_document(*j, item.name)) {
                loaded.document = std::move(*doc);
            }
        }
        if (!loaded.document && loaded.refusal.empty()) {
            loaded.refusal = "It is damaged.";
        }
        opened.items.push_back(std::move(loaded));
    }
    return opened;
}

std::string read_preview(const OpenedFile& file, std::size_t n)
{
    if (n >= file.manifest.items.size() || file.manifest.items[n].preview.empty()) {
        return {};
    }
    const auto* e = file.bundle.find(file.manifest.items[n].preview);
    std::string why;
    auto bytes = e != nullptr ? file.bundle.read(*e, k_max_preview_bytes, why) : std::nullopt;
    return bytes.value_or(std::string());
}

std::vector<std::size_t> fonts_to_take(const Manifest& file, const std::set<std::string>& families,
                                       const std::vector<db::LibraryFont>& library,
                                       std::vector<std::string>& kept)
{
    std::set<std::string> wanted;
    for (const auto& f : families) {
        wanted.insert(util::to_lower(f));
    }
    std::set<std::tuple<std::string, bool, bool>> held;
    std::set<std::string> files;
    for (const auto& f : library) {
        held.emplace(util::to_lower(f.family), f.bold, f.italic);
        files.insert(f.filename);
    }
    std::vector<std::size_t> take;
    for (std::size_t i = 0; i < file.fonts.size(); ++i) {
        const auto& f = file.fonts[i];
        const auto family = util::to_lower(f.family);
        // The same file already here is the same face: nothing to take or say.
        if (!wanted.contains(family) || files.contains(f.entry.substr(k_fonts_dir.size()))) {
            continue;
        }
        if (held.contains({family, f.bold, f.italic})) {
            kept.push_back(f.style.empty() ? f.family : f.family + " " + f.style);
            continue;
        }
        held.emplace(family, f.bold, f.italic);  // one face a slot
        take.push_back(i);
    }
    return take;
}

}  // namespace seventhslide::exchange
