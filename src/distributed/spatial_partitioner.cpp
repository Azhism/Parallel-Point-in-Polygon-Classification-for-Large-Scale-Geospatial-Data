#include "distributed/spatial_partitioner.hpp"
#include <cmath>
#include <algorithm>

namespace pdc_mpi {

std::vector<SpatialRegion> SpatialPartitioner::create_strips(
    double x_min, double x_max, double y_min, double y_max,
    int num_ranks
) {
    std::vector<SpatialRegion> regions(num_ranks);
    double strip_width = (x_max - x_min) / num_ranks;

    for (int i = 0; i < num_ranks; ++i) {
        regions[i].x_min = x_min + i * strip_width;
        regions[i].x_max = (i == num_ranks - 1) ? x_max : x_min + (i + 1) * strip_width;
        regions[i].y_min = y_min;
        regions[i].y_max = y_max;
    }
    return regions;
}

std::vector<SpatialRegion> SpatialPartitioner::create_grid(
    double x_min, double x_max, double y_min, double y_max,
    int num_ranks
) {
    // Factor num_ranks into rows * cols as close to square as possible
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(num_ranks))));
    int rows = (num_ranks + cols - 1) / cols;

    double col_width = (x_max - x_min) / cols;
    double row_height = (y_max - y_min) / rows;

    std::vector<SpatialRegion> regions;
    regions.reserve(num_ranks);

    for (int r = 0; r < rows && static_cast<int>(regions.size()) < num_ranks; ++r) {
        for (int c = 0; c < cols && static_cast<int>(regions.size()) < num_ranks; ++c) {
            SpatialRegion reg;
            reg.x_min = x_min + c * col_width;
            reg.x_max = (c == cols - 1) ? x_max : x_min + (c + 1) * col_width;
            reg.y_min = y_min + r * row_height;
            reg.y_max = (r == rows - 1) ? y_max : y_min + (r + 1) * row_height;
            regions.push_back(reg);
        }
    }
    return regions;
}

std::vector<std::vector<pdc_geo::Point>> SpatialPartitioner::partition_points(
    const std::vector<pdc_geo::Point>& points,
    const std::vector<SpatialRegion>& regions
) {
    int num_ranks = static_cast<int>(regions.size());
    std::vector<std::vector<pdc_geo::Point>> partitioned(num_ranks);

    // Pre-compute strip width for O(1) assignment if regions are strips
    // For general regions, fall back to linear scan
    double x_min = regions[0].x_min;
    double x_max = regions[num_ranks - 1].x_max;

    // Check if all regions share the same y_min/y_max (strip layout)
    bool is_strip = true;
    for (int i = 1; i < num_ranks; ++i) {
        if (regions[i].y_min != regions[0].y_min || regions[i].y_max != regions[0].y_max) {
            is_strip = false;
            break;
        }
    }

    if (is_strip) {
        // O(1) per point for strip layout
        double strip_width = (x_max - x_min) / num_ranks;
        for (const auto& p : points) {
            int rank = static_cast<int>((p.x - x_min) / strip_width);
            rank = std::clamp(rank, 0, num_ranks - 1);
            partitioned[rank].push_back(p);
        }
    } else {
        // General case: find containing region
        for (const auto& p : points) {
            for (int r = 0; r < num_ranks; ++r) {
                if (regions[r].contains(p)) {
                    partitioned[r].push_back(p);
                    break;
                }
            }
            // Points outside all regions go to nearest (last rank as fallback)
        }
    }

    return partitioned;
}

std::vector<pdc_geo::Polygon> SpatialPartitioner::filter_polygons_for_region(
    const std::vector<pdc_geo::Polygon>& polygons,
    const SpatialRegion& region
) {
    // Expand region by 5% on each side for boundary overlap
    double width = region.x_max - region.x_min;
    double height = region.y_max - region.y_min;
    double x_buffer = width * 0.05;
    double y_buffer = height * 0.05;

    SpatialRegion expanded;
    expanded.x_min = region.x_min - x_buffer;
    expanded.x_max = region.x_max + x_buffer;
    expanded.y_min = region.y_min - y_buffer;
    expanded.y_max = region.y_max + y_buffer;

    std::vector<pdc_geo::Polygon> filtered;
    for (const auto& poly : polygons) {
        if (expanded.intersects(poly.bbox)) {
            filtered.push_back(poly);
        }
    }
    return filtered;
}

int SpatialPartitioner::assign_rank(
    const pdc_geo::Point& p,
    double x_min, double x_max,
    int num_ranks
) {
    double strip_width = (x_max - x_min) / num_ranks;
    int rank = static_cast<int>((p.x - x_min) / strip_width);
    return std::clamp(rank, 0, num_ranks - 1);
}

}  // namespace pdc_mpi
