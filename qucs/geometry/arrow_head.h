#ifndef GEOMETRY_ARROW_HEAD_H
#define GEOMETRY_ARROW_HEAD_H

#include <array>
#include <cmath>

namespace qucs_s {
namespace geom {

struct Point2D {
    double x, y;
};

struct ArrowHeadShape {
    bool filled;
    // Open-head representation: two lines from the shaft tip to each wing.
    Point2D wing1;
    Point2D wing2;
    // Offsets from the shaft end to each wing, exposed so callers can
    // reproduce the original "shaftEnd - int(offset)" truncation order.
    Point2D wingOffset1;
    Point2D wingOffset2;
    // Filled-head representation: one closed triangle {wing1, tip, wing2}.
    std::array<Point2D, 3> headPoints;
};

// shaftEndX/shaftEndY: the arrow tip (shaft start offset by dx/dy).
// dx/dy: the shaft direction vector (pre-offset deltas).
// headHeight/headWidth: arrow head size fields.
// filled: the requested headStyle.
inline ArrowHeadShape computeArrowHeadShape(double shaftEndX, double shaftEndY,
                                             double dx, double dy,
                                             double headHeight,
                                             double headWidth,
                                             bool filled) noexcept {
    double beta = std::atan2(headWidth, headHeight);
    double phi = std::atan2(dy, dx);
    double length = std::sqrt(headWidth * headWidth + headHeight * headHeight);

    double w = beta + phi;
    Point2D wingOffset1{length * std::cos(w), length * std::sin(w)};
    Point2D wing1{shaftEndX - wingOffset1.x, shaftEndY - wingOffset1.y};

    w = phi - beta;
    Point2D wingOffset2{length * std::cos(w), length * std::sin(w)};
    Point2D wing2{shaftEndX - wingOffset2.x, shaftEndY - wingOffset2.y};

    ArrowHeadShape shape{};
    shape.filled = filled;
    shape.wing1 = wing1;
    shape.wing2 = wing2;
    shape.wingOffset1 = wingOffset1;
    shape.wingOffset2 = wingOffset2;
    shape.headPoints[0] = wing1;
    shape.headPoints[1] = Point2D{shaftEndX, shaftEndY};
    shape.headPoints[2] = wing2;
    return shape;
}

} // namespace geom
} // namespace qucs_s

#endif
