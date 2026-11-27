#pragma once

// What this computer is fetching, and from which connection, across all of
// them. A computer connected to several others (the server and the church PC,
// say) asks each file of one of them at a time: a change file is not fetched
// twice, and a media file has one writer, so the part on the disk is only ever
// added to by the transfer it belongs to. When a connection answers that it
// has not got a file, or closes, the file is let go and another may be asked.
//
// Items are named as the peer names them ("change/<machine>/<n>",
// "media/<path>", ...), per library.
//
// Seam: plain C++20.

#include <map>
#include <set>
#include <string>
#include <utility>

namespace seventhslide::link {

class Fetches {
public:
    /// Claim ``item`` of ``library`` for ``by``: true when no one else has
    /// it (``by`` may claim again what it holds).
    bool claim(const std::string& library, const std::string& item, const void* by)
    {
        const auto [it, added] = by_item_.emplace(std::pair{library, item}, by);
        return added || it->second == by;
    }
    /// ``by`` is done with ``item``: fetched, refused or not to be had there.
    void release(const std::string& library, const std::string& item, const void* by)
    {
        const auto it = by_item_.find(std::pair{library, item});
        if (it != by_item_.end() && it->second == by) {
            by_item_.erase(it);
        }
    }
    /// Let go of everything ``by`` holds (of ``library`` only, when given):
    /// the libraries it held any of.
    std::set<std::string> release_all(const void* by, const std::string* library = nullptr)
    {
        std::set<std::string> libraries;
        for (auto it = by_item_.begin(); it != by_item_.end();) {
            if (it->second == by && (library == nullptr || it->first.first == *library)) {
                libraries.insert(it->first.first);
                it = by_item_.erase(it);
            } else {
                ++it;
            }
        }
        return libraries;
    }
    /// Whether anyone is fetching ``item``.
    [[nodiscard]] bool claimed(const std::string& library, const std::string& item) const
    {
        return by_item_.contains(std::pair{library, item});
    }

private:
    std::map<std::pair<std::string, std::string>, const void*> by_item_;
};

}  // namespace seventhslide::link
