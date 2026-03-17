#pragma once

#include "geometry/polygon.hpp"
#include "geometry/point.hpp"
#include <vector>
#include <memory>
#include <set>

namespace pdc_geo {

/**
 * Quadtree spatial index for point-in-polygon classification.
 * 
 * Recursively partitions space into 4 quadrants (NW, NE, SW, SE).
 * Each leaf node stores up to MAX_POLYGONS_PER_LEAF polygon IDs.
 * When a leaf overflows, it splits into 4 children and redistributes.
 */
class QuadTreeIndex {
 public:
    struct QuadNode {
        BBox bbox;
        std::vector<uint64_t> polygon_ids;  // IDs of polygons in this node (leaf only)
        std::unique_ptr<QuadNode> children[4];  // NW, NE, SW, SE (null if leaf)
        
        QuadNode(const BBox& b) : bbox(b) {
            // children are default-initialized to nullptr
        }
        
        bool is_leaf() const {
            return children[0] == nullptr;
        }
    };
    
    // Configuration constants
    static constexpr int MAX_POLYGONS_PER_LEAF = 10;
    static constexpr int MAX_DEPTH = 8;
    
    QuadTreeIndex() : polygons_ref_(nullptr) {}
    ~QuadTreeIndex() = default;
    
    /**
     * Build spatial index from polygon dataset.
     * Computes the root node's bbox from all polygon bboxes,
     * then recursively inserts each polygon.
     */
    void build(const std::vector<Polygon>& polygons);
    
    /**
     * Query the index for polygon candidates containing a point.
     * Returns a vector of unique polygon IDs (deduplicated).
     */
    std::vector<uint64_t> query_point(const Point& p) const;
    
    /**
     * Get the size of the index (number of leaf entries).
     */
    size_t size() const;
    
    /**
     * Clear the index.
     */
    void clear();
    
 private:
    std::unique_ptr<QuadNode> root_;
    const std::vector<Polygon>* polygons_ref_;  // Reference to original polygon data
    
    // Helper: compute union of all polygon bboxes
    BBox compute_bbox_union(const std::vector<Polygon>& polygons) const;
    
    // Helper: recursively insert a polygon
    void insert_polygon(QuadNode* node, uint64_t polygon_id, const BBox& poly_bbox, int depth);
    
    // Helper: recursively query for candidates
    void query_node(const QuadNode* node, const Point& p, std::set<uint64_t>& candidates) const;
};

}  // namespace pdc_geo
