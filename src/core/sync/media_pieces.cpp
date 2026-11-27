#include "seventhslide/sync/media_pieces.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/db/sync_tables.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <fstream>
#include <system_error>
#include <vector>

namespace seventhslide::sync {
namespace {

constexpr std::size_t k_size_bytes = 8;

/// ``size`` as it starts a file of kept hashes.
std::string size_bytes(std::int64_t size)
{
    std::string out(k_size_bytes, '\0');
    for (std::size_t i = 0; i < k_size_bytes; ++i) {
        out[i] = static_cast<char>((static_cast<std::uint64_t>(size) >> (8 * i)) & 0xff);
    }
    return out;
}

}  // namespace

std::int64_t piece_count(std::int64_t size)
{
    return size <= 0 ? 1 : (size + k_media_piece_bytes - 1) / k_media_piece_bytes;
}

std::string piece_hash(std::string_view bytes)
{
    return crypto::sha256(bytes);
}

std::string media_root(std::string_view hashes)
{
    return crypto::sha256_hex(hashes);
}

std::optional<std::string> hash_media_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string hashes;
    std::vector<char> piece(static_cast<std::size_t>(k_media_piece_bytes));
    do {
        in.read(piece.data(), static_cast<std::streamsize>(piece.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (in.bad()) {
            return std::nullopt;
        }
        // An empty file is one empty piece; otherwise no piece is empty.
        if (got > 0 || hashes.empty()) {
            hashes += piece_hash({piece.data(), got});
        }
    } while (in);
    return hashes;
}

std::optional<std::string> PieceHashes::load(std::string_view path, std::int64_t size) const
{
    if (folder_.empty() || !db::is_sync_media_path(path)) {
        return std::nullopt;
    }
    const auto kept = util::read_file(folder_ / path);
    if (!kept || kept->size() < k_size_bytes || size_bytes(size) != kept->substr(0, k_size_bytes)
        || static_cast<std::int64_t>(kept->size() - k_size_bytes)
               != piece_count(size) * static_cast<std::int64_t>(k_piece_hash_bytes)) {
        return std::nullopt;
    }
    return kept->substr(k_size_bytes);
}

bool PieceHashes::save(std::string_view path, std::int64_t size, std::string_view hashes) const
{
    if (folder_.empty() || !db::is_sync_media_path(path)) {
        return false;
    }
    const auto file = folder_ / path;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Rebuilt when lost, and written from more than one thread.
    return util::write_cache_file(file, size_bytes(size).append(hashes));
}

void PieceHashes::keep_for_now(std::string_view path) const
{
    if (folder_.empty() || !db::is_sync_media_path(path)) {
        return;
    }
    std::error_code ec;
    std::filesystem::last_write_time(folder_ / path, std::filesystem::file_time_type::clock::now(), ec);
}

void PieceHashes::forget(std::string_view path) const
{
    if (folder_.empty() || !db::is_sync_media_path(path)) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(folder_ / path, ec);
}

std::optional<std::string> PieceHashes::of(std::string_view path, const std::filesystem::path& file) const
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto changed = std::filesystem::last_write_time(file, ec);
    if (!ec && !folder_.empty() && db::is_sync_media_path(path)) {
        std::error_code kept_ec;
        const auto kept_at = std::filesystem::last_write_time(folder_ / path, kept_ec);
        if (!kept_ec && kept_at >= changed) {
            if (auto kept = load(path, static_cast<std::int64_t>(size))) {
                return kept;
            }
        }
    }
    auto hashes = hash_media_file(file);
    if (hashes) {
        (void)save(path, static_cast<std::int64_t>(size), *hashes);
    }
    return hashes;
}

}  // namespace seventhslide::sync
