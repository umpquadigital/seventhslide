#pragma once

// A media file as sync names and checks it: in pieces of
// ``k_media_piece_bytes`` (the last one shorter), each with its SHA-256,
// and the file named by its root, the SHA-256 of those hashes one after
// another. A change file declares a file by its root
// (``db::MediaDigest::root``). A computer sending a file sends the pieces'
// hashes first, which the root checks, then the pieces, each checked as it
// comes: a damaged one is known at once, and which computer sent it, and a
// file can come piece by piece from several computers.
//
// Each computer keeps the hashes of the files it has (``PieceHashes``,
// under the library's sync folder): worked out once, where the file was
// imported or arrived, rather than read through again for each computer
// that asks for it.
//
// Seam: the crypto module, and the file system.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::sync {

inline constexpr std::int64_t k_media_piece_bytes = 4 * 1024 * 1024;
/// A piece's hash, raw.
inline constexpr std::size_t k_piece_hash_bytes = 32;

/// How many pieces a file of ``size`` bytes is: one at least, an empty
/// file's.
[[nodiscard]] std::int64_t piece_count(std::int64_t size);

/// The SHA-256 of ``bytes``, raw: a piece's hash.
[[nodiscard]] std::string piece_hash(std::string_view bytes);

/// The root of ``hashes``, the pieces' hashes one after another: lower-case
/// hex, as a file is declared by.
[[nodiscard]] std::string media_root(std::string_view hashes);

/// The pieces' hashes of the file at ``path``, read through; nullopt when it
/// cannot be read.
[[nodiscard]] std::optional<std::string> hash_media_file(const std::filesystem::path& path);

/// The hashes a computer keeps of its media files, by path under the media
/// folder ("images/<name>"): ``<folder>/<path>``, the file's size in 8 bytes,
/// then the raw hashes. Rebuilt from the file when lost. Not used for a file
/// changed since they were kept (``of``).
class PieceHashes {
public:
    /// None kept when ``folder`` is empty.
    explicit PieceHashes(std::filesystem::path folder) : folder_(std::move(folder)) {}

    /// Those kept for ``path``, when they are of a file of ``size``.
    [[nodiscard]] std::optional<std::string> load(std::string_view path, std::int64_t size) const;
    /// Keep ``hashes`` of a file of ``size`` for ``path``; false when they
    /// could not be written.
    bool save(std::string_view path, std::int64_t size, std::string_view hashes) const;
    /// The file at ``path`` as it now is is what those kept are the hashes
    /// of (it was just put in place, checked).
    void keep_for_now(std::string_view path) const;
    /// Keep none for ``path`` any more: the file is gone.
    void forget(std::string_view path) const;
    /// Those kept, unless ``file`` changed since; otherwise read through it
    /// (and kept). Nullopt when it cannot be read.
    [[nodiscard]] std::optional<std::string> of(std::string_view path,
                                                const std::filesystem::path& file) const;

private:
    std::filesystem::path folder_;
};

}  // namespace seventhslide::sync
