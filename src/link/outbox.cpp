#include "seventhslide/link/outbox.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/link/frames.hpp"
#include "seventhslide/link/off_thread.hpp"
#include "seventhslide/link/peer_shared.hpp"
#include "seventhslide/sync/media_pieces.hpp"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

namespace seventhslide::link {
namespace {

Q_LOGGING_CATEGORY(lc_outbox, "seventhslide.link", QtWarningMsg)

using nlohmann::json;

/// What may wait to go out on the network: little enough that a slow
/// connection holds little in memory, enough to keep a fast one busy.
constexpr qint64 k_write_ahead = 2 * 1024 * 1024;

// A frame never holds the end of one piece and the start of the next: each
// piece is checked as its last frame comes.
static_assert(sync::k_media_piece_bytes % k_media_piece == 0);
/// The largest media file sent: its pieces' hashes fill one bytes frame
/// (128 GiB).
constexpr std::int64_t k_max_media_pieces =
    k_max_bytes_frame / static_cast<std::int64_t>(sync::k_piece_hash_bytes);

/// ``message`` with what names the file: its machine and number, as a
/// ``missing`` or a whole file says them (``pieces``: as a ``piece`` does),
/// or a part's SHA-256.
json named(json message, StoreFile kind, const std::string& name, std::int64_t number, bool pieces = false)
{
    if (kind == StoreFile::Part) {
        message["sha256"] = name;
        return message;
    }
    message["machine"] = name;
    message[pieces ? "number" : kind == StoreFile::Snapshot ? "files" : "sequence"] = number;
    return message;
}

/// What names a store file in the log: ``change/<machine>/<number>``,
/// ``snapshot/<machine>/<files>`` or ``part/<sha256>``.
QString file_name(StoreFile kind, const std::string& name, std::int64_t number)
{
    auto out =
        QStringLiteral("%1/%2").arg(QString::fromUtf8(store_file_name(kind)), QString::fromStdString(name));
    return kind == StoreFile::Part ? out : out + QStringLiteral("/%1").arg(number);
}

}  // namespace

Outbox::Outbox(QSslSocket& socket, PeerShared& shared, const std::vector<SyncedLibrary>& libraries)
    : socket_(socket), shared_(shared), libraries_(libraries)
{
    said_.start();
    wake_.setSingleShot(true);
    QObject::connect(&wake_, &QTimer::timeout, &context_, [this] { pump(); });
}

const SyncedLibrary* Outbox::library(const std::string& uid) const
{
    const auto it = std::ranges::find(libraries_, uid, &SyncedLibrary::uid);
    return it == libraries_.end() ? nullptr : &*it;
}

void Outbox::message(const json& message)
{
    messages_.push_back(encode_message(message));
    said_.restart();
    pump();
}

void Outbox::file(std::string library, StoreFile kind, std::string machine, std::int64_t number)
{
    file_jobs_.push_back({std::move(library), kind, std::move(machine), number, nullptr, 0});
}

void Outbox::part(std::string library, std::string sha256)
{
    part_jobs_.push_back({std::move(library), StoreFile::Part, std::move(sha256), 0, nullptr, 0});
}

void Outbox::too_large(const std::string& library, const QString& file, std::int64_t size)
{
    // Asked for again at every sync: said once.
    if (!shared_.said_once({"", library, file.toStdString()})) {
        return;
    }
    const auto most = file.startsWith(QStringLiteral("media/")) ? k_max_media_pieces * sync::k_media_piece_bytes
                                                                : sync::k_largest_store_file;
    qCWarning(lc_outbox).noquote() << QStringLiteral("%1 of library %2 is not sent: it is %3 bytes, and the most is %4")
                                          .arg(file, QString::fromStdString(library))
                                          .arg(size)
                                          .arg(most);
}

void Outbox::missing_media(const std::string& library, const std::string& path)
{
    message({{"type", "missing"}, {"library", library}, {"kind", "media"}, {"path", path}});
}

void Outbox::media(const SyncedLibrary& lib, const std::string& path, std::int64_t offset)
{
    const auto key = lib.uid + "/" + path;
    const QFileInfo info(QString::fromStdString((lib.media / path).string()));
    // From the start of a piece, as a fetch goes on; a file too large for
    // its hashes to go in one frame is not sent.
    if (!info.isFile() || offset < 0 || offset > info.size() || offset % sync::k_media_piece_bytes != 0
        || sync::piece_count(info.size()) > k_max_media_pieces) {
        if (info.isFile() && sync::piece_count(info.size()) > k_max_media_pieces) {
            too_large(lib.uid, QStringLiteral("media/%1").arg(QString::fromStdString(path)), info.size());
        }
        missing_media(lib.uid, path);
        return;
    }
    const auto ask = ++asks_;
    hashing_[key] = ask;
    run_off_thread(
        &context_, shared_.checks,
        [pieces = sync::PieceHashes(lib.pieces), path, file = lib.media / path] { return pieces.of(path, file); },
        [this, uid = lib.uid, path, key, ask, offset, size = info.size()](std::optional<std::string> hashes) {
            // Not wanted any more: the connection paused, the library was
            // let go of, or the other end said it has enough (and may have
            // asked again since, from elsewhere in the file).
            const auto it = hashing_.find(key);
            if (it == hashing_.end() || it->second != ask) {
                return;
            }
            hashing_.erase(it);
            if (closed_) {
                return;
            }
            if (!hashes || static_cast<std::int64_t>(hashes->size())
                               != sync::piece_count(size) * static_cast<std::int64_t>(sync::k_piece_hash_bytes)) {
                missing_media(uid, path);
                return;
            }
            // The hashes go ahead of the bytes: messages are written first.
            messages_.push_back(encode_message({{"type", "pieces"}, {"library", uid}, {"path", path}}));
            messages_.push_back(encode_bytes(QByteArray::fromStdString(*hashes)));
            media_jobs_.push_back({uid, path, offset, size});
            said_.restart();
            pump();
        });
}

void Outbox::cancel_media(const std::string& library, const std::string& path)
{
    hashing_.erase(library + "/" + path);
    std::erase_if(media_jobs_, [&](const MediaJob& job) { return job.library == library && job.path == path; });
}

void Outbox::drop_library(const std::string& library)
{
    std::erase_if(media_jobs_, [&](const MediaJob& job) { return job.library == library; });
    std::erase_if(hashing_, [&](const auto& entry) { return entry.first.starts_with(library + "/"); });
}

void Outbox::drop_files()
{
    file_jobs_.clear();
    part_jobs_.clear();
    media_jobs_.clear();
    hashing_.clear();
}

qint64 Outbox::unsent() const
{
    // A TLS socket's ``bytesToWrite`` counts only what is not encrypted yet:
    // it is encrypted at once into the connection's own buffer, which takes
    // anything. Held to the write-ahead by both, a whole video is never read
    // into memory faster than the network takes it.
    return socket_.bytesToWrite() + socket_.encryptedBytesToWrite();
}

bool Outbox::busy() const
{
    return !messages_.empty() || !file_jobs_.empty() || !part_jobs_.empty() || !media_jobs_.empty()
           || !hashing_.empty() || unsent() > 0;
}

bool Outbox::sending(const std::string& library) const
{
    const auto of_it = [&](const auto& job) { return job.library == library; };
    return std::ranges::any_of(file_jobs_, of_it) || std::ranges::any_of(part_jobs_, of_it)
           || std::ranges::any_of(media_jobs_, of_it)
           || std::ranges::any_of(hashing_, [&](const auto& entry) { return entry.first.starts_with(library + "/"); });
}

std::set<std::string> Outbox::media_going(const std::string& library) const
{
    std::set<std::string> out;
    for (const auto& job : media_jobs_) {
        if (job.library == library) {
            out.insert(job.path);
        }
    }
    const auto prefix = library + "/";
    for (const auto& [key, ask] : hashing_) {
        if (key.starts_with(prefix)) {
            out.insert(key.substr(prefix.size()));
        }
    }
    return out;
}

void Outbox::pump()
{
    if (closed_ || !socket_.isEncrypted()) {
        return;
    }
    while (unsent() < k_write_ahead) {
        if (!messages_.empty()) {
            socket_.write(messages_.front());
            messages_.pop_front();
            continue;
        }
        // Change files and snapshots' indexes, then parts, before media: a
        // song edit is never held up by a snapshot, nor a snapshot by a
        // video. While one is read from the store, media goes.
        if (auto* queue = file_queue()) {
            if (queue->front().text) {
                write_file_job(*queue);
                said_.restart();
                continue;
            }
            read_file_job(*queue);
        }
        if (!write_media_job()) {
            break;
        }
    }
}

bool Outbox::write_media_job()
{
    // Held to the cap, a frame of what it lets go in a moment, from where the
    // job is to the end of its piece at most.
    const bool capped = shared_.upload.rate() > 0 && (!capped_ || capped_());
    const auto frame = capped ? shared_.upload.frame(k_media_piece) : k_media_piece;
    const auto next_frame = [frame](const MediaJob& job) {
        const auto piece_end = (job.offset / sync::k_media_piece_bytes + 1) * sync::k_media_piece_bytes;
        return std::min({frame, job.size - job.offset, piece_end - job.offset});
    };
    while (!media_jobs_.empty()) {
        if (capped && !shared_.upload.take(next_frame(media_jobs_.front()))) {
            if (!wake_.isActive()) {
                wake_.start(static_cast<int>(std::max<std::int64_t>(1, shared_.upload.wait().count())));
            }
            return false;
        }
        MediaJob job = media_jobs_.front();
        media_jobs_.pop_front();
        const auto* lib = library(job.library);
        QFile file(lib ? QString::fromStdString((lib->media / job.path).string()) : QString());
        if (lib == nullptr || !file.open(QIODevice::ReadOnly) || !file.seek(job.offset)) {
            messages_.push_back(encode_message(
                {{"type", "missing"}, {"library", job.library}, {"kind", "media"}, {"path", job.path}}));
            return true;  // the message goes next
        }
        const QByteArray piece = file.read(next_frame(job));
        if (piece.isEmpty() && job.offset < job.size) {
            // Shorter than it was when asked for.
            messages_.push_back(encode_message(
                {{"type", "missing"}, {"library", job.library}, {"kind", "media"}, {"path", job.path}}));
            return true;
        }
        const bool last = job.offset + piece.size() >= job.size;
        socket_.write(encode_message({{"type", "media"},
                                      {"library", job.library},
                                      {"path", job.path},
                                      {"offset", job.offset},
                                      {"size", job.size},
                                      {"last", last}}));
        socket_.write(encode_bytes(piece));
        said_.restart();
        job.offset += piece.size();
        // The others get their turn between pieces of the file, not within
        // one: the other end holds a piece in memory until it is whole.
        if (!last) {
            if (job.offset % sync::k_media_piece_bytes == 0) {
                media_jobs_.push_back(job);
            } else {
                media_jobs_.push_front(job);
            }
        }
        return true;
    }
    return false;
}

std::deque<Outbox::FileJob>* Outbox::file_queue()
{
    // One begun in pieces goes on: the other end holds it in part meanwhile.
    // Then one read already, held back by the write-ahead: one file in memory
    // at a time (``PeerShared::files``).
    for (auto* queue : {&file_jobs_, &part_jobs_}) {
        if (!queue->empty() && queue->front().offset > 0) {
            return queue;
        }
    }
    for (auto* queue : {&file_jobs_, &part_jobs_}) {
        if (!queue->empty() && queue->front().text) {
            return queue;
        }
    }
    for (auto* queue : {&file_jobs_, &part_jobs_}) {
        if (!queue->empty()) {
            return queue;
        }
    }
    return nullptr;
}

void Outbox::read_file_job(std::deque<FileJob>& queue)
{
    if (reading_file_) {
        return;
    }
    const auto& job = queue.front();
    const auto* lib = library(job.library);
    if (lib == nullptr) {
        queue.pop_front();  // not synced here now: nothing more about it
        return;
    }
    reading_file_ = true;
    run_off_thread(
        &context_, shared_.files,
        [store = lib->store, kind = job.kind, name = job.name, number = job.number]()
            -> std::shared_ptr<const std::string> {
            try {
                switch (kind) {
                case StoreFile::Change:
                    return std::make_shared<const std::string>(store->read(sync::Numbered::Changes, name, number));
                case StoreFile::Snapshot:
                    return std::make_shared<const std::string>(store->read_snapshot({name, number}));
                case StoreFile::Part:
                    break;
                }
                auto text = std::make_shared<const std::string>(store->read_part(name));
                if (crypto::sha256_hex(*text) != name) {
                    store->set_aside_part(name);  // damaged here: not to be blamed on this computer
                    return nullptr;
                }
                return text;
            } catch (const std::exception&) {
                return nullptr;
            }
        },
        [this, job, &queue](std::shared_ptr<const std::string> text) {
            reading_file_ = false;
            // Still the one to send (a pause drops them all).
            if (!queue.empty() && !queue.front().text && queue.front().same(job)) {
                // One larger than any computer takes is not sent.
                if (text && static_cast<std::int64_t>(text->size()) <= sync::k_largest_store_file) {
                    queue.front().text = std::move(text);
                } else {
                    if (text) {
                        too_large(job.library, file_name(job.kind, job.name, job.number),
                                  static_cast<std::int64_t>(text->size()));
                    }
                    messages_.push_back(encode_message(
                        named({{"type", "missing"}, {"library", job.library}, {"kind", store_file_name(job.kind)}},
                              job.kind, job.name, job.number)));
                    queue.pop_front();
                }
            }
            pump();
        });
}

void Outbox::write_file_job(std::deque<FileJob>& queue)
{
    auto& job = queue.front();
    const auto size = static_cast<std::int64_t>(job.text->size());
    // Whole when small.
    if (size <= k_inline_file) {
        auto message =
            named({{"type", store_file_name(job.kind)}, {"library", job.library}}, job.kind, job.name, job.number);
        message["text"] = *job.text;
        socket_.write(encode_message(message));
        queue.pop_front();
        return;
    }
    const auto n = std::min<std::int64_t>(k_max_bytes_frame, size - job.offset);
    socket_.write(encode_message(named({{"type", "piece"},
                                        {"library", job.library},
                                        {"kind", store_file_name(job.kind)},
                                        {"offset", job.offset},
                                        {"size", size}},
                                       job.kind, job.name, job.number, true)));
    socket_.write(encode_bytes(QByteArray(job.text->data() + job.offset, static_cast<qsizetype>(n))));
    job.offset += n;
    if (job.offset >= size) {
        queue.pop_front();
    }
}

}  // namespace seventhslide::link
