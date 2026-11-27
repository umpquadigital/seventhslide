#pragma once

// Import: a Freeform file (.ssff, .ssfb) or a service's package (.zip) into
// the open library, in two steps the Import window walks through.
//
//   1. ``readFile`` opens and checks the file whole on a worker thread, and
//      reports what it holds on ``fileRead``: its items, its outputs with the
//      outputs here each may go to and a first choice for each, its files
//      and fonts, and its service when it has a rundown. What was read is
//      kept until it is imported or let go of (``discardRead``).
//   2. ``importRead`` takes what the window chose. Each item's decks go to
//      the outputs chosen and are fitted to them (exchange::adapt_document).
//      On the worker, the files the items still name and the service's own
//      files are copied out of the file under staging names, and each that
//      the library already holds, byte for byte, is used as it is. Then, on
//      the GUI thread and in one transaction, the folders, the files, the
//      items and the service are made; a failure leaves none of them. Fonts
//      go to the font controller afterwards, only for slots the library has
//      no face in, and it waits as it does while something is live.
//
// Nothing the library already has is changed. Each request reports once,
// on ``fileRead`` or ``importFinished``; a library closed or switched while
// one is on the worker drops its result.
//
// SessionFacade owns one and exposes it as ``session.freeformImport``.

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace seventhslide::session {
class Session;
}

namespace seventhslide::exchange {
struct OpenedFile;
}

namespace seventhslide::app {

class FontController;
class FreeformLibraryController;
class LibraryController;
class MediaController;
class OpenLibrary;

class FreeformImportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns Import: session.freeformImport")

    /// A file is being read or imported.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 0 to 1 through the import under way.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
    FreeformImportController(session::Session& session,
                             OpenLibrary& open_library,
                             FreeformLibraryController& freeform,
                             MediaController& media,
                             FontController& fonts,
                             LibraryController& library,
                             QObject* parent = nullptr);
    ~FreeformImportController() override;

    FreeformImportController(const FreeformImportController&) = delete;
    FreeformImportController& operator=(const FreeformImportController&) = delete;
    FreeformImportController(FreeformImportController&&) = delete;
    FreeformImportController& operator=(FreeformImportController&&) = delete;

    /// Read the file at ``url`` (a path or file:// URL). ``fileRead`` reports
    /// it under the id returned: ``{error}`` when it cannot be imported, else
    /// what it holds (see the .cpp's ``summary``).
    Q_INVOKABLE qint64 readFile(const QString& url);
    /// Import what was read, as chosen: ``{items: [index...], mapping:
    /// [outputId or "" per file output], folderName, addService,
    /// addItemsToLibrary}``. ``importFinished`` reports it: ``{ok, error,
    /// items, files, fonts, serviceId, notes}``.
    Q_INVOKABLE qint64 importRead(const QVariantMap& choices);
    /// Let go of what was read.
    Q_INVOKABLE void discardRead();
    /// Stop the read or import under way: it reports as stopped and leaves
    /// nothing behind.
    Q_INVOKABLE void cancel();
    /// The file name patterns of Freeform files (``"*.ssff *.ssfb"``) and of
    /// service packages (``"*.zip"``), for a file dialog's filters.
    Q_INVOKABLE [[nodiscard]] QString freeformFilePatterns() const;
    Q_INVOKABLE [[nodiscard]] QString packageFilePatterns() const;

    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] double progress() const noexcept { return progress_; }

    /// The library is closing: what was read goes, and the work under way is
    /// stopped and its result dropped.
    void library_closing();

    // What an import is made of, on its way between the GUI thread and the
    // worker (the .cpp's): public only so the .cpp's helpers can name them.
    struct Need;
    struct Got;
    struct Extracted;

signals:
    void fileRead(qint64 requestId, const QVariantMap& summary);
    void importFinished(qint64 requestId, const QVariantMap& result);
    void busyChanged();
    void progressChanged();

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    [[nodiscard]] qint64 refuse_read(const QString& why);
    [[nodiscard]] qint64 refuse_import(const QString& why);
    void set_busy(bool busy);
    void set_progress(double progress);
    /// What the window shows of ``read_``.
    [[nodiscard]] QVariantMap summary() const;
    /// Why the open library cannot take an import, or empty.
    [[nodiscard]] QString why_not_here() const;
    /// The open library's identity, to know a result is still its.
    [[nodiscard]] std::string library_key() const;
    void commit(qint64 id, const std::shared_ptr<Extracted>& extracted, const CancelFlag& cancelled);

    session::Session& session_;
    OpenLibrary& open_library_;
    FreeformLibraryController& freeform_;
    MediaController& media_;
    FontController& fonts_;
    LibraryController& library_;

    std::shared_ptr<exchange::OpenedFile> read_;
    std::string read_name_;
    std::string read_library_;
    /// Each item's preview as a ``data:`` URL, made on the worker.
    QStringList read_previews_;
    qint64 last_request_id_ = 0;
    bool busy_ = false;
    double progress_ = 0.0;
    CancelFlag cancelled_ = std::make_shared<std::atomic<bool>>(false);
    QThreadPool pool_;
};

}  // namespace seventhslide::app
