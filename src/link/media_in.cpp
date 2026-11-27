#include "seventhslide/link/media_in.hpp"

#include "seventhslide/link/off_thread.hpp"
#include "seventhslide/link/peer_shared.hpp"
#include "seventhslide/sync/media_pieces.hpp"
#include "seventhslide/util/atomic_file.hpp"

#include <QFile>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace seventhslide::link {
namespace {

/// How many whole pieces may wait to be written, the one being written
/// among them, before no more is read.
constexpr std::size_t k_pieces_waiting = 2;

std::filesystem::path part_of(const SyncedLibrary& lib, const std::string& path)
{
    return lib.media / (path + ".part");
}

/// The piece's hash in ``hashes``.
std::string hash_at(const std::string& hashes, std::int64_t offset)
{
    const auto index = static_cast<std::size_t>(offset / sync::k_media_piece_bytes);
    return hashes.substr(index * sync::k_piece_hash_bytes, sync::k_piece_hash_bytes);
}

/// The first of the pieces in ``found`` that is not as in ``hashes``, as an
/// offset in the file; none when each is, and there are as many.
std::optional<std::int64_t> first_bad_piece(const std::string& found, const std::string& hashes)
{
    if (found == hashes) {
        return std::nullopt;
    }
    const auto same = std::mismatch(found.begin(), found.end(), hashes.begin(), hashes.end());
    const auto index = static_cast<std::size_t>(same.first - found.begin()) / sync::k_piece_hash_bytes;
    return static_cast<std::int64_t>(index) * sync::k_media_piece_bytes;
}

}  // namespace

MediaIn::MediaIn(PeerShared& shared, const std::vector<SyncedLibrary>& libraries, Events events)
    : shared_(shared), libraries_(libraries), events_(std::move(events))
{
}

const SyncedLibrary* MediaIn::library(const std::string& uid) const
{
    const auto it = std::ranges::find(libraries_, uid, &SyncedLibrary::uid);
    return it == libraries_.end() ? nullptr : &*it;
}

template <typename Work, typename Then, typename Always>
void MediaIn::at_work(const Key& key, QThreadPool& pool, Work work, Then then, Always always)
{
    shared_.parts_at_work.insert(key);
    // Reports to what the link shares: the part is let go of, and another
    // connection may begin on it, even once this one is gone.
    run_off_thread(&shared_.context, pool, std::move(work),
                   [this, here = QPointer<QObject>(&context_), shared = &shared_, key,
                    then = std::move(then), always = std::move(always)](auto result) mutable {
                       shared->parts_at_work.erase(key);
                       always(result);
                       if (here) {
                           then(std::move(result));
                       }
                       // Not when it went on to more work.
                       if ((!here || !parts_.contains(key)) && !shared->parts_at_work.contains(key)
                           && shared->part_free) {
                           shared->part_free(key.first);
                       }
                   });
}

bool MediaIn::free_to_begin(const std::string& library, const std::string& path) const
{
    return !shared_.parts_at_work.contains({library, path});
}

std::int64_t MediaIn::begin(const SyncedLibrary& lib, const db::MediaDigest& digest)
{
    const auto start = [&](std::int64_t offset) {
        Part part;
        part.digest = digest;
        part.written = offset;
        part.received = offset;
        const auto known = shared_.parts_written.find({lib.uid, digest.path});
        part.resumed = offset > 0 && (known == shared_.parts_written.end() || known->second < offset);
        parts_[{lib.uid, digest.path}] = std::move(part);
        return offset;
    };
    const auto file = part_of(lib, digest.path);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0) {
        return start(0);
    }
    // The hashes kept for the file are those its part was checked against,
    // kept as they came: the part is of this file when they make its root.
    const auto kept = sync::PieceHashes(lib.pieces).load(digest.path, digest.size);
    if (!kept || sync::media_root(*kept) != digest.root) {
        std::filesystem::remove(file, ec);
        return start(0);
    }
    // From the start of a piece, and of the last at the latest: a part
    // whole but not yet in place gets its last piece again, and goes in
    // place once that has come.
    const auto offset = std::min(static_cast<std::int64_t>(size) / sync::k_media_piece_bytes,
                                 sync::piece_count(digest.size) - 1)
                        * sync::k_media_piece_bytes;
    if (static_cast<std::int64_t>(size) != offset) {
        std::filesystem::resize_file(file, static_cast<std::uintmax_t>(offset), ec);
        if (ec) {
            std::filesystem::remove(file, ec);
            return start(0);
        }
    }
    return start(offset);
}

