#pragma once

#include "geometry/polygon.hpp"
#include "geometry/point.hpp"
#include <vector>
#include <memory>
#include <unordered_set>  // BUG 1 FIX: was std::set (O log n) → now O(1) avg

namespace pdc_geo {

class QuadTreeIndex {
 public:
    struct QuadNode {
        BBox bbox;
        std::vector<uint64_t> polygon_ids;
        std::unique_ptr<QuadNode> children[4];

        QuadNode(const BBox& b) : bbox(b) {}

        bool is_leaf() const {
            return children[0] == nullptr;
        }
    };

    // BUG 4 FIX: Increased depth and reduced leaf size for 10K polygon grid
    // With 100x100 grid, polygons are small and uniform — deeper tree = fewer candidates
    static constexpr int MAX_POLYGONS_PER_LEAF = 6;   // was 10
    static constexpr int MAX_DEPTH = 12;               // was 8

    QuadTreeIndex() : polygons_ref_(nullptr) {}
    ~QuadTreeIndex() = default;

    void build(const std::vector<Polygon>& polygons);
    std::vector<uint64_t> query_point(const Point& p) const;
    size_t size() const;
    void clear();

 private:
    std::unique_ptr<QuadNode> root_;
    const std::vector<Polygon>* polygons_ref_;

    BBox compute_bbox_union(const std::vector<Polygon>& polygons) const;
    void insert_polygon(QuadNode* node, uint64_t polygon_id, const BBox& poly_bbox, int depth);

    // BUG 1 FIX: changed std::set → std::unordered_set
    void query_node(const QuadNode* node, const Point& p,
                    std::unordered_set<uint64_t>& candidates) const;
};

}  // namespace pdc_geo