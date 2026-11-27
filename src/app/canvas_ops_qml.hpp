#pragma once

// The arrange commands' choices as QML names them: ``CanvasOrder.Front``,
// ``CanvasAlign.Left``, ``CanvasDistribute.Horizontal``,
// ``CanvasFlip.Vertical``. SlideObjectsSession's reorderSelected, align,
// distribute and flip take one, so a misspelt choice is a qmllint error
// rather than a refusal at run time. Each enum is an int underneath, so any
// number QML passes is a value of it and to_core can refuse the rest.

#include <seventhslide/canvas/layout.hpp>
#include <seventhslide/freeform/doc_edit.hpp>

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace seventhslide::app {

namespace CanvasOrderQml {
Q_NAMESPACE
QML_NAMED_ELEMENT(CanvasOrder)

enum Order : int { Front, Forward, Backward, Back };
Q_ENUM_NS(Order)

/// ``order`` as core's stacking move; nullopt for a value outside the enum.
[[nodiscard]] constexpr std::optional<freeform::ZOrderOp> to_core(Order order) noexcept
{
    switch (order) {
    case Front:
        return freeform::ZOrderOp::ToFront;
    case Forward:
        return freeform::ZOrderOp::Forward;
    case Backward:
        return freeform::ZOrderOp::Backward;
    case Back:
        return freeform::ZOrderOp::ToBack;
    }
    return std::nullopt;
}

} // namespace CanvasOrderQml

namespace CanvasAlignQml {
Q_NAMESPACE
QML_NAMED_ELEMENT(CanvasAlign)

enum Align : int { Left, Center, Right, Top, Middle, Bottom };
Q_ENUM_NS(Align)

[[nodiscard]] constexpr std::optional<canvas::Align> to_core(Align align) noexcept
{
    switch (align) {
    case Left:
        return canvas::Align::Left;
    case Center:
        return canvas::Align::Center;
    case Right:
        return canvas::Align::Right;
    case Top:
        return canvas::Align::Top;
    case Middle:
        return canvas::Align::Middle;
    case Bottom:
        return canvas::Align::Bottom;
    }
    return std::nullopt;
}

} // namespace CanvasAlignQml

namespace CanvasDistributeQml {
Q_NAMESPACE
QML_NAMED_ELEMENT(CanvasDistribute)

enum Distribute : int { Horizontal, Vertical };
Q_ENUM_NS(Distribute)

[[nodiscard]] constexpr std::optional<canvas::Distribute> to_core(Distribute dist) noexcept
{
    switch (dist) {
    case Horizontal:
        return canvas::Distribute::Horizontal;
    case Vertical:
        return canvas::Distribute::Vertical;
    }
    return std::nullopt;
}

} // namespace CanvasDistributeQml

namespace CanvasFlipQml {
Q_NAMESPACE
QML_NAMED_ELEMENT(CanvasFlip)

enum Axis : int { Horizontal, Vertical };
Q_ENUM_NS(Axis)

[[nodiscard]] constexpr std::optional<canvas::FlipAxis> to_core(Axis axis) noexcept
{
    switch (axis) {
    case Horizontal:
        return canvas::FlipAxis::Horizontal;
    case Vertical:
        return canvas::FlipAxis::Vertical;
    }
    return std::nullopt;
}

} // namespace CanvasFlipQml

} // namespace seventhslide::app
