#include "font_store.hpp"

#include "entry_error.hpp"

#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/db/sync_tables.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/read_file.hpp>
#include <seventhslide/util/strings.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QObject>
#include <QRawFont>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace seventhslide::app {
namespace {

/// Where the shipped fonts are in the program's resources: a folder per family,
/// named exactly for it.
constexpr auto k_bundled_dir = ":/bundled_fonts";

/// Whether the system's font engine opens ``bytes``. A font that passes
/// ``read_font_file`` can still be one it will not draw.
[[nodiscard]] bool engine_opens(std::string_view bytes)
{
    QRawFont raw;
    raw.loadFromData(QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size())),
                     12.0, QFont::PreferDefaultHinting);
    return raw.isValid();
}

/// Delete ``filename`` of ``folder``, if that is a file's name at all: one from
/// a row that came by sync could say anything.
void remove_font_file(const std::filesystem::path& folder, std::string_view filename)
{
    if (db::is_media_file_name(filename)) {
        std::error_code ec;
        std::filesystem::remove(folder / filename, ec);
    }
}

[[nodiscard]] std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

FontStore::FontStore(db::Database& db, std::filesystem::path media_root)
    : db_(db), fonts_(db), folder_(std::move(media_root) / model::k_fonts_subdir)
{
}

FontStore::Checked FontStore::check_and_write(const std::filesystem::path& path,
                                               const std::filesystem::path& media_root)
{
    Checked refused;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        refused.error = QObject::tr("This font file could not be read.").toStdString();
        return refused;
    }
    if (size > static_cast<std::uintmax_t>(fonts::k_max_font_file_bytes)) {
        refused.error = QObject::tr("This font file is too large.").toStdString();
        return refused;
    }
    try {
        const auto bytes = util::read_file(path);
        if (!bytes) {
            refused.error = QObject::tr("This font file could not be read.").toStdString();
            return refused;
        }
        auto read = fonts::read_font_file(*bytes);
        if (!read.refusal.empty()) {
            refused.error = std::move(read.refusal);
            return refused;
        }
        if (!std::ranges::all_of(read.faces, [](const auto& f) { return engine_opens(f.bytes); })) {
            refused.error = QObject::tr("This font file is damaged.").toStdString();
            return refused;
        }
        return write_faces(std::move(read.faces), media_root / model::k_fonts_subdir);
    } catch (const std::exception& ex) {
        refused.error = report_entry_error("reading a font file", ex).toStdString();
        return refused;
    }
}

FontStore::Checked FontStore::write_faces(std::vector<fonts::FontFileFace> faces,
                                          const std::filesystem::path& folder)
{
    Checked checked;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    for (auto& face : faces) {
        auto filename = crypto::sha256_hex(face.bytes);
        filename += face.extension;
        const auto file = folder / filename;
        // One already there is kept only if it is these bytes: a file lost or
        // damaged is put right by adding the font again.
        const auto held = util::read_file(file);
        if (!held || *held != face.bytes) {
            if (!util::write_file_atomically(file, face.bytes)) {
                discard(checked);
                Checked failed;
                failed.error =
                    QObject::tr("The font could not be saved to the library's folder.").toStdString();
                return failed;
            }
            checked.written.push_back(file);
        }
        checked.rows.push_back(db::LibraryFont{.filename = std::move(filename),
                                               .display_name = face.family + " " + face.style,
                                               .family = std::move(face.family),
                                               .style = std::move(face.style),
                                               .bold = face.bold,
                                               .italic = face.italic,
                                               .variable = face.variable,
                                               .added_at = now_ms()});
    }
    return checked;
}

void FontStore::discard(const Checked& checked)
{
    std::error_code ec;
    for (const auto& file : checked.written) {
        std::filesystem::remove(file, ec);
    }
}

FontStore::Added FontStore::add_checked(const Checked& checked, bool keep_occupied)
{
    Added added;
    if (!checked.error.empty()) {
        added.error = checked.error;
        return added;
    }
    try {
        std::set<std::string> gone;
        std::set<std::string> left_out;
        std::size_t already = 0;
        std::vector<db::LibraryFont> faces;
        db::Transaction tx(db_);
        for (const auto& row : checked.rows) {
            if (keep_occupied && !fonts_.get(row.filename)) {
                const auto family = fonts_.list_family(row.family);
                const bool occupied = std::ranges::any_of(family, [&](const db::LibraryFont& other) {
                    return other.filename != row.filename && other.bold == row.bold && other.italic == row.italic;
                });
                if (occupied) {
                    added.kept.push_back(row.display_name);
                    left_out.insert(row.filename);
                    continue;
                }
            }
            faces.push_back(row);
            already += fonts_.get(row.filename) ? 1 : 0;
            for (const auto& other : fonts_.list_family(row.family)) {
                if (other.filename != row.filename && other.bold == row.bold
                    && other.italic == row.italic) {
                    fonts_.remove(other.filename);
                    gone.insert(other.filename);
                }
            }
            fonts_.add(row);
        }
        tx.commit();

        for (const auto& filename : gone) {
            remove_font_file(folder_, filename);
        }
        // A face left out takes the file this check wrote for it with it.
        std::error_code ec;
        for (const auto& written : checked.written) {
            if (left_out.contains(written.filename().string())) {
                std::filesystem::remove(written, ec);
            }
        }
        added.replaced = gone.size();
        added.already = already;
        added.faces = std::move(faces);
    } catch (const std::exception& ex) {
        // Only what this wrote goes: the rest is the library's.
        discard(checked);
        added.error = report_entry_error("adding a font", ex).toStdString();
    }
    return added;
}

FontStore::Added FontStore::import_file(const std::filesystem::path& path)
{
    return add_checked(check_and_write(path, folder_.parent_path()));
}

