#pragma once

#include "geometry/polygon.hpp"
#include <vector>
#include <string>

namespace pdc_gen {

/**
 * Utilities for creating and loading polygon datasets.
 */
class PolygonLoader {
 public:
    /**
     * Create a simple square polygon.
     */
    static pdc_geo::Polygon create_square(
        uint64_t id,
        double x_min, double y_min,
        double x_max, double y_max
    );
    
    /**
     * Create a grid of square polygons covering a region.
     */
    static std::vector<pdc_geo::Polygon> create_grid(
        double x_min, double y_min,
        double x_max, double y_max,
        size_t cols, size_t rows
    );
    
    /**
     * Create a synthetic circle polygon (approximated with many vertices).
     */
    static pdc_geo::Polygon create_circle(
        uint64_t id,
        double center_x, double center_y,
        double radius,
        size_t num_vertices = 32
    );
};

}  // namespace pdc_gen
