#pragma once

#include <cmath>
#include <cstdint>

namespace pdc_geo {

struct Point {
    using coord_type = double;
    
    coord_type x;
    coord_type y;
    uint64_t id;  // Unique identifier for the point

    Point() : x(0.0), y(0.0), id(0) {}
    Point(coord_type x_, coord_type y_, uint64_t id_ = 0)
        : x(x_), y(y_), id(id_) {}

    // Compute squared Euclidean distance
    coord_type dist_sq(const Point& other) const {
        coord_type dx = x - other.x;
        coord_type dy = y - other.y;
        return dx * dx + dy * dy;
    }

    coord_type dist(const Point& other) const {
        return std::sqrt(dist_sq(other));
    }

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
};

}  // namespace pdc_geo
