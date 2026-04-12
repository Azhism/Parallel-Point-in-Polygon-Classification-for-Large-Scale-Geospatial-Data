#include "index/quadtree.hpp"
#include <algorithm>

namespace pdc_geo {

void QuadTreeIndex::build(const std::vector<Polygon>& polygons) {
    if (polygons.empty()) return;

    polygons_ref_ = &polygons;

    BBox root_bbox = compute_bbox_union(polygons);

    double pad = (root_bbox.max_x - root_bbox.min_x) * 0.001;
    if (pad < 1e-6) pad = 1e-6;
    root_bbox.min_x -= pad;
    root_bbox.max_x += pad;
    root_bbox.min_y -= pad;
    root_bbox.max_y += pad;

    root_ = std::make_unique<QuadNode>(root_bbox);

    for (const auto& poly : polygons) {
        insert_polygon(root_.get(), poly.id, poly.bbox, 0);
    }
}

BBox QuadTreeIndex::compute_bbox_union(const std::vector<Polygon>& polygons) const {
    BBox result = polygons[0].bbox;
    for (size_t i = 1; i < polygons.size(); ++i) {
        result.min_x = std::min(result.min_x, polygons[i].bbox.min_x);
        result.max_x = std::max(result.max_x, polygons[i].bbox.max_x);
        result.min_y = std::min(result.min_y, polygons[i].bbox.min_y);
        result.max_y = std::max(result.max_y, polygons[i].bbox.max_y);
    }
    return result;
}

void QuadTreeIndex::insert_polygon(QuadNode* node, uint64_t polygon_id,
                                    const BBox& poly_bbox, int depth) {
    if (depth >= MAX_DEPTH) {
        node->polygon_ids.push_back(polygon_id);
        return;
    }

    if (!node->is_leaf()) {
        double mid_x = (node->bbox.min_x + node->bbox.max_x) / 2.0;
        double mid_y = (node->bbox.min_y + node->bbox.max_y) / 2.0;

        if (poly_bbox.min_x < mid_x && poly_bbox.min_y < mid_y && node->children[2])
            insert_polygon(node->children[2].get(), polygon_id, poly_bbox, depth + 1);
        if (poly_bbox.max_x > mid_x && poly_bbox.min_y < mid_y && node->children[3])
            insert_polygon(node->children[3].get(), polygon_id, poly_bbox, depth + 1);
        if (poly_bbox.min_x < mid_x && poly_bbox.max_y > mid_y && node->children[0])
            insert_polygon(node->children[0].get(), polygon_id, poly_bbox, depth + 1);
        if (poly_bbox.max_x > mid_x && poly_bbox.max_y > mid_y && node->children[1])
            insert_polygon(node->children[1].get(), polygon_id, poly_bbox, depth + 1);
        return;
    }

    node->polygon_ids.push_back(polygon_id);

    // BUG 3 FIX: was <= (only split at size 11), now < (splits correctly at size 7)
    if ((int)node->polygon_ids.size() < MAX_POLYGONS_PER_LEAF) {
        return;
    }

    double mid_x = (node->bbox.min_x + node->bbox.max_x) / 2.0;
    double mid_y = (node->bbox.min_y + node->bbox.max_y) / 2.0;

    BBox child_bboxes[4];
    child_bboxes[2] = BBox(node->bbox.min_x, node->bbox.min_y, mid_x, mid_y);   // SW
    child_bboxes[3] = BBox(mid_x, node->bbox.min_y, node->bbox.max_x, mid_y);   // SE
    child_bboxes[0] = BBox(node->bbox.min_x, mid_y, mid_x, node->bbox.max_y);   // NW
    child_bboxes[1] = BBox(mid_x, mid_y, node->bbox.max_x, node->bbox.max_y);   // NE

    for (int i = 0; i < 4; ++i)
        node->children[i] = std::make_unique<QuadNode>(child_bboxes[i]);

    std::vector<uint64_t> old_ids = std::move(node->polygon_ids); // move instead of copy
    node->polygon_ids.clear();

    if (polygons_ref_) {
        for (uint64_t pid : old_ids) {
            if (pid < polygons_ref_->size()) {
                const BBox& pbbox = (*polygons_ref_)[pid].bbox;
                for (int i = 0; i < 4; ++i) {
                    if (child_bboxes[i].intersects(pbbox))
                        node->children[i]->polygon_ids.push_back(pid);
                }
            }
        }
    }
}

std::vector<uint64_t> QuadTreeIndex::query_point(const Point& p) const {
    if (!root_) return {};

    // BUG 1 FIX: unordered_set gives O(1) avg insert vs O(log n) for std::set
    std::unordered_set<uint64_t> candidates;
    candidates.reserve(16); // pre-reserve to avoid rehashing for typical case
    query_node(root_.get(), p, candidates);

    return std::vector<uint64_t>(candidates.begin(), candidates.end());
}

void QuadTreeIndex::query_node(const QuadNode* node, const Point& p,
                                std::unordered_set<uint64_t>& candidates) const {
    if (!node->bbox.contains(p)) return;

    if (node->is_leaf()) {
        for (uint64_t pid : node->polygon_ids)
            candidates.insert(pid);
        return;
    }

    double mid_x = (node->bbox.min_x + node->bbox.max_x) / 2.0;
    double mid_y = (node->bbox.min_y + node->bbox.max_y) / 2.0;

    // BUG 2 FIX: was (p.x <= mid_x) AND (p.x >= mid_x) — a point on the boundary
    // satisfied BOTH conditions, visiting up to 4 quadrants and producing duplicates.
    // Fix: use strict < for left/bottom, >= for right/top — each point visits EXACTLY
    // one quadrant. Boundary points deterministically go right/up.
    bool left  = (p.x <  mid_x);
    bool right = (p.x >= mid_x);
    bool below = (p.y <  mid_y);
    bool above = (p.y >= mid_y);

    if (left  && below && node->children[2]) query_node(node->children[2].get(), p, candidates); // SW
    if (right && below && node->children[3]) query_node(node->children[3].get(), p, candidates); // SE
    if (left  && above && node->children[0]) query_node(node->children[0].get(), p, candidates); // NW
    if (right && above && node->children[1]) query_node(node->children[1].get(), p, candidates); // NE
}

size_t QuadTreeIndex::size() const {
    return root_ ? 1 : 0;
}

void QuadTreeIndex::clear() {
    root_.reset();
}

}  // namespace pdc_geo