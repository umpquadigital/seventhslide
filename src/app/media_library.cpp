#include "media_library.hpp"

#include "entry_error.hpp"
#include "media_controller.hpp"

#include <seventhslide/util/leaf_filename.hpp>

#include <string>
#include <vector>

namespace seventhslide::app {

MediaLibrary::MediaLibrary(MediaController& media, db::MediaKind kind, QObject* parent)
    : FolderLibrary(parent)
    , media_(media)
    , kind_(kind)
{
}

QString MediaLibrary::deleteItem(const QString& filename)
try {
    return media_.delete_media_file(kind_, filename);
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

bool MediaLibrary::attached() const
{
    return media_.attached();
}

void MediaLibrary::rebuild()
{
    media_.rebuild_media_browser(kind_);
}

void MediaLibrary::announce()
{
    emit media_.changed();
}

QString MediaLibrary::delete_folder(std::int64_t folder_id)
{
    return media_.delete_folder(kind_, folder_id);
}

QString MediaLibrary::move_items(const QVariantList& items, std::optional<std::int64_t> folder)
{
    std::vector<std::string> filenames;
    for (const auto& item : items) {
        if (auto fn = util::leaf_filename(item.toString().toStdString()); !fn.empty()) {
            filenames.push_back(std::move(fn));
        }
    }
    media_.media_repo_->move_filenames_to_folder(kind_, filenames, folder);
    return {};
}

bool MediaLibrary::reorder_items(std::optional<std::int64_t> folder,
                                 const std::vector<std::int64_t>& ids)
{
    if (!folder) {
        return false;
    }
    media_.media_repo_->reorder_folder_items(kind_, *folder, ids);
    return true;
}

} // namespace seventhslide::app
