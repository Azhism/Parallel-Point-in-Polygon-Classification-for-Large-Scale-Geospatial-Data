#include "geometry/ray_casting.hpp"
#include <cmath>
#include <algorithm>

namespace pdc_geo {

// Helper: compute cross product of vectors OA and OB
double RayCaster::cross_product(const Point& o, const Point& a, const Point& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Helper: check if point p is on segment ab (assumes collinearity)
bool RayCaster::point_on_segment(const Point& p, const Point& a, const Point& b) {
    double cross = cross_product(a, p, b);
    if (std::abs(cross) > EPSILON) return false;  // Not collinear
    
    bool on_x = (p.x >= std::min(a.x, b.x) - EPSILON) &&
                (p.x <= std::max(a.x, b.x) + EPSILON);
    bool on_y = (p.y >= std::min(a.y, b.y) - EPSILON) &&
                (p.y <= std::max(a.y, b.y) + EPSILON);
    
    return on_x && on_y;
}

// Ray-casting against a single ring
// Returns: +1 for each crossing, 0 for point on the ring
int RayCaster::ray_cast_ring(const Point& p, const std::vector<Point>& ring) {
    if (ring.size() < 3) return 0;
    
    int crossings = 0;
    size_t n = ring.size();
    
    for (size_t i = 0; i < n; i++) {
        const Point& a = ring[i];
        const Point& b = ring[(i + 1) % n];
        
        // Skip horizontal edges
        if (std::abs(a.y - b.y) < EPSILON) continue;
        
        // Check if point is on the edge
        if (point_on_segment(p, a, b)) return 0;  // ON_BOUNDARY
        
        // Check if point is within edge y-range
        double y_min = std::min(a.y, b.y);
        double y_max = std::max(a.y, b.y);
        
        if (p.y < y_min - EPSILON || p.y > y_max + EPSILON) continue;
        
        // Compute x-intersection of ray with edge
        double t = (p.y - a.y) / (b.y - a.y);
        double x_intersect = a.x + t * (b.x - a.x);
        
        // Ray is cast rightward (+x direction); only count if intersect is to the right
        if (x_intersect > p.x + EPSILON) {
            // Avoid double-counting at vertices: only count if endpoints
            // are on opposite sides of the ray's y-coordinate
            if ((a.y > p.y) != (b.y > p.y)) {
                crossings++;
            }
        }
    }
    
    return crossings;
}

RayCaster::Classification RayCaster::point_in_polygon(
    const Point& p, const Polygon& poly) {
    
    // Check bounding box first
    if (!poly.bbox.contains(p)) {
        return Classification::OUTSIDE;
    }
    
    // Test against exterior ring
    int crossings = ray_cast_ring(p, poly.exterior);
    
    // If on boundary, mark as such
    if (crossings == 0 && !poly.exterior.empty()) {
        // Point may be on exterior edge (ray_cast_ring returns 0 for on-boundary)
        // Check if any vertex or edge matched
        for (size_t i = 0; i < poly.exterior.size(); i++) {
            const Point& a = poly.exterior[i];
            const Point& b = poly.exterior[(i + 1) % poly.exterior.size()];
            if (point_on_segment(p, a, b)) {
                return Classification::ON_BOUNDARY;
            }
        }
    }
    
    if (crossings % 2 == 0) {
        // Even crossings or on boundary of exterior: definitely outside
        return Classification::OUTSIDE;
    }
    
    // Odd crossings: inside exterior. Now check holes.
    for (const auto& hole : poly.holes) {
        int hole_crossings = ray_cast_ring(p, hole);
        if (hole_crossings == 0) {
            // Check if on hole boundary
            for (size_t i = 0; i < hole.size(); i++) {
                const Point& a = hole[i];
                const Point& b = hole[(i + 1) % hole.size()];
                if (point_on_segment(p, a, b)) {
                    return Classification::ON_BOUNDARY;
                }
            }
        }
        
        if (hole_crossings % 2 == 1) {
            // Inside a hole: outside the polygon
            return Classification::OUTSIDE;
        }
    }
    
    // Inside exterior, not in any hole
    return Classification::INSIDE;
}

RayCaster::Classification RayCaster::point_in_multipolygon(
    const Point& p, const MultiPolygon& multi) {
    
    for (const auto& poly : multi.components) {
        Classification result = point_in_polygon(p, poly);
        if (result == Classification::INSIDE || result == Classification::ON_BOUNDARY) {
            return result;
        }
    }
    
    return Classification::OUTSIDE;
}

}  // namespace pdc_geo