bool MediaIn::expects_hashes(const std::string& library, const std::string& path) const
{
    const auto it = parts_.find({library, path});
    return it != parts_.end() && !it->second.hashes;
}

bool MediaIn::expects_bytes(const std::string& library, const std::string& path, std::int64_t offset) const
{
    const auto it = parts_.find({library, path});
    // Handed over, only the rest of the piece coming.
    return it != parts_.end() && it->second.hashes && !it->second.whole && offset == it->second.received
           && !(it->second.handing_over && it->second.piece.empty());
}

void MediaIn::take_hashes(const std::string& library, const std::string& path, const QByteArray& hashes)
{
    const Key key{library, path};
    const auto it = parts_.find(key);
    const auto* lib = this->library(library);
    if (it == parts_.end() || lib == nullptr || it->second.hashes) {
        return;
    }
    const auto& digest = it->second.digest;
    const std::string_view got(hashes.constData(), static_cast<std::size_t>(hashes.size()));
    if (static_cast<std::int64_t>(got.size())
            != sync::piece_count(digest.size) * static_cast<std::int64_t>(sync::k_piece_hash_bytes)
        || sync::media_root(got) != digest.root) {
        drop(key);
        events_.refused(library, path,
                        QStringLiteral("A media file offered was not the one declared, and was not taken: %1"));
        return;
    }
    // Kept before any piece is written: they tell whether the part is of
    // this file, should the fetch go on from it later. (Not kept, a later
    // fetch starts again from the start; a part gone on from was found by
    // the same hashes, kept already.)
    (void)sync::PieceHashes(lib->pieces).save(path, digest.size, got);
    it->second.hashes = std::make_shared<const std::string>(got);
}

