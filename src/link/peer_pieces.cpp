// Peer: what is announced ahead of a bytes frame, a media file's hashes or
// bytes or a piece of a change file, snapshot's index or part, and the
// pieces as they come.

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include <memory>
#include <utility>

namespace seventhslide::link {

void Peer::take_piece_announced(const SyncedLibrary& lib, const json& m)
{
    if (incoming_) {
        malformed("a piece announced before the last one's bytes");
    }
    const auto kind = store_file_kind(text_of(m, "kind"));
    if (!kind) {
        malformed("a piece of something that is not a change file, snapshot or part");
    }
    auto in = std::make_unique<Incoming>();
    in->library = lib.uid;
    in->kind = Incoming::Kind::StoreFile;
    in->file = *kind;
    in->offset = number_of(m, "offset");
    in->size = number_of(m, "size");
    if (in->file == StoreFile::Part) {
        in->name = part_name_of(m);
        in->path = part_item(in->name);
    } else {
        in->name = text_of(m, "machine");
        if (!sync::is_machine_id(in->name)) {
            malformed("a machine id that is not one");
        }
        in->number = number_of(m, "number");
        in->path = item(in->file, in->name, in->number);
    }
    if (in->size > sync::k_largest_store_file) {
        too_large(lib.uid, in->path, QStringLiteral("%1, %2 bytes").arg(QString::fromStdString(in->path)).arg(in->size));
        malformed("a file larger than any sent in pieces");
    }
    // Only a file asked for, of a size it may have: a part of the size its
    // index names.
    files_heard_.restart();
    auto& st = state_[lib.uid];
    in->accept = st.requested.contains(in->path) && (in->file == StoreFile::Part || in->number >= 1)
                 && in->offset >= 0 && in->size > 0;
    if (in->accept && in->file == StoreFile::Part && !size_as_named(lib.uid, in->name, in->size)) {
        in->accept = false;
    }
    if (!in->accept) {
        // Let go of meanwhile: what came of it is let go of too.
        st.in_pieces.erase(in->path);
    }
    // A connection sends one at a time, so one file is also the most it may
    // hold in part: what others hold here and this one whole.
    if (const auto held = in->accept ? parts_held_besides(lib.uid, in->path) : 0;
        held + in->size > sync::k_largest_store_file) {
        too_large(lib.uid, in->path, QStringLiteral("%1, %2 bytes, beside %3 bytes of others in pieces")
                               .arg(QString::fromStdString(in->path))
                               .arg(in->size)
                               .arg(held));
        malformed("more of files in pieces at once than one file");
    }
    incoming_ = std::move(in);
}

void Peer::too_large(const std::string& uid, const std::string& path, const QString& what)
{
    // A computer that sends it may send it again once reconnected: said once.
    if (shared_.said_once({machine_, uid, path})) {
        qCWarning(lc_link).noquote() << QStringLiteral("%1 sent more of library %2 at once than the %3 bytes a sync "
                                                       "file may be (%4), and is disconnected")
                                            .arg(QString::fromStdString(name_), QString::fromStdString(uid))
                                            .arg(sync::k_largest_store_file)
                                            .arg(what);
    }
}

std::int64_t Peer::parts_held_besides(const std::string& uid, const std::string& path) const
{
    std::int64_t held = 0;
    for (const auto& [library, st] : state_) {
        for (const auto& [name, part] : st.in_pieces) {
            if (library != uid || name != path) {
                held += static_cast<std::int64_t>(part.size());
            }
        }
    }
    return held;
}

void Peer::take_media_announced(const SyncedLibrary& lib, const json& m)
{
    if (incoming_) {
        malformed("a media file's bytes announced before the last one's");
    }
    auto in = std::make_unique<Incoming>();
    in->kind = text_of(m, "type") == "pieces" ? Incoming::Kind::Hashes : Incoming::Kind::Media;
    in->library = lib.uid;
    in->path = media_path_of(m);
    in->offset = in->kind == Incoming::Kind::Media ? number_of(m, "offset") : 0;
    const auto last = m.find("last");
    in->last = last != m.end() && last->is_boolean() && last->get<bool>();
    // Only a file asked for: its hashes once, then its bytes from where
    // what came ends. Anything else is let go by.
    in->accept = state_[lib.uid].requested.contains(item("media", in->path))
                 && (in->kind == Incoming::Kind::Hashes ? media_in_.expects_hashes(lib.uid, in->path)
                                                        : media_in_.expects_bytes(lib.uid, in->path, in->offset));
    incoming_ = std::move(in);
}

void Peer::handle_bytes(const QByteArray& bytes)
{
    if (!incoming_) {
        malformed("bytes without a media message");
    }
    const auto in = std::move(incoming_);
    const auto* lib = library(in->library);
    if (lib == nullptr) {
        return;
    }
    if (!in->accept) {
        return;
    }
    // A file let go of meanwhile (a read can end between a message and its
    // bytes) is let by there.
    switch (in->kind) {
    case Incoming::Kind::StoreFile:
        take_piece(*lib, *in, bytes);
        break;
    case Incoming::Kind::Hashes:
        media_in_.take_hashes(in->library, in->path, bytes);
        break;
    case Incoming::Kind::Media:
        media_bytes_in_ += bytes.size();
        media_in_.take_bytes(in->library, in->path, in->offset, bytes, in->last);
        break;
    }
}

void Peer::take_piece(const SyncedLibrary& lib, const Incoming& in, const QByteArray& bytes)
{
    if (!in.accept) {
        return;
    }
    auto& st = state_[lib.uid];
    auto& part = st.in_pieces[in.path];
    if (in.offset != static_cast<std::int64_t>(part.size()) || in.offset + bytes.size() > in.size) {
        // Not the next piece of a file of that size: dropped, and asked of
        // another computer.
        st.in_pieces.erase(in.path);
        emit refused(QStringLiteral("A sync file arrived out of order and was dropped: %1")
                         .arg(QString::fromStdString(in.path)));
        if (in.file == StoreFile::Part) {
            drop_waiting(lib.uid, in.name, false);
        } else {
            st.missing_there.insert(in.path);
        }
        got(lib.uid, in.path, Got::NotHere);
        return;
    }
    part.append(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    emit files_coming(lib.uid);
    if (static_cast<std::int64_t>(part.size()) < in.size) {
        return;
    }
    auto whole = std::move(part);
    st.in_pieces.erase(in.path);
    if (in.file == StoreFile::Part) {
        part_came(lib, in.name, std::move(whole));
    } else {
        take_file(lib, in.name, in.number, std::move(whole), in.file == StoreFile::Snapshot);
    }
}

}  // namespace seventhslide::link
