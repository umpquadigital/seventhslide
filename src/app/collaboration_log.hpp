#pragma once

// The open library's shared log, for the Collaboration window: the messages the
// computers that sync it leave each other, and a note of what each changed
// (``sync/shared_log.hpp``), oldest first, with the words people read. A row
// is an entry, or a run of one computer's notes that nothing came between on
// the same day, as one note of all it changed. ``SyncController`` owns it,
// points it at the open library's store (``follow``), gives it the member list
// (who is called what, with which picture), and has it read again when
// entries came (``refresh``); the store is read off the window's thread.
//
// It also knows whether a message from another computer waits to be read
// (``unread``): one is read if it was there when the window opened or came while
// it was open (``setShowing``), scrolled to or not. Which have been read is
// kept per computer, not synced (``collaboration_read.json`` in the data
// folder), so a message left while this computer was off still counts, and one
// that comes late, after a later one was read, does too.
//
// Seam: Qt Core (the model, a thread pool), the sync store, the file system.

#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/shared_log.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QThreadPool>
#include <QVariant>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace seventhslide::app {

/// ``picture`` as QML shows one (``Avatar.qml``): ``glyph`` and ``colour``,
/// or ``image`` (base64 PNG), or none of them for initials.
[[nodiscard]] QVariantMap picture_map(const sync::Picture& picture);

class CollaborationLog : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SyncController keeps the one log")

    /// The open library syncs: there is a log to show.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Which library's log this is, empty for none.
    Q_PROPERTY(QString library READ library NOTIFY availableChanged)
    /// This computer may post there: an editor of the library.
    Q_PROPERTY(bool canPost READ canPost NOTIFY availableChanged)
    /// A message from another computer has not been shown here yet.
    Q_PROPERTY(bool unread READ unread NOTIFY unreadChanged)

public:
    enum Roles {
        /// "machine/sequence", of the row's first entry.
        EntryIdRole = Qt::UserRole + 1,
        /// "message", "changes", or another a later SeventhSlide writes.
        KindRole,
        MachineRole,
        /// The writer as the list names it now.
        NameRole,
        /// ``picture_map``.
        PictureRole,
        /// Written by this computer.
        MineRole,
        /// "9:14 AM"; a run of notes, "9:14 AM – 10:02 AM".
        TimeTextRole,
        /// A message's text.
        TextRole,
        /// A note of changes in a line: "Jane's PC updated 1 song".
        SummaryRole,
        /// A note's records: each ``{glyph, title, detail}``, one a run
        /// changed more than once said once.
        ItemsRole,
        /// The first row of its day: ``DayRole`` heads it.
        ShowDayRole,
        /// "Today", "Yesterday", "Tuesday, November 3".
        DayRole,
        /// A message not following one of the same computer's a moment
        /// before: its picture and name head it.
        ShowSenderRole,
        /// A note shown open, by the operator.
        ExpandedRole,
    };
    Q_ENUM(Roles)

    /// Writes a message to the log (``SyncNode::post_message``); ``done`` gets
    /// why it could not, or nothing.
    using Post = std::function<void(std::string uid, std::string text, std::function<void(std::string)> done)>;

    /// ``data``: this computer's data folder, where how far it has read is kept.
    CollaborationLog(Post post, std::filesystem::path data, QObject* parent = nullptr);
    ~CollaborationLog() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool available() const noexcept { return !uid_.empty(); }
    [[nodiscard]] QString library() const { return QString::fromStdString(uid_); }
    [[nodiscard]] bool canPost() const noexcept { return can_post_; }
    [[nodiscard]] bool unread() const noexcept { return unread_; }

    /// Show library ``uid``'s log from ``store``, ``me`` this computer; an
    /// empty ``uid`` for none. The same library again keeps what is read.
    void follow(const std::string& uid, std::shared_ptr<sync::SyncStore> store, std::string me);
    /// Who is called what, with which picture, and whether this computer may
    /// post: from the library's member list as it stands.
    void set_members(const sync::Members& members);
    /// Read the entries that came since the last look.
    void refresh();

    /// Post ``text``: why it cannot be (empty, too long, a viewer), or empty
    /// when it goes. One that fails on its way says so with ``failed``.
    Q_INVOKABLE QString send(const QString& text);
    /// Show note ``entryId`` open, or not.
    Q_INVOKABLE void setExpanded(const QString& entryId, bool expanded);
    /// Whether the window shows the log now: what it holds, and what comes
    /// while it does, is read.
    Q_INVOKABLE void setShowing(bool showing);

    /// The words for a note of changes, by whom: "Jane's PC updated 1 song".
    [[nodiscard]] static QString summary(const QString& who, const sync::LogCounts& counts);
    /// A record a note names, as a row says it: ``glyph``, ``title`` and
    /// ``detail`` ("Lyrics, verse order").
    [[nodiscard]] static QVariantMap item_map(const sync::LogItem& item);
    /// A run of notes as one: each record once, as it ended up ("added" then
    /// "changed" is added; "changed" twice, both fields), and the counts of
    /// those and of the records the notes counted but did not name.
    [[nodiscard]] static sync::LogEntry merge_notes(std::span<const sync::LogEntry> notes);

signals:
    void availableChanged();
    void unreadChanged();
    /// A message could not be posted: why, in words.
    void failed(const QString& message);

private:
    /// A row: the entries ``first`` to ``last`` of ``entries_``.
    struct Row {
        std::size_t first = 0;
        std::size_t last = 0;
    };

    struct Who {
        QString name;
        sync::Picture picture;
        bool operator==(const Who&) const = default;
    };

    [[nodiscard]] Who who(const std::string& machine) const;
    [[nodiscard]] std::string entry_id(const sync::LogEntry& entry) const;
    /// ``entries_`` in rows.
    [[nodiscard]] std::vector<Row> rows_of() const;
    /// A row's entries as one: its only one, or its notes merged.
    [[nodiscard]] sync::LogEntry row_entry(const Row& row) const;
    /// Take ``entries`` read from the store, of reading ``generation``.
    void take(std::uint64_t generation, std::vector<sync::LogEntry> entries);
    /// ``entry`` is another computer's message, not read here.
    [[nodiscard]] bool is_unread(const sync::LogEntry& entry) const;
    /// Say whether anything is unread, now the entries or the marks changed;
    /// while the window shows the log, first count all of it read.
    void update_unread();

    Post post_;
    std::filesystem::path data_;
    std::string uid_;
    std::shared_ptr<sync::SyncStore> store_;
    std::string me_;
    bool can_post_ = false;
    std::map<std::string, Who> members_;
    /// Shared with the reading off the window's thread, one at a time.
    std::shared_ptr<sync::LogReader> reader_;
    std::vector<sync::LogEntry> entries_;
    std::vector<Row> rows_;
    /// The notes shown open, by entry id: a row is open if any of its are.
    std::set<std::string> expanded_;
    /// By library, then by writer: the numbers of its messages read here.
    std::map<std::string, std::map<std::string, std::set<std::int64_t>>> read_;
    bool showing_ = false;
    bool unread_ = false;
    /// Counts the libraries followed: a read for one before is let go of.
    std::uint64_t generation_ = 0;
    bool reading_ = false;
    bool read_again_ = false;
    QThreadPool pool_;
};

} // namespace seventhslide::app
