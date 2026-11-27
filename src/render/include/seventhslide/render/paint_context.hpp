#pragma once

// What the element painters need besides the element itself.
//
// Seam: Qt Gui. Every field is optional; a default context paints text in
// Qt's own fonts, skips images and draws every element live.

#include "seventhslide/render/source_frames.hpp"

#include <QDateTime>

namespace seventhslide::layout {
class QtFontFaces;
}

namespace seventhslide::render {

class BackgroundImageCache;
class ElementRasterCache;

struct PaintContext {
    /// Decoded pictures (backgrounds, image objects, image media).
    BackgroundImageCache* cache = nullptr;
    /// The resolved font faces layout measured with.
    const layout::QtFontFaces* faces = nullptr;
    /// What a wall clock shows. Invalid = the current time.
    QDateTime now;
    /// What a countdown clock counts down: the clock of the video in the
    /// frame being painted (``video_clock_of`` that frame in
    /// ``source_frames``; the compositor sets it for each frame it paints).
    const SourceClock* video_clock = nullptr;
    /// Rasters of elements already drawn, reused while they are unchanged.
    /// Null draws every element live.
    ElementRasterCache* rasters = nullptr;
    /// Pictures drawn elsewhere, by source: video items, web pages, live
    /// sources and video objects (``scene::video_object_key``).
    const SourceFrames* source_frames = nullptr;
};

}  // namespace seventhslide::render
