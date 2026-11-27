#pragma once

// Computers syncing a library over the link on this computer's loopback
// address, for the link and server tests: each its library, store, engine
// and link.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/link/link.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <QHostAddress>
#include <QtLogging>
#include <QSqlQuery>
#include <QString>
#include <QTcpServer>
#include <QThread>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::test::link_fixture {

using seventhslide::crypto::SigningKey;
using seventhslide::db::Database;
using seventhslide::link::Link;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Identity;
using seventhslide::sync::MemberLog;
using seventhslide::sync::Numbered;
using seventhslide::sync::Role;
using seventhslide::sync::SyncEngine;

inline constexpr const char* k_passphrase = "lilies of the field";
inline constexpr const char* k_expires = "2999-01-01T00:00:00Z";
inline constexpr auto k_quick = seventhslide::crypto::k_box_params_quick;

inline Database new_library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

/// Whether this computer can listen at ``address`` on its loopback: Linux
/// and Windows at any of 127.0.0.0/8, macOS only at the ones added to lo0
/// (``sudo ifconfig lo0 alias 127.0.0.2 up``; the macOS CI job adds them).
inline bool loopback_has(const QString& address)
{
    QTcpServer probe;
    return probe.listen(QHostAddress(address));
}

/// A computer: its library, store, engine and link, listening on loopback.
struct Computer {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir root;
    std::filesystem::path media{root.path / "media"};
    std::filesystem::path pieces{root.path / "pieces"};
    /// Shared with the link, which holds it while it works on it.
    std::shared_ptr<FolderStore> shared_store{std::make_shared<FolderStore>(root.path / "sync")};
    FolderStore& store{*shared_store};
    Identity me;
    Database db;
    SyncEngine engine;
    Link link;
    std::string uid;
    std::vector<QString> closed;
    std::vector<QString> refused;
    /// The room the library's size limit leaves; -1 for no limit.
    std::int64_t room = -1;

    Computer(const std::string& machine, const std::string& name, const QHostAddress& listen_on = QHostAddress::LocalHost)
        : me{machine, name, SigningKey::generate()}, db(new_library()),
          engine(db, store, media, me), link(me)
    {
        engine.set_piece_hashes(pieces);
        REQUIRE(link.listen(listen_on));
        QObject::connect(&link, &Link::peer_closed,
                         [this](seventhslide::link::Peer*, const QString& why) { closed.push_back(why); });
        QObject::connect(&link, &Link::refused, [this](const QString& why) { refused.push_back(why); });
    }
    Computer(const Computer&) = delete;
    Computer& operator=(const Computer&) = delete;

    /// Sync ``library`` over the link.
    void share(const std::string& library)
    {
        uid = library;
        link.add_library({uid, shared_store, media, pieces, [this] { return missing_media(engine); },
                          [this] { return room; },
                          [this] { return seventhslide::db::SyncBookkeeping(db).applied(); }});
    }
    const seventhslide::crypto::PublicKey& key() const { return me.key.public_key(); }
    quint16 port() const { return link.port(); }
    MemberLog members() { return {store, me, uid}; }
    /// This computer's own join entry, which it offers a library with.
    [[nodiscard]] std::string join_entry()
    {
        const auto own = seventhslide::sync::own_invite(store, uid, me.machine);
        REQUIRE(own.has_value());
        return own->entry;
    }
    seventhslide::sync::Members list() { return seventhslide::sync::read_members(store, uid); }