std::string FontStore::remove(std::string_view filename)
{
    try {
        fonts_.remove(filename);
    } catch (const std::exception& ex) {
        return report_entry_error("removing a font", ex).toStdString();
    }
    remove_font_file(folder_, filename);
    return {};
}

std::string FontStore::remove_rows(const std::vector<db::LibraryFont>& rows)
{
    try {
        db::Transaction tx(db_);
        for (const auto& row : rows) {
            fonts_.remove(row.filename);
        }
        tx.commit();
    } catch (const std::exception& ex) {
        return report_entry_error("removing a font", ex).toStdString();
    }
    for (const auto& row : rows) {
        remove_font_file(folder_, row.filename);
    }
    return {};
}

std::string FontStore::remove_faces(const std::vector<std::string>& filenames)
{
    try {
        std::vector<db::LibraryFont> rows;
        for (const auto& filename : filenames) {
            const auto font = fonts_.get(filename);
            if (!font) {
                continue;
            }
            auto slot = fonts_.list_family(font->family);
            std::erase_if(slot, [&](const auto& row) {
                return row.bold != font->bold || row.italic != font->italic;
            });
            rows.insert(rows.end(), slot.begin(), slot.end());
        }
        return remove_rows(rows);
    } catch (const std::exception& ex) {
        return report_entry_error("removing a font", ex).toStdString();
    }
}

std::string FontStore::remove_family(std::string_view family)
{
    try {
        return remove_rows(fonts_.list_family(family));
    } catch (const std::exception& ex) {
        return report_entry_error("removing a font", ex).toStdString();
    }
}

fonts::FontIndex FontStore::library_index()
{
    fonts::FontIndex index;
    for (const auto& face : library_faces()) {
        const auto style = face.bold ? (face.italic ? "Bold Italic" : "Bold")
                                     : (face.italic ? "Italic" : "Regular");
        index[util::to_lower(face.family)].push_back(
            fonts::FontFaceEntry{style, folder_ / face.filename, face.variable});
    }
    return index;
}

std::vector<db::LibraryFont> FontStore::library_faces()
{
    auto faces = fonts_.list_faces();
    std::erase_if(faces, [](const auto& face) { return !db::is_media_file_name(face.filename); });
    return faces;
}

std::filesystem::path FontStore::file_of(const db::LibraryFont& face) const
{
    return folder_ / face.filename;
}

std::vector<std::string> FontStore::library_families()
{
    std::vector<std::string> families;
    for (auto& face : library_faces()) {
        if (families.empty() || util::to_lower(families.back()) != util::to_lower(face.family)) {
            families.push_back(std::move(face.family));
        }
    }
    return families;
}

fonts::FontIndex FontStore::shipped_index()
{
    fonts::FontIndex index;
    for (const auto& family : bundled_families()) {
        const QDir dir(QLatin1String(k_bundled_dir) + QLatin1Char('/')
                       + QString::fromStdString(family));
        auto& faces = index[util::to_lower(family)];
        for (const auto& name :
             dir.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files)) {
            // "OpenSans-BoldItalic.ttf": fetch_bundled_fonts.py names them so.
            const auto style = name.mid(name.lastIndexOf(QLatin1Char('-')) + 1).chopped(4);
            faces.push_back(fonts::FontFaceEntry{style.toStdString(),
                                                 dir.filePath(name).toStdString()});
        }
    }
    return index;
}

std::vector<std::string> FontStore::bundled_families()
{
    std::vector<std::string> families;
    for (const auto& family :
         QDir(QLatin1String(k_bundled_dir)).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        families.push_back(family.toStdString());
    }
    std::ranges::sort(families);
    return families;
}

std::optional<std::vector<std::string>> FontStore::bundled_to_take()
{
    if (fonts_.bundled_taken()) {
        return std::nullopt;
    }
    auto families = bundled_families();
    std::erase_if(families, [this](const auto& family) { return !fonts_.list_family(family).empty(); });
    return families;
}

FontStore::Checked FontStore::write_bundled(const std::vector<std::string>& families,
                                            const std::filesystem::path& media_root)
{
    try {
        const auto shipped = shipped_index();
        std::vector<fonts::FontFileFace> faces;
        for (const auto& family : families) {
            const auto entries = shipped.find(util::to_lower(family));
            if (entries == shipped.end()) {
                continue;
            }
            for (const auto& entry : entries->second) {
                QFile file(QString::fromStdString(entry.path.string()));
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const auto bytes = file.readAll();
                auto read = fonts::read_font_file(std::string_view(bytes.constData(), bytes.size()));
                // The folder names the family: a file may call itself otherwise
                // ("EB Garamond 08" ships as EB Garamond).
                for (auto& face : read.faces) {
                    face.family = family;
                }
                std::ranges::move(read.faces, std::back_inserter(faces));
            }
        }
        return write_faces(std::move(faces), media_root / model::k_fonts_subdir);
    } catch (const std::exception& ex) {
        Checked failed;
        failed.error = report_entry_error("copying the shipped fonts", ex).toStdString();
        return failed;
    }
}

FontStore::Added FontStore::take_bundled(const Checked& checked)
{
    auto added = add_checked(checked);
    if (added.error.empty()) {
        try {
            fonts_.note_bundled_taken();
        } catch (const std::exception& ex) {
            added.error = report_entry_error("copying the shipped fonts", ex).toStdString();
        }
    }
    return added;
}

FontStore::Added FontStore::take_bundled()
{
    try {
        const auto families = bundled_to_take();
        return families ? take_bundled(write_bundled(*families, folder_.parent_path())) : Added{};
    } catch (const std::exception& ex) {
        Added failed;
        failed.error = report_entry_error("copying the shipped fonts", ex).toStdString();
        return failed;
    }
}

}  // namespace seventhslide::app
