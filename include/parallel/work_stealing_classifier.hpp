#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include "index/quadtree.hpp"
#include <vector>
#include <cstdint>

namespace pdc_geo {

/**
 * Result for a single point classification.
 */
struct WorkStealingResult {
    uint64_t point_index;   // index into original points array
    uint64_t polygon_id;    // matched polygon id, or UINT64_MAX if none
};

/**
 * Work-Stealing Parallel Classifier.
 *
 * Implements true work-stealing with per-thread task deques.
 * Significantly better load balancing than static/dynamic for
 * non-uniform workloads (e.g., clustered point distributions).
 *
 * Algorithm:
 * 1. Divide points into M*T chunks (M multiplier, T thread count)
 * 2. Distribute chunks round-robin across thread queues
 * 3. Each thread:
 *    - Process its own queue front-to-back
 *    - When idle, steal from random thread's back
 *    - Continue until all queues empty
 */
class WorkStealingClassifier {
public:
    /**
     * Classify points using work-stealing parallelism.
     *
     * @param points      input GPS points
     * @param polygons    polygon dataset
     * @param index       pre-built QuadTreeIndex
     * @param num_threads thread count (0 = use OMP_NUM_THREADS / hardware)
     * @return            per-point classification results
     */
    std::vector<WorkStealingResult> classify(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        int                         num_threads = 0
    ) const;

private:
    /**
     * Helper: classify a single point
     */
    uint64_t classify_one(
        const Point&                p,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index
    ) const;
};

}  // namespace pdc_geo
