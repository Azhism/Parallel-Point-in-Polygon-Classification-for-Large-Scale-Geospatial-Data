#pragma once

#include "point.hpp"
#include <vector>
#include <cstdint>

namespace pdc_geo {

// Bounding box representation
struct BBox {
    using coord_type = double;
    
    coord_type min_x, min_y, max_x, max_y;
    
    BBox() : min_x(0), min_y(0), max_x(0), max_y(0) {}
    BBox(coord_type min_x_, coord_type min_y_, coord_type max_x_, coord_type max_y_)
        : min_x(min_x_), min_y(min_y_), max_x(max_x_), max_y(max_y_) {}
    
    // Check if a point is inside/on the bounding box
    bool contains(const Point& p) const {
        return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
    }
    
    // Check if two bounding boxes overlap
    bool intersects(const BBox& other) const {
        return !(max_x < other.min_x || min_x > other.max_x ||
                 max_y < other.min_y || min_y > other.max_y);
    }
    
    // Expand bbox to include a point
    void expand(const Point& p) {
        if (min_x > max_x) {  // Uninitialized
            min_x = max_x = p.x;
            min_y = max_y = p.y;
        } else {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
    }
};

// A simple polygon with optional holes
struct Polygon {
    uint64_t id;
    std::vector<Point> exterior;  // Outer ring vertices in CCW order
    std::vector<std::vector<Point>> holes;  // Inner rings (holes) in CW order
    BBox bbox;
    
    Polygon() : id(0) {}
    
    Polygon(uint64_t id_, const std::vector<Point>& ext)
        : id(id_), exterior(ext) {
        compute_bbox();
    }
    
    // Compute bounding box from exterior ring
    void compute_bbox() {
        if (exterior.empty()) return;
        bbox = BBox(exterior[0].x, exterior[0].y, exterior[0].x, exterior[0].y);
        for (const auto& p : exterior) {
            bbox.expand(p);
        }
    }
    
    // Add a hole to this polygon
    void add_hole(const std::vector<Point>& hole) {
        holes.push_back(hole);
    }
    
    size_t num_vertices() const {
        size_t count = exterior.size();
        for (const auto& hole : holes) {
            count += hole.size();
        }
        return count;
    }
};

// Multi-polygon: collection of simple polygons
struct MultiPolygon {
    uint64_t id;
    std::vector<Polygon> components;
    BBox bbox;
    
    MultiPolygon() : id(0) {}
    
    void add_polygon(const Polygon& poly) {
        if (components.empty()) {
            bbox = poly.bbox;
        } else {
            bbox.min_x = std::min(bbox.min_x, poly.bbox.min_x);
            bbox.max_x = std::max(bbox.max_x, poly.bbox.max_x);
            bbox.min_y = std::min(bbox.min_y, poly.bbox.min_y);
            bbox.max_y = std::max(bbox.max_y, poly.bbox.max_y);
        }
        components.push_back(poly);
    }
};

}  // namespace pdc_geo
