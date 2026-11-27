#pragma once

// Every library file a Freeform document names: the pictures and videos its
// objects place and its slides' backgrounds. Walks the stored JSON, so the database
// can find and repoint files without parsing a document, and the model, so
// an editor can. Both read canvas/media_refs.hpp's table.
// Seam: plain C++20 + nlohmann/json.

#include "seventhslide/canvas/media_refs.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/json/read.hpp"

#include <nlohmann/json.hpp>

#include <concepts>
#include <string>
#include <type_traits>

namespace seventhslide::freeform {

using canvas::MediaRefKind;

/// ``fn(path, kind)`` for every file ``obj`` holds. ``path`` is the field
/// itself (``std::string&``, or const), so it can be read or repointed.
template <class Object, class Fn>
void visit_media(Object& obj, Fn&& fn)
{
    if (obj.type == ObjectType::Image) {
        fn(obj.filename, MediaRefKind::Picture);
    }
    if (obj.type == ObjectType::Video) {
        fn(obj.filename, MediaRefKind::Video);
        if (!obj.video.poster.empty()) {
            fn(obj.video.poster, MediaRefKind::Picture);
        }
    }
    // A picture fill's file: in use, or kept from when it was.
    if (obj.fill.kind == canvas::FillKind::Picture || !obj.fill.picture.file.empty()) {
        fn(obj.fill.picture.file, MediaRefKind::Picture);
    }
}

/// ``fn(path, kind)`` for every file a slide's background names.
template <class Background, class Fn>
void visit_background_media(Background& bg, Fn&& fn)
{
    if (bg.kind == SlideBackgroundKind::Image) {
        fn(bg.image, MediaRefKind::Picture);
    } else if (bg.kind == SlideBackgroundKind::Video) {
        fn(bg.video, MediaRefKind::Video);
    }
}

/// ``fn(path, kind)`` for every file ``doc`` names, deck by deck, slide by
/// slide: the background first, then its objects.
template <class Doc, class Fn>
void visit_document_media(Doc& doc, Fn&& fn)
{
    for (auto& deck : doc.decks) {
        for (auto& slide : deck.slides) {
            visit_background_media(slide.background, fn);
            for (auto& obj : slide.objects) {
                visit_media(obj, fn);
            }
        }
    }
}

/// The same walk over a stored document. ``path`` is the JSON string node.
/// Anything not shaped as a document is walked as far as it goes.
template <class Json, class Fn>
void visit_document_media(Json& doc, Fn&& fn)
    requires std::same_as<std::remove_const_t<Json>, nlohmann::json>
{
    auto* decks = json::member(doc, "decks");
    if (decks == nullptr || !decks->is_array()) {
        return;
    }
    for (auto& deck : *decks) {
        auto* slides = json::member(deck, "slides");
        if (slides == nullptr || !slides->is_array()) {
            continue;
        }
        for (auto& slide : *slides) {
            if (auto* bg = json::member(slide, "background")) {
                const auto* kind = json::member(*bg, "kind");
                if (kind != nullptr && kind->is_string()) {
                    const auto& name = kind->template get_ref<const std::string&>();
                    if (name == "image" || name == "video") {
                        if (auto* file = json::member(*bg, name.c_str());
                            file != nullptr && file->is_string()) {
                            fn(*file, name == "image" ? MediaRefKind::Picture : MediaRefKind::Video);
                        }
                    }
                }
            }
            auto* objects = json::member(slide, "objects");
            if (objects == nullptr || !objects->is_array()) {
                continue;
            }
            for (auto& obj : *objects) {
                canvas::visit_object_media(obj, fn);
            }
        }
    }
}

}  // namespace seventhslide::freeform
