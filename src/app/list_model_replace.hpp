#pragma once

// Replace a QAbstractListModel backing list without modelReset when row
// identity is unchanged (rename / in-place field updates). ListView keeps
// contentY on dataChanged; beginResetModel jumps scroll to the top.
//
// The same list with rows come and gone (an edit, another computer's sync)
// changes by those rows alone (``RowSteps``), and stays where it is scrolled.
// A list with nothing in common is another list: a reset, as is one whose keys
// repeat. So is another view of the rows, as a search: the model says which.

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace seventhslide::app {

enum class ListReplace { Noop, DataChanged, Reset };

template<typename Key>
ListReplace classify_identity_keys(const std::vector<Key>& dest, const std::vector<Key>& next)
{
    if (dest.empty() && next.empty()) {
        return ListReplace::Noop;
    }
    if (dest.size() != next.size()) {
        return ListReplace::Reset;
    }
    auto a = dest;
    auto b = next;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b ? ListReplace::DataChanged : ListReplace::Reset;
}

template<typename T, typename KeyFn>
ListReplace classify_list_replace(const std::vector<T>& dest, const std::vector<T>& next, KeyFn key)
{
    using Key = std::decay_t<std::invoke_result_t<KeyFn, const T&>>;
    std::vector<Key> dest_keys;
    std::vector<Key> next_keys;
    dest_keys.reserve(dest.size());
    next_keys.reserve(next.size());
    for (const auto& row : dest) {
        dest_keys.push_back(key(row));
    }
    for (const auto& row : next) {
        next_keys.push_back(key(row));
    }
    return classify_identity_keys(dest_keys, next_keys);
}

/// The rows that stay on the way from one list to another, marked in each.
struct KeptRows {
    std::vector<char> before;
    std::vector<char> after;
};

/// As many rows as can stay, those both lists hold in the same order; one that
/// moved goes and comes back. None when the lists have nothing in common or
/// their keys repeat.
template<typename Key>
std::optional<KeptRows> kept_rows(const std::vector<Key>& before, const std::vector<Key>& after)
{
    std::map<Key, int> place;
    for (int i = 0; i < static_cast<int>(after.size()); ++i) {
        if (!place.emplace(after[static_cast<std::size_t>(i)], i).second) {
            return std::nullopt;
        }
    }
    std::map<Key, int> seen;
    std::vector<int> rows;
    std::vector<int> places;
    for (int i = 0; i < static_cast<int>(before.size()); ++i) {
        const auto& key = before[static_cast<std::size_t>(i)];
        if (!seen.emplace(key, i).second) {
            return std::nullopt;
        }
        if (const auto it = place.find(key); it != place.end()) {
            rows.push_back(i);
            places.push_back(it->second);
        }
    }
    if (rows.empty()) {
        return std::nullopt;
    }
    // The longest run of places that only rises. ``ends[n]`` is the entry
    // ending the best run of n + 1, ``previous`` the entry before each.
    std::vector<int> ends;
    std::vector<int> previous(places.size(), -1);
    for (int i = 0; i < static_cast<int>(places.size()); ++i) {
        const auto at = std::lower_bound(ends.begin(), ends.end(), places[static_cast<std::size_t>(i)],
                                         [&](int end, int value) {
                                             return places[static_cast<std::size_t>(end)] < value;
                                         });
        if (at != ends.begin()) {
            previous[static_cast<std::size_t>(i)] = *std::prev(at);
        }
        if (at == ends.end()) {
            ends.push_back(i);
        } else {
            *at = i;
        }
    }
    KeptRows kept{std::vector<char>(before.size()), std::vector<char>(after.size())};
    for (int i = ends.back(); i >= 0; i = previous[static_cast<std::size_t>(i)]) {
        kept.before[static_cast<std::size_t>(rows[static_cast<std::size_t>(i)])] = 1;
        kept.after[static_cast<std::size_t>(places[static_cast<std::size_t>(i)])] = 1;
    }
    return kept;
}

/// A list model's rows changed step by step, each announced as it happens. A
/// model using this declares it a friend, for its begin and end calls.
struct RowSteps {
    /// ``drop(first, last)`` for each run of rows going, last first, then
    /// ``settle()``, then ``add(first, last)`` for each run coming, in order.
    template<typename Model, typename Drop, typename Settle, typename Add>
    static void apply(Model& model, const KeptRows& kept, Drop drop, Settle settle, Add add)
    {
        for (int last = static_cast<int>(kept.before.size()) - 1; last >= 0; --last) {
            if (kept.before[static_cast<std::size_t>(last)]) {
                continue;
            }
            int first = last;
            while (first > 0 && !kept.before[static_cast<std::size_t>(first - 1)]) {
                --first;
            }
            model.beginRemoveRows({}, first, last);
            drop(first, last);
            model.endRemoveRows();
            last = first;
        }
        settle();
        const int rows = static_cast<int>(kept.after.size());
        for (int first = 0; first < rows; ++first) {
            if (kept.after[static_cast<std::size_t>(first)]) {
                continue;
            }
            int last = first;
            while (last + 1 < rows && !kept.after[static_cast<std::size_t>(last + 1)]) {
                ++last;
            }
            model.beginInsertRows({}, first, last);
            add(first, last);
            model.endInsertRows();
            first = last;
        }
    }

    /// ``rows`` becomes ``next``, as little of the view changing as may be.
    template<typename Model, typename T, typename KeyFn>
    static void replace(Model& model, std::vector<T>& rows, std::vector<T> next, KeyFn key)
    {
        const auto keys = [&](const std::vector<T>& list) {
            using Key = std::decay_t<std::invoke_result_t<KeyFn&, const T&>>;
            std::vector<Key> out;
            out.reserve(list.size());
            std::transform(list.begin(), list.end(), std::back_inserter(out),
                           [&](const T& row) { return std::invoke(key, row); });
            return out;
        };
        const auto before = keys(rows);
        const auto after = keys(next);
        const auto kind = classify_identity_keys(before, after);
        if (kind == ListReplace::Noop) {
            return;
        }
        const auto kept = kind == ListReplace::Reset ? kept_rows(before, after) : std::nullopt;
        if (kind == ListReplace::DataChanged) {
            rows = std::move(next);
        } else if (kept) {
            apply(
                model, *kept,
                [&](int first, int last) { rows.erase(rows.begin() + first, rows.begin() + last + 1); },
                [&] {
                    // What stays reads as it is now.
                    std::size_t at = 0;
                    for (std::size_t i = 0; i < next.size(); ++i) {
                        if (kept->after[i]) {
                            rows[at++] = next[i];
                        }
                    }
                },
                [&](int first, int last) {
                    rows.insert(rows.begin() + first, next.begin() + first, next.begin() + last + 1);
                });
        } else {
            model.beginResetModel();
            rows = std::move(next);
            model.endResetModel();
            return;
        }
        if (!rows.empty()) {
            emit model.dataChanged(model.index(0), model.index(static_cast<int>(rows.size()) - 1));
        }
    }
};

} // namespace seventhslide::app
