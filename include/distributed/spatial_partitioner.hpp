#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include <vector>
#include <cstdint>

namespace pdc_mpi {

enum class PolygonMode {
    REPLICATE,          // Every rank gets all polygons
    SPATIAL_PARTITION   // Each rank gets polygons overlapping its spatial region
};

struct SpatialRegion {
    double x_min, x_max, y_min, y_max;

    bool contains(const pdc_geo::Point& p) const {
        return p.x >= x_min && p.x <= x_max && p.y >= y_min && p.y <= y_max;
    }

    bool intersects(const pdc_geo::BBox& bbox) const {
        return !(bbox.max_x < x_min || bbox.min_x > x_max ||
                 bbox.max_y < y_min || bbox.min_y > y_max);
    }
};

class SpatialPartitioner {
public:
    /**
     * Divide the domain into num_ranks vertical strips along the X-axis.
     */
    static std::vector<SpatialRegion> create_strips(
        double x_min, double x_max, double y_min, double y_max,
        int num_ranks
    );

    /**
     * Divide the domain into a 2D grid (rows x cols factored from num_ranks).
     */
    static std::vector<SpatialRegion> create_grid(
        double x_min, double x_max, double y_min, double y_max,
        int num_ranks
    );

    /**
     * Assign each point to a rank based on which region it falls in.
     * Returns vector of size num_ranks, each containing the points for that rank.
     */
    static std::vector<std::vector<pdc_geo::Point>> partition_points(
        const std::vector<pdc_geo::Point>& points,
        const std::vector<SpatialRegion>& regions
    );

    /**
     * Filter polygons to only those whose bounding box intersects the region.
     * Applies a 5% overlap buffer on each side to handle boundary polygons.
     */
    static std::vector<pdc_geo::Polygon> filter_polygons_for_region(
        const std::vector<pdc_geo::Polygon>& polygons,
        const SpatialRegion& region
    );

    /**
     * O(1) rank lookup for a point given strip regions.
     * Returns the rank index, clamped to [0, num_ranks-1].
     */
    static int assign_rank(
        const pdc_geo::Point& p,
        double x_min, double x_max,
        int num_ranks
    );
};

}  // namespace pdc_mpi
