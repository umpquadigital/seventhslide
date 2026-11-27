#pragma once

// Lists kept in the order the operator sets: a service's items, a folder's
// folders, Freeform documents and media files. Each row has a number (its
// ``order_num`` or ``sort_order``) and its list reads in that order.
//
// The numbers are a gap apart. A row added or moved takes a number between its
// new neighbours', so no other row's changes. Lists sync row by row, so two
// computers adding or moving different rows of one list never write the same
// rows, and both changes stand; numbering 0..n-1 made every insert or drag
// rewrite the whole list, and one computer's renumbering could undo another's
// move. A list whose gap fills up is spaced out again.
//
// Rows with the same number are listed by something every computer has alike
// (a uid, a file's name), never by their local id.
//
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace seventhslide::db {

/// The room left between neighbours. A dozen rows can go into one gap before
/// the list is spaced out again.
inline constexpr std::int64_t k_order_gap = 4096;

/// ``count`` numbers, rising, strictly between ``low`` and ``high`` (either
/// absent: no neighbour on that side); nullopt when there is no room.
[[nodiscard]] std::optional<std::vector<std::int64_t>> orders_between(
    std::optional<std::int64_t> low, std::optional<std::int64_t> high, int count);

/// The numbers for rows numbered ``now``, listed in the order wanted, that put
/// them in that order changing as few as can be: the longest run already in
/// order keeps its numbers, and each row between takes one between theirs.
/// When there is no room, every row is spaced out afresh.
[[nodiscard]] std::vector<std::int64_t> reordered(const std::vector<std::int64_t>& now);

/// Where ``table``'s rows of one list keep their order: the number column, the
/// column naming the list (NULL for the root), and that list.
struct OrderedList {
    std::string_view table;
    std::string_view order_column;
    std::string_view parent_column;
    std::optional<std::int64_t> parent;
};

/// The number that puts a new row last in ``list``: a gap past the largest,
/// or 0 for the first.
[[nodiscard]] std::int64_t next_order(Database& db, const OrderedList& list);

/// Put ``list``'s rows ``ids`` (every one of them) in that order, writing only
/// the rows that move. An id not in the list is ignored.
void reorder_rows(Database& db, const OrderedList& list, const std::vector<std::int64_t>& ids);

}  // namespace seventhslide::db
