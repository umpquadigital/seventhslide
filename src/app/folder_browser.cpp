#include "folder_browser.hpp"

#include "refusals.hpp"
#include "qml_ids.hpp"
#include "qvariant_json.hpp"

#include <QVariantMap>

#include <utility>

namespace seventhslide::app {

void FolderBrowser::attach(db::FolderTree tree)
{
    // emplace: a FolderTree holds a reference, so it cannot be assigned.
    tree_.emplace(std::move(tree));
    folder_.reset();
    search_.clear();
}

void FolderBrowser::detach()
{
    tree_.reset();
    folder_.reset();
    search_.clear();
}

bool FolderBrowser::contains(std::int64_t id) const
{
    return tree_ && id >= 0 && tree_->get(id).has_value();
}

bool FolderBrowser::enter(qint64 folder_id)
{
    const auto folder = optional_id(folder_id);
    if (folder && !contains(*folder)) {
        return false;
    }
    folder_ = folder;
    return true;
}

bool FolderBrowser::up()
{
    if (!folder_) {
        return false;
    }
    folder_ = optional_id(parent_id());
    return true;
}

bool FolderBrowser::set_search(const QString& text)
{
    if (search_ == text) {
        return false;
    }
    search_ = text;
    return true;
}

bool FolderBrowser::drop_missing()
{
    if (folder_ && !contains(*folder_)) {
        folder_.reset();
        return true;
    }
    return false;
}

qint64 FolderBrowser::parent_id() const
{
    if (!folder_ || !tree_) {
        return -1;
    }
    const auto folder = tree_->get(*folder_);
    return folder ? qml_id(folder->parent_id) : -1;
}

qint64 FolderBrowser::create(const QString& name)
{
    const auto trimmed = name.trimmed();
    if (!tree_ || trimmed.isEmpty()) {
        return -1;
    }
    return static_cast<qint64>(tree_->create(trimmed.toStdString(), folder_));
}

QString FolderBrowser::rename(qint64 folder_id, const QString& name)
{
    if (!tree_) {
        return refusal::no_library();
    }
    if (folder_id < 0) {
        return refusal::folder_gone();
    }
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return refusal::name_required();
    }
    (void)tree_->rename(folder_id, trimmed.toStdString());
    return {};
}

bool FolderBrowser::move(qint64 folder_id, qint64 parent_id)
{
    return tree_ && folder_id >= 0 && tree_->move(folder_id, optional_id(parent_id));
}

bool FolderBrowser::reorder(qint64 parent_id, const QVariantList& ordered_ids)
{
    if (!tree_) {
        return false;
    }
    const auto ids = parse_id_list(ordered_ids);
    if (!ids) {
        return false;
    }
    tree_->reorder_children(optional_id(parent_id), *ids);
    return true;
}

QVariantList FolderBrowser::rows() const
{
    QVariantList out;
    if (!tree_) {
        return out;
    }
    for (const auto& f : model::folder_paths(tree_->list())) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), static_cast<qint64>(f.id));
        row.insert(QStringLiteral("name"), QString::fromStdString(f.name));
        row.insert(QStringLiteral("path"), QString::fromStdString(f.path));
        out.push_back(row);
    }
    return out;
}

std::vector<model::Folder> FolderBrowser::children() const
{
    return tree_ ? tree_->children(folder_) : std::vector<model::Folder>{};
}

std::vector<MediaBrowserEntry> FolderBrowser::folder_entries(
    const std::vector<model::Folder>& folders,
    const std::unordered_map<std::int64_t, int>& counts) const
{
    std::vector<MediaBrowserEntry> entries;
    if (folder_ && query().isEmpty()) {
        MediaBrowserEntry parent;
        parent.kind = MediaBrowserEntry::Kind::Parent;
        parent.name = "..";
        entries.push_back(std::move(parent));
    }
    for (const auto& folder : folders) {
        if (!folder.id) {
            continue;
        }
        MediaBrowserEntry e;
        e.kind = MediaBrowserEntry::Kind::Folder;
        e.id = *folder.id;
        e.name = folder.name;
        if (const auto it = counts.find(*folder.id); it != counts.end()) {
            e.count = it->second;
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

}  // namespace seventhslide::app
