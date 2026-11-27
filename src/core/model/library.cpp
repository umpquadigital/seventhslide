#include "seventhslide/model/library.hpp"

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace seventhslide::model {

std::vector<FolderPath> folder_paths(const std::vector<Folder>& folders)
{
    std::unordered_map<std::int64_t, const Folder*> by_id;
    for (const auto& f : folders) {
        if (f.id) {
            by_id[*f.id] = &f;
        }
    }
    const auto path_of = [&by_id](std::int64_t id) {
        std::vector<const std::string*> parts;
        std::set<std::int64_t> seen;
        for (std::optional<std::int64_t> walk = id; walk && seen.insert(*walk).second;) {
            const auto it = by_id.find(*walk);
            if (it == by_id.end()) {
                break;
            }
            parts.push_back(&it->second->name);
            walk = it->second->parent_id;
        }
        std::string path;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!path.empty()) {
                path += " / ";
            }
            path += **it;
        }
        return path;
    };

    std::vector<FolderPath> out;
    out.reserve(folders.size());
    for (const auto& f : folders) {
        if (f.id) {
            out.push_back(FolderPath{*f.id, f.name, path_of(*f.id)});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const FolderPath& a, const FolderPath& b) { return a.path < b.path; });
    return out;
}

std::string normalize_bible_abbreviation(std::string_view raw)
{
    const std::string line = util::collapse_ws(raw);
    return util::trim(util::utf8_prefix(line, k_max_bible_abbreviation_bytes));
}

nlohmann::json freeform_item_data(const FreeformItem& item)
{
    nlohmann::json data{{"name", item.name.empty() ? "Freeform" : item.name}};
    if (item.document.is_object() && !item.document.empty()) {
        data["document"] = item.document;
    }
    return data;
}

}  // namespace seventhslide::model
