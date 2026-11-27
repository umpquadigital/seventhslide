#pragma once

// What every canvas edit answers with, and the few types an editor's
// commands take: the z-order moves and the geometry a drag or a ribbon
// writes. Seam: plain C++20.

#include <string>
#include <utility>
#include <vector>

namespace seventhslide::canvas {

struct EditResult {
    bool ok = true;
    std::string error;

    [[nodiscard]] static EditResult success() { return {}; }
    [[nodiscard]] static EditResult fail(std::string message)
    {
        return EditResult{false, std::move(message)};
    }

    explicit operator bool() const noexcept { return ok; }
};

enum class ZOrderOp {
    ToFront,
    Forward,
    Backward,
    ToBack,
};

enum class GeometryUnits {
    Exact,         ///< as given (Freeform)
    WholePixels,   ///< rounded (text themes project onto integer pixel fields)
};

struct ObjectGeometry {
    std::string id;
    double x = 0;
    double y = 0;
    double w = 100;
    double h = 100;
    double rotation = 0;
    bool flip_h = false;
    bool flip_v = false;
    bool has_flip = false;
};

}  // namespace seventhslide::canvas
