#pragma once

// Taking fonts into a library: checked once, on the computer that adds them,
// then kept in the media folder's ``fonts``, one file per face, and synced as
// the library's other files are (``db::FontRepository``).
//
// The fonts SeventhSlide ships are in the program (``third_party/fonts``), and
// a library takes a copy of them all once. From there each is the library's like
// any other, to keep or remove, so a newer build shipping a newer version never
// changes how a library looks, and a computer on an older build still gets the
// font by sync.
//
// Seam: Qt Gui (the font engine's own check) and db/. Call on the GUI thread,
// with a QGuiApplication, but for ``check_and_write``.

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/fonts/font_file.hpp>
#include <seventhslide/fonts/font_registry.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

class FontStore {
public:
    /// What adding fonts came to.
    struct Added {
        /// The faces now in the library, as they are registered.
        std::vector<db::LibraryFont> faces;
        /// How many faces the library had that these took the place of.
        std::size_t replaced = 0;
        /// Those of ``faces`` the library had already, the same bytes.
        std::size_t already = 0;
        /// Faces left out to keep a face the library has in their slot
        /// (``add_checked``'s ``keep_occupied``), as "<family> <style>".
        std::vector<std::string> kept;
        /// Why nothing was added, in words for people; empty otherwise.
        std::string error;
    };

    /// A font file read and checked, its faces in the library's folder, and not
    /// yet in the library: all that may take a while, and none that needs the
    /// database.
    struct Checked {
        std::vector<db::LibraryFont> rows;
        /// The files this wrote: only these go if it comes to nothing.
        std::vector<std::filesystem::path> written;
        /// Why nothing was written, in words for people; empty otherwise.
        std::string error;
    };

    /// ``media_root`` is the library's media folder.
    FontStore(db::Database& db, std::filesystem::path media_root);

    /// Check the font file at ``path`` and write each of its faces to
    /// ``media_root``'s fonts under a name of its own. From any thread.
    [[nodiscard]] static Checked check_and_write(const std::filesystem::path& path,
                                                 const std::filesystem::path& media_root);
    /// Take back what ``check_and_write`` wrote. From any thread.
    static void discard(const Checked& checked);
    /// Put ``checked``'s faces in the library, in place of what it has in those
    /// slots (their files go too). The same bytes added again are the same record.
    /// With ``keep_occupied`` a face goes in only where the library has no other
    /// face in its slot (its family, case aside, bold and italic, as the font
    /// file says, read now); one left out loses the file this check wrote.
    [[nodiscard]] Added add_checked(const Checked& checked, bool keep_occupied = false);

    /// ``check_and_write`` and ``add_checked`` in one.
    [[nodiscard]] Added import_file(const std::filesystem::path& path);

    /// Take ``filename`` and its file out of the library. Why it could not be,
    /// in words for people, or empty.
    [[nodiscard]] std::string remove(std::string_view filename);
    /// Take the slots of ``filenames`` out of the library, all or none: each file,
    /// and any other that two computers adding apart left in its slot, or the next
    /// would only show up.
    [[nodiscard]] std::string remove_faces(const std::vector<std::string>& filenames);
    /// Take every face of ``family`` out of the library.
    [[nodiscard]] std::string remove_family(std::string_view family);

    /// The faces text can show, one per slot. A row whose name is not a file's
    /// name, which only a damaged sync could bring, is left out.
    [[nodiscard]] std::vector<db::LibraryFont> library_faces();
    /// The same, for the FontRegistry.
    [[nodiscard]] fonts::FontIndex library_index();
    /// Its families, one name each.
    [[nodiscard]] std::vector<std::string> library_families();
    /// Where a face of the library's is.
    [[nodiscard]] std::filesystem::path file_of(const db::LibraryFont& face) const;

    /// What SeventhSlide ships, for the FontRegistry. Asks nothing of the font
    /// files, each style being in its file's name, so none is unpacked.
    [[nodiscard]] static fonts::FontIndex shipped_index();
    /// The families SeventhSlide ships, sorted.
    [[nodiscard]] static std::vector<std::string> bundled_families();

    /// The shipped families to copy into the library, the first time only, so
    /// a family removed stays removed: nullopt once it has had its copy. One
    /// the library has a font of that name for keeps its own.
    [[nodiscard]] std::optional<std::vector<std::string>> bundled_to_take();
    /// Write the shipped ``families``' faces to ``media_root``'s fonts, as
    /// ``check_and_write`` does a file's, each in the family its folder names.
    /// From any thread.
    [[nodiscard]] static Checked write_bundled(const std::vector<std::string>& families,
                                               const std::filesystem::path& media_root);
    /// Put ``write_bundled``'s faces in the library, and note it has had its copy.
    [[nodiscard]] Added take_bundled(const Checked& checked);
    /// All three in one. ``faces`` is what was copied.
    [[nodiscard]] Added take_bundled();

private:
    [[nodiscard]] static Checked write_faces(std::vector<fonts::FontFileFace> faces,
                                             const std::filesystem::path& folder);
    [[nodiscard]] std::string remove_rows(const std::vector<db::LibraryFont>& rows);

    db::Database& db_;
    db::FontRepository fonts_;
    std::filesystem::path folder_;
};

}  // namespace seventhslide::app