    /// This computer's engine runs, and the link hears of what it wrote.
    seventhslide::sync::SyncResult sync()
    {
        auto result = engine.sync();
        link.library_changed(uid);
        return result;
    }
    std::string text(const std::string& sql)
    {
        QSqlQuery q(db.handle());
        REQUIRE(q.exec(QString::fromStdString(sql)));
        REQUIRE(q.next());
        return q.value(0).toString().toStdString();
    }
    void put_file(const std::string& path, const std::string& bytes)
    {
        std::filesystem::create_directories((media / path).parent_path());
        std::ofstream(media / path, std::ios::binary) << bytes;
    }
    std::string file(const std::string& path)
    {
        std::ifstream in(media / path, std::ios::binary);
        // On Windows a file just put in place can refuse to open for a moment
        // ("permission denied", a sharing violation): a virus scanner reads
        // it once the handle that wrote it closes. Wait that out.
        for (int tries = 0; !in && tries < 40 && std::filesystem::exists(media / path); ++tries) {
            QThread::msleep(50);
            in.clear();
            in.open(media / path, std::ios::binary);
        }
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    bool has_file(const std::string& path) { return std::filesystem::exists(media / path); }
};

using seventhslide::test::run_events_for;
using seventhslide::test::settle_while;

/// ``settle_while`` any of ``computers``' links has anything on its way.
inline bool settle(std::initializer_list<Computer*> computers, int ms = 20000)
{
    const std::vector<Computer*> all(computers);
    return settle_while(
        [&all] { return std::ranges::any_of(all, [](Computer* c) { return c->link.busy(); }); }, ms);
}

/// The warnings logged while it lives, from any thread. One at a time.
class LoggedWarnings {
public:
    LoggedWarnings()
    {
        lines().clear();
        previous() = qInstallMessageHandler(&take);
    }
    ~LoggedWarnings() { qInstallMessageHandler(previous()); }
    LoggedWarnings(const LoggedWarnings&) = delete;
    LoggedWarnings& operator=(const LoggedWarnings&) = delete;

    /// How many hold every one of ``parts``.
    [[nodiscard]] std::size_t count(std::initializer_list<std::string_view> parts) const
    {
        const std::lock_guard lock(mutex());
        return static_cast<std::size_t>(std::ranges::count_if(lines(), [&](const std::string& line) {
            return std::ranges::all_of(parts, [&](std::string_view p) { return line.find(p) != std::string::npos; });
        }));
    }
    [[nodiscard]] std::string all() const
    {
        const std::lock_guard lock(mutex());
        std::string out;
        for (const auto& line : lines()) {
            out += line + "\n";
        }
        return out;
    }

private:
    static void take(QtMsgType type, const QMessageLogContext& context, const QString& text)
    {
        if (type == QtWarningMsg) {
            const std::lock_guard lock(mutex());
            lines().push_back(text.toStdString());
        }
        if (previous() != nullptr) {
            previous()(type, context, text);
        }
    }
    static std::vector<std::string>& lines()
    {
        static std::vector<std::string> l;
        return l;
    }
    static std::mutex& mutex()
    {
        static std::mutex m;
        return m;
    }
    static QtMessageHandler& previous()
    {
        static QtMessageHandler h = nullptr;
        return h;
    }
};

/// Why ``c``'s connections closed, for a failing test to say.
inline std::string why(const Computer& c)
{
    std::string out;
    for (const auto& w : c.closed) {
        out += (out.empty() ? "" : " | ") + w.toStdString();
    }
    return out;
}

/// A library put online on ``church``: founded, shared, its first file sent.
inline MemberLog::Founded put_online(Computer& church)
{
    seventhslide::test::fill_library(church.db);
    church.put_file("images/ab12.png", "lilies");
    church.put_file("videos/cd34.mp4", "welcome");
    church.put_file("audio/ef56.mp3", "prelude");
    const auto uid = seventhslide::db::library_uid(church.db);
    church.share(uid);
    auto founded = church.members().found(k_passphrase, k_quick);
    church.engine.share_everything();
    REQUIRE(church.sync().changes_sent > 0);
    return founded;
}

/// ``joiner`` joins ``uid`` through the computer at ``port``, on an invite.
inline void join_through(Computer& joiner, const std::string& uid, const std::string& code,
                  const seventhslide::crypto::PublicKey& key, quint16 port)
{
    joiner.share(uid);
    joiner.members().join(seventhslide::sync::read_invite(code));
    joiner.link.connect_to("127.0.0.1", port, key, {{uid, joiner.join_entry()}});
}

}  // namespace seventhslide::test::link_fixture
