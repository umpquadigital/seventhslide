#pragma once

// Every library file a stored canvas object names: one table, read by the
// Freeform document's walkers and the database's, so a new part that holds a
// file is registered here and nowhere else.
// Seam: plain C++20 + nlohmann/json.

#include "seventhslide/json/read.hpp"

#include <nlohmann/json.hpp>

#include <string_view>

namespace seventhslide::canvas {

enum class MediaRefKind {
    Picture,
    Video,
};

/// One file field of a stored object: ``key`` (or ``key.sub`` when ``sub`` is
/// set) on objects of ``type``, or of every type when ``type`` is empty.
struct ObjectMediaField {
    std::string_view type;
    std::string_view key;
    std::string_view sub;
    MediaRefKind kind;
};

inline constexpr ObjectMediaField k_object_media_fields[] = {
    {"image", "filename", {}, MediaRefKind::Picture},
    // A picture fill's file, on any object that has one.
    {{}, "fill_picture", "file", MediaRefKind::Picture},
    // A video object's file, and the picture it shows until it plays.
    {"video", "filename", {}, MediaRefKind::Video},
    {"video", "video", "poster", MediaRefKind::Picture},
};

/// ``fn(path, kind)`` for every file field ``obj`` (a stored object) holds, in
/// table order. ``path`` is the JSON string node, so a caller may read it or
/// point it at another file. ``Json`` is ``nlohmann::json`` or ``const
/// nlohmann::json``.
template <class Json, class Fn>
void visit_object_media(Json& obj, Fn&& fn)
{
    if (!obj.is_object()) {
        return;
    }
    const auto* type = json::member(obj, "type");
    // Both arms string_view: a std::string arm and a "" arm would make the
    // result a temporary std::string, gone before type_name is read.
    const std::string_view type_name = type != nullptr && type->is_string()
        ? std::string_view(type->template get_ref<const std::string&>())
        : std::string_view{};
    for (const auto& field : k_object_media_fields) {
        if (!field.type.empty() && field.type != type_name) {
            continue;
        }
        auto* node = json::member(obj, field.key);
        if (node != nullptr && !field.sub.empty()) {
            node = json::member(*node, field.sub);
        }
        if (node != nullptr && node->is_string()) {
            fn(*node, field.kind);
        }
    }
}

}  // namespace seventhslide::canvas
