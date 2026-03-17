#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include <vector>

namespace pdc_geo {

/**
 * Simple bounding-box filtering without spatial index.
 * Used for baseline comparison and validation.
 */
class BBoxFilter {
 public:
    /**
     * Get candidate polygon IDs whose bbox contains the point.
     * Uses linear scan over all polygons.
     */
    static std::vector<uint64_t> get_candidates(
        const Point& p,
        const std::vector<Polygon>& polygons
    );
};

}  // namespace pdc_geo