void MediaIn::take_bytes(const std::string& library, const std::string& path, std::int64_t offset,
                         const QByteArray& bytes, bool last)
{
    const Key key{library, path};
    const auto it = parts_.find(key);
    const auto* lib = this->library(library);
    if (it == parts_.end() || lib == nullptr || !it->second.hashes || it->second.whole) {
        return;
    }
    auto& part = it->second;
    const auto size = part.digest.size;
    std::string_view rest(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (collecting_ && *collecting_ != key) {
        drop(key);
        events_.refused(library, path,
                        QStringLiteral("A media file arrived out of turn, before another's piece was "
                                       "done, and was dropped: %1"));
        return;
    }
    if (offset + static_cast<std::int64_t>(rest.size()) > size) {
        drop(key);
        events_.refused(library, path, QStringLiteral("A media file arrived longer than declared and was dropped: %1"));
        return;
    }
    // Each piece taken whole (an empty file's too), for the piece thread to
    // check and write.
    for (;;) {
        const auto start = part.received - static_cast<std::int64_t>(part.piece.size());
        const auto piece_end = std::min(size, start + sync::k_media_piece_bytes);
        if (part.piece.empty()) {
            part.piece.reserve(static_cast<std::size_t>(piece_end - start));
        }
        const auto take = std::min<std::size_t>(rest.size(), static_cast<std::size_t>(piece_end - part.received));
        part.piece.append(rest.substr(0, take));
        part.received += static_cast<std::int64_t>(take);
        rest.remove_prefix(take);
        if (part.received < piece_end) {
            collecting_ = key;
            break;
        }
        collecting_.reset();
        waiting_.push_back({key, part_of(*lib, path), std::make_shared<const std::string>(std::move(part.piece)),
                            hash_at(*part.hashes, start), start, !part.resumed});
        part.piece = {};
        if (part.received >= size) {
            part.whole = true;
            break;
        }
        if (rest.empty() || part.handing_over) {
            break;
        }
    }
    const bool short_of_it = last && !part.whole;
    write_next();
    if (short_of_it) {
        drop(key);
        events_.refused(library, path, QStringLiteral("A media file arrived shorter than declared and was dropped: %1"));
    }
}

void MediaIn::write_next()
{
    if (writing_ || waiting_.empty()) {
        return;
    }
    auto piece = std::move(waiting_.front());
    waiting_.pop_front();
    writing_ = true;
    const auto key = piece.key;
    const auto bytes = static_cast<std::int64_t>(piece.bytes->size());
    const auto end = piece.at + bytes;
    const bool fresh = piece.fresh;
    at_work(
        key, shared_.pieces,
        [piece = std::move(piece), bytes] {
            if (sync::piece_hash(*piece.bytes) != piece.hash) {
                return Written::Damaged;
            }
            std::error_code ec;
            std::filesystem::create_directories(piece.file.parent_path(), ec);
            QFile file(piece.file);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                return Written::Failed;
            }
            // Flushed and measured: a short write is known here, not by the
            // file going in place short.
            const bool good = file.size() == piece.at && file.write(piece.bytes->data(), bytes) == bytes
                              && file.flush() && file.size() == piece.at + bytes;
            if (!good && file.size() > piece.at) {
                (void)file.resize(piece.at);
            }
            return good ? Written::Good : Written::Failed;
        },
        [this, key, bytes](Written result) {
            writing_ = false;
            piece_written(key, result, bytes);
            write_next();
            if (has_room() && events_.room) {
                events_.room();
            }
        },
        [shared = &shared_, key, end, fresh](Written result) {
            if (fresh && result == Written::Good) {
                shared->parts_written[key] = end;
            }
        });
}

void MediaIn::piece_written(const Key& key, Written result, std::int64_t bytes)
{
    const auto it = parts_.find(key);
    if (it == parts_.end()) {
        return;  // let go of meanwhile
    }
    const auto& [library, path] = key;
    switch (result) {
    case Written::Damaged:
        // What came before is on the disk, checked: another computer goes
        // on from it.
        drop(key);
        events_.refused(library, path, QStringLiteral("A media file arrived damaged and was dropped: %1"));
        return;
    case Written::Failed:
        drop(key);
        events_.failed(library, path, QStringLiteral("A media file could not be written: %1"));
        return;
    case Written::Good:
        break;
    }
    it->second.written += bytes;
    const auto written = it->second.written;
    const auto size = it->second.digest.size;
    events_.progress(library, path, written);
    // What heard of it may have let go of the file.
    if (written >= size && parts_.contains(key)) {
        finish(key);
        return;
    }
    hand_over_if_done(key);
}

bool MediaIn::hand_over(const std::string& library, const std::string& path)
{
    const Key key{library, path};
    const auto it = parts_.find(key);
    if (it == parts_.end() || it->second.whole) {
        return false;
    }
    it->second.handing_over = true;
    // Nothing more to come of it, maybe nothing still to write: looked at once
    // whoever asked has done asking.
    QMetaObject::invokeMethod(&context_, [this, key] { hand_over_if_done(key); }, Qt::QueuedConnection);
    return true;
}

void MediaIn::hand_over_if_done(const Key& key)
{
    const auto it = parts_.find(key);
    if (it == parts_.end() || !it->second.handing_over || it->second.whole || !it->second.piece.empty()
        || shared_.parts_at_work.contains(key)
        || std::ranges::any_of(waiting_, [&](const WholePiece& piece) { return piece.key == key; })) {
        return;
    }
    drop(key);
    if (events_.handed_over) {
        events_.handed_over(key.first, key.second);
    }
}

