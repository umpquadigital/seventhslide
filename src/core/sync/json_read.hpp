#pragma once

// Reading the files the sync engine keeps in the account (change files,
// snapshots), each refused with one message for whatever is wrong with it.
// Not part of the public API.
// Seam: nlohmann/json; db for the schema version.

#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::sync {

/// No version counter reaches this; one past it could overflow.
inline constexpr std::int64_t k_max_counter = std::int64_t{1} << 62;

class JsonRead {
public:
    /// ``what`` names the kind of file in messages: "A sync change file".
    explicit JsonRead(std::string what) : what_(std::move(what)) {}

    [[noreturn]] void malformed(const std::string& why) const
    {
        throw std::runtime_error(what_ + " could not be read (" + why + ")");
    }

    const nlohmann::json& member(const nlohmann::json& j, const char* name) const
    {
        const auto it = j.find(name);
        if (it == j.end()) {
            malformed(std::string("no ") + name);
        }
        return *it;
    }

    std::string text(const nlohmann::json& j, const char* name) const
    {
        const auto& v = member(j, name);
        if (!v.is_string()) {
            malformed(std::string(name) + " is not text");
        }
        return v.get<std::string>();
    }

    /// ``name`` when it is there; empty when it is not. Anything but text
    /// there is malformed.
    std::string optional_text(const nlohmann::json& j, const char* name) const
    {
        const auto it = j.find(name);
        if (it == j.end()) {
            return {};
        }
        if (!it->is_string()) {
            malformed(std::string(name) + " is not text");
        }
        return it->get<std::string>();
    }

    /// ``name`` when it is there, ``fallback`` when it is not.
    bool optional_flag(const nlohmann::json& j, const char* name, bool fallback) const
    {
        const auto it = j.find(name);
        if (it == j.end()) {
            return fallback;
        }
        if (!it->is_boolean()) {
            malformed(std::string(name) + " is not true or false");
        }
        return it->get<bool>();
    }

    /// A version counter: a whole number from 0, short of where adding to it
    /// could overflow (no library ever counts that far, so a file that says
    /// so is not one).
    std::int64_t counter(const nlohmann::json& j, const char* name) const
    {
        const auto v = number(j, name);
        if (v < 0 || v >= k_max_counter) {
            malformed(std::string(name) + " is out of range");
        }
        return v;
    }

    /// ``read(j)``, with anything the JSON library throws about a value of
    /// the wrong type reported as this kind of file being malformed.
    template <typename Read>
    auto whole(const nlohmann::json& j, Read read) const
    {
        try {
            return read(j);
        } catch (const nlohmann::json::exception& ex) {
            malformed(ex.what());
        }
    }

    std::int64_t number(const nlohmann::json& j, const char* name) const
    {
        const auto& v = member(j, name);
        if (!v.is_number_integer()) {
            malformed(std::string(name) + " is not a whole number");
        }
        return v.get<std::int64_t>();
    }

    /// The media files ``j`` declares (``media``); none for a file that does
    /// not say.
    std::vector<db::MediaDigest> media(const nlohmann::json& j) const
    {
        std::vector<db::MediaDigest> out;
        const auto it = j.find("media");
        if (it == j.end()) {
            return out;
        }
        if (!it->is_array()) {
            malformed("media is not a list");
        }
        for (const auto& m : *it) {
            try {
                out.push_back(db::media_digest_from_json(m));
            } catch (const std::invalid_argument& ex) {
                malformed(ex.what());
            }
        }
        return out;
    }

    /// The schema of the library ``j`` was made from.
    int schema(const nlohmann::json& j) const
    {
        const auto v = number(j, "schema");
        if (v < 0 || v > 1'000'000) {
            malformed("schema is not a schema");
        }
        return static_cast<int>(v);
    }

    /// A format, as ``schema``.
    int format(const nlohmann::json& j) const
    {
        const auto v = number(j, "format");
        if (v < 0 || v > 1'000'000) {
            malformed("format is not a format");
        }
        return static_cast<int>(v);
    }

private:
    std::string what_;
};

/// Refuse ``j`` if it is of a later format than ``format_here``, or made from
/// a library of a later schema: a newer SeventhSlide made it, and this one
/// could not keep everything it holds. The format first: a later one need not
/// say its schema as this one does.
inline void refuse_newer(const JsonRead& read, const nlohmann::json& j, int format_here)
{
    if (read.format(j) > format_here || read.schema(j) > db::k_schema_version) {
        throw NewerFormat();
    }
}

}  // namespace seventhslide::sync
