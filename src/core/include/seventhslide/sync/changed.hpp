#pragma once

// Which records a sync's changes from other computers went into, by table:
// what a window showing the library reads again, and no more. A record is
// named by its key in the change files (``change_file.hpp``): a song's uid,
// an app setting's key. A catch-up from a snapshot rewrites every table, so
// all of each may have changed.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::sync {

class Changed {
public:
    /// Record ``key`` of ``table``.
    void add(std::string_view table, std::string_view key)
    {
        auto& keys = tables_.try_emplace(std::string(table), std::set<std::string>{}).first->second;
        if (keys) {
            keys->emplace(key);  // not when all of it is already
        }
    }

    /// All of ``table``.
    void add_all(std::string_view table) { tables_.insert_or_assign(std::string(table), std::nullopt); }

    /// What ``other`` names too.
    void add(const Changed& other)
    {
        for (const auto& [table, keys] : other.tables_) {
            if (!keys) {
                add_all(table);
                continue;
            }
            for (const auto& key : *keys) {
                add(table, key);
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept { return tables_.empty(); }
    [[nodiscard]] bool touches(std::string_view table) const { return tables_.contains(table); }
    /// Whether any record of ``table`` may have changed, not only those
    /// ``keys`` names.
    [[nodiscard]] bool whole(std::string_view table) const
    {
        const auto it = tables_.find(table);
        return it != tables_.end() && !it->second;
    }
    /// The records changed in ``table``, by key: empty when it is not
    /// touched, or changed ``whole``.
    [[nodiscard]] const std::set<std::string>& keys(std::string_view table) const
    {
        static const std::set<std::string> none;
        const auto it = tables_.find(table);
        return it != tables_.end() && it->second ? *it->second : none;
    }
    /// The tables touched, in name order.
    [[nodiscard]] std::vector<std::string> tables() const
    {
        std::vector<std::string> out;
        out.reserve(tables_.size());
        for (const auto& [table, keys] : tables_) {
            out.push_back(table);
        }
        return out;
    }

private:
    /// By table, its records changed; nullopt for all of it.
    std::map<std::string, std::optional<std::set<std::string>>, std::less<>> tables_;
};

}  // namespace seventhslide::sync
