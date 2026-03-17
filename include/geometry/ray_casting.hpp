#pragma once

#include "point.hpp"
#include "polygon.hpp"
#include <cmath>
#include <limits>

namespace pdc_geo {

class RayCaster {
 public:
    // Classification result
    enum class Classification { OUTSIDE, INSIDE, ON_BOUNDARY };
    
    // Tolerance for floating-point comparisons
    static constexpr double EPSILON = 1e-10;
    
    /**
     * Test if a point is inside a simple polygon using ray-casting.
     * 
     * Edge case handling:
     * - Point on edge or vertex: classified as ON_BOUNDARY
     * - Horizontal polygon edges: skipped in crossing count
     * - Vertex crossing: only counted if one endpoint strictly above rayline,
     *   other on-or-below (prevents double-counting)
     * 
     * Assumes CCW winding order for exterior ring.
     */
    static Classification point_in_polygon(const Point& p, const Polygon& poly);
    
    /**
     * Test if a point is inside a multi-polygon.
     * Inside if inside any component and not in any hole.
     */
    static Classification point_in_multipolygon(const Point& p, const MultiPolygon& multi);
    
 private:
    // Helper: ray-cast test against a simple ring (exterior or hole)
    static int ray_cast_ring(const Point& p, const std::vector<Point>& ring);
    
    // Helper: check if point is on line segment
    static bool point_on_segment(const Point& p, const Point& a, const Point& b);
    
    // Helper: compute cross product for orientation
    static double cross_product(const Point& o, const Point& a, const Point& b);
};

}  // namespace pdc_geo
