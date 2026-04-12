#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include "index/quadtree.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace pdc_geo {

/**
 * Result for a single point classification.
 */
struct ClassificationResult {
    uint64_t point_index;   // index into original points array
    uint64_t polygon_id;    // matched polygon id, or UINT64_MAX if none
};

/**
 * Parallel point-in-polygon classifier.
 *
 * Provides four strategies of increasing sophistication:
 *
 *  1. classify_sequential  — single-threaded baseline (Week 1 logic)
 *  2. classify_static      — omp parallel for, static scheduling
 *  3. classify_dynamic     — omp parallel for, dynamic scheduling (fixes skew)
 *  4. classify_tiled       — spatial grid tiling + Morton-sorted points
 *                            for cache locality and balanced work per thread
 */
class ParallelClassifier {
public:
    /**
     * Strategy tag — passed to classify() for unified benchmarking.
     */
    enum class Strategy {
        SEQUENTIAL,
        STATIC_OMP,
        DYNAMIC_OMP,
        TILED,
        HYBRID_OMP
    };

    static std::string strategy_name(Strategy s) {
        switch (s) {
            case Strategy::SEQUENTIAL:  return "Sequential";
            case Strategy::STATIC_OMP:  return "Parallel (static)";
            case Strategy::DYNAMIC_OMP: return "Parallel (dynamic)";
            case Strategy::TILED:       return "Parallel (tiled+sorted)";
            case Strategy::HYBRID_OMP:  return "Parallel (hybrid)";
        }
        return "Unknown";
    }

    /**
     * Unified entry point — dispatches to the chosen strategy.
     *
     * @param points      input GPS points
     * @param polygons    polygon dataset
     * @param index       pre-built QuadTreeIndex
     * @param strategy    which parallel strategy to use
     * @param num_threads thread count (0 = use OMP_NUM_THREADS / hardware)
     * @return            per-point classification results
     */
    std::vector<ClassificationResult> classify(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        Strategy                    strategy,
        int                         num_threads = 0
    ) const;

    // ----------------------------------------------------------------
    // Individual strategy implementations (public for direct testing)
    // ----------------------------------------------------------------

    std::vector<ClassificationResult> classify_sequential(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index
    ) const;

    std::vector<ClassificationResult> classify_static(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        int                         num_threads
    ) const;

    std::vector<ClassificationResult> classify_dynamic(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        int                         num_threads
    ) const;

    std::vector<ClassificationResult> classify_tiled(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        int                         num_threads
    ) const;

    std::vector<ClassificationResult> classify_hybrid(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        int                         num_threads
    ) const;

    /**
     * Tiled strategy with pre-sorted points (optimised for preprocessing).
     * Accepts pre-computed Morton sort order to skip internal sorting.
     *
     * @param points      input GPS points
     * @param polygons    polygon dataset
     * @param index       pre-built QuadTreeIndex
     * @param order       pre-sorted indices (from preprocess_morton)
     * @param num_threads thread count
     * @return            per-point classification results
     */
    std::vector<ClassificationResult> classify_tiled_presorted(
        const std::vector<Point>&   points,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index,
        const std::vector<size_t>&  order,
        int                         num_threads
    ) const;

private:
    // ----------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------

    /**
     * Classify a single point against polygon candidates from the index.
     * Returns matching polygon id or UINT64_MAX.
     */
    uint64_t classify_one(
        const Point&                p,
        const std::vector<Polygon>& polygons,
        const QuadTreeIndex&        index
    ) const;

    /**
     * Compute Morton (Z-order) code for a point normalised to [0, 1]^2.
     * Used to sort points for cache-friendly spatial locality.
     */
    static uint64_t morton_code(double norm_x, double norm_y);

    /**
     * Interleave a 32-bit integer's bits (for Morton code).
     */
    static uint64_t spread_bits(uint32_t x);
};

}  // namespace pdc_geo
