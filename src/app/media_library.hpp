#pragma once

// One media kind's library tab (images, videos or audio): the folder calls
// every library has (FolderLibrary), over that kind's folders, with its files
// keyed by name.
//
// MediaController owns one per kind (``session.media.library(kind)``) and
// keeps the work only it can do: the files themselves, their thumbnails, and
// what the services still play.

#include "folder_library.hpp"

#include <seventhslide/db/media_kind.hpp>

#include <QtQml/qqmlregistration.h>
#include <QString>

namespace seventhslide::app {

class MediaController;

class MediaLibrary final : public FolderLibrary {
    Q_OBJECT
    QML_ANONYMOUS

public:
    MediaLibrary(MediaController& media, db::MediaKind kind, QObject* parent);

    [[nodiscard]] db::MediaKind kind() const noexcept { return kind_; }

    /// Take file ``filename`` out of the library, as
    /// ``MediaController::delete_media_file`` describes. Empty on success,
    /// else why not.
    Q_INVOKABLE QString deleteItem(const QString& filename);

protected:
    [[nodiscard]] bool attached() const override;
    void rebuild() override;
    /// Through ``session.media``, so what binds to it hears too.
    void announce() override;
    /// With the files in it, as ``MediaController::delete_folder`` describes.
    QString delete_folder(std::int64_t folder_id) override;
    /// ``items`` are filenames. A file leaves every folder it was in.
    QString move_items(const QVariantList& items, std::optional<std::int64_t> folder) override;
    /// ``ids`` are the folder's file rows; the root's files have no order of
    /// their own.
    bool reorder_items(std::optional<std::int64_t> folder,
                       const std::vector<std::int64_t>& ids) override;

private:
    MediaController& media_;
    db::MediaKind kind_;
};

} // namespace seventhslide::app
