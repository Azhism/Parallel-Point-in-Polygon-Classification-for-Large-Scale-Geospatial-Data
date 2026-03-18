#pragma once

#include "geometry/polygon.hpp"
#include "geometry/point.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdc_geo {

/**
 * Strip-based spatial index.
 *
 * Space is partitioned into horizontal strips along the Y axis.
 * Each strip stores IDs of polygons whose bounding box overlaps that strip.
 */
class StripIndex {
 public:
    explicit StripIndex(std::size_t num_strips = 0);

    /**
     * Build index from polygon dataset.
     * If num_strips passed to constructor is 0, uses sqrt(polygon_count).
     */
    void build(const std::vector<Polygon>& polygons);

    /**
     * Query candidates for a point based on the point's strip.
     */
    std::vector<uint64_t> query_point(const Point& p) const;

    /**
     * Number of strips in the current index.
     */
    std::size_t strip_count() const { return strips_.size(); }

    /**
     * Remove all index data.
     */
    void clear();

 private:
    std::size_t requested_num_strips_;
    double y_min_;
    double y_max_;
    double strip_height_;
    std::vector<std::vector<uint64_t>> strips_;
};

}  // namespace pdc_geo