void MediaIn::finish(const Key& key)
{
    const auto* lib = library(key.first);
    if (lib == nullptr) {
        drop(key);
        return;
    }
    const auto& path = key.second;
    const auto& part = parts_.at(key);
    // Pieces written over this connection were checked as they came; what
    // was on the disk from before is read through.
    const auto hashes = part.resumed ? part.hashes : nullptr;
    const auto size = part.digest.size;
    placing_.insert(key);
    shared_.parts_written.erase(key);
    at_work(
        key, shared_.checks,
        [file = part_of(*lib, path), whole = lib->media / path, kept = sync::PieceHashes(lib->pieces), path, size,
         hashes] {
            std::error_code ec;
            const auto found = std::filesystem::file_size(file, ec);
            if (ec) {
                return Placed::Failed;
            }
            if (hashes) {
                const auto actual = sync::hash_media_file(file);
                if (!actual) {
                    return Placed::Failed;
                }
                if (const auto bad = first_bad_piece(*actual, *hashes)) {
                    std::filesystem::resize_file(
                        file, static_cast<std::uintmax_t>(std::min(*bad, static_cast<std::int64_t>(found))), ec);
                    return Placed::Damaged;
                }
            } else if (static_cast<std::int64_t>(found) != size) {
                return Placed::Failed;
            }
            if (!util::move_file_durably(file, whole)) {
                return Placed::Failed;
            }
            // The file here, as it now is, is what they are the hashes of.
            kept.keep_for_now(path);
            return Placed::Good;
        },
        [this, key](Placed result) {
            placing_.erase(key);
            finished(key, result);
        },
        [](Placed) {});
    parts_.erase(key);
}

void MediaIn::finished(const Key& key, Placed result)
{
    const auto& [library, path] = key;
    switch (result) {
    case Placed::Good:
        cut_back_.erase(key);
        events_.done(library, path);
        return;
    case Placed::Damaged:
        if (cut_back_.insert(key).second) {
            events_.again(library, path,
                          QStringLiteral("What was on the disk of a media file was damaged, and is "
                                         "being fetched again: %1"));
            return;
        }
        break;
    case Placed::Failed:
        break;
    }
    events_.failed(library, path, QStringLiteral("A media file could not be put in place: %1"));
}

void MediaIn::drop(const Key& key)
{
    parts_.erase(key);
    std::erase_if(waiting_, [&](const WholePiece& piece) { return piece.key == key; });
    if (collecting_ == key) {
        collecting_.reset();
    }
}

void MediaIn::forget(const std::string& library, const std::string& path)
{
    drop({library, path});
}

std::map<std::string, Arriving> MediaIn::arriving(const std::string& library) const
{
    std::map<std::string, Arriving> out;
    for (const auto& [key, part] : parts_) {
        if (key.first == library) {
            out[key.second] = {part.written, part.digest.size};
        }
    }
    return out;
}

std::map<std::string, Flowing> MediaIn::flowing(const std::string& library) const
{
    std::map<std::string, Flowing> out;
    for (const auto& [key, part] : parts_) {
        if (key.first == library && part.hashes && !part.whole && !part.handing_over) {
            out[key.second] = {part.digest.size - part.received, !part.piece.empty()};
        }
    }
    return out;
}

bool MediaIn::expecting() const
{
    return std::ranges::any_of(parts_, [](const auto& entry) {
        const auto& part = entry.second;
        return part.hashes && !part.whole && !part.handing_over;
    });
}

void MediaIn::forget_library(const std::string& library)
{
    std::erase_if(shared_.parts_written, [&](const auto& entry) { return entry.first.first == library; });
    std::erase_if(parts_, [&](const auto& entry) { return entry.first.first == library; });
    std::erase_if(waiting_, [&](const WholePiece& piece) { return piece.key.first == library; });
    if (collecting_ && collecting_->first == library) {
        collecting_.reset();
    }
}

void MediaIn::forget_all()
{
    parts_.clear();
    waiting_.clear();
    collecting_.reset();
}

bool MediaIn::placing(const std::string& library, const std::string& path) const
{
    return placing_.contains({library, path});
}

bool MediaIn::has_room() const
{
    return waiting_.size() + (writing_ ? 1 : 0) < k_pieces_waiting;
}

}  // namespace seventhslide::link
