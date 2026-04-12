#include "parallel/parallel_classifier.hpp"
#include "geometry/ray_casting.hpp"

#include <omp.h>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>

namespace pdc_geo {

// ============================================================
// Unified dispatcher
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    Strategy                    strategy,
    int                         num_threads
) const {
    switch (strategy) {
        case Strategy::SEQUENTIAL:
            return classify_sequential(points, polygons, index);
        case Strategy::STATIC_OMP:
            return classify_static(points, polygons, index, num_threads);
        case Strategy::DYNAMIC_OMP:
            return classify_dynamic(points, polygons, index, num_threads);
        case Strategy::TILED:
            return classify_tiled(points, polygons, index, num_threads);
    }
    return {};
}

// ============================================================
// Internal helper: classify one point
// ============================================================

uint64_t ParallelClassifier::classify_one(
    const Point&                p,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index
) const {
    auto candidates = index.query_point(p);
    for (uint64_t pid : candidates) {
        if (pid < polygons.size()) {
            if (RayCaster::point_in_polygon(p, polygons[pid]) == RayCaster::Classification::INSIDE) {
                return pid;
            }
        }
    }
    return std::numeric_limits<uint64_t>::max();
}

// ============================================================
// Strategy 1: Sequential baseline
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify_sequential(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index
) const {
    std::vector<ClassificationResult> results(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        results[i] = { i, classify_one(points[i], polygons, index) };
    }
    return results;
}

// ============================================================
// Strategy 2: Static OpenMP parallelism
//
// Divides the points array into equal chunks, one per thread.
// 
// CRITICAL FIX (April 12, 2026): Use adaptive chunk size
// instead of default (n/threads) to avoid L3 cache thrashing.
//
// Root cause of slowdown with default scheduling:
//   Default chunk = 25K points (100K ÷ 4 threads)
//   Each 25K points hits different quadtree nodes
//   All 4 threads simultaneously evict each other from L3 cache
//   Dynamic wins because chunk=79 points → threads finish quickly
//   and interleave, reducing simultaneous cache pressure
//
// Solution: Use same chunk_size as dynamic (sqrt(n)/4)
// This gives Static the same cache efficiency as Dynamic
// while keeping scheduling cost low.
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify_static(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    int                         num_threads
) const {
    if (num_threads > 0) omp_set_num_threads(num_threads);

    std::vector<ClassificationResult> results(points.size());
    const int n = static_cast<int>(points.size());

    // FIX: Use adaptive chunk size to avoid L3 cache thrashing
    // Default static (no chunk size) gives 25K-point chunks per thread
    // that thrash the entire quadtree in L3 cache.
    // Solution: Use sqrt(n)/4 like dynamic, but with static scheduling
    // This reduces simultaneous cache pressure while staying deterministic.
    const int chunk_size = std::max(1, static_cast<int>(std::sqrt(n) / 4));

    // schedule(static, chunk_size) = deterministic scheduling with small chunks
    // Reduces L3 cache thrashing vs. default n/threads chunks
    // if(n > 50000): skip OpenMP overhead for small datasets (pure sequential is faster)
    #pragma omp parallel for schedule(static, chunk_size) if(n > 50000)
    for (int i = 0; i < n; ++i) {
        results[i] = { static_cast<uint64_t>(i),
                       classify_one(points[i], polygons, index) };
    }

    return results;
}

// ============================================================
// Strategy 3: Dynamic OpenMP scheduling
//
// Threads grab small chunks from a shared task queue.
// When a thread finishes its chunk it immediately grabs the next.
// No thread sits idle while others are overloaded.
//
// chunk_size tuning:
//   Too small → excessive queue contention (threads fight for chunks)
//   Too large → same imbalance as static
//   sqrt(n) is a good heuristic — tested on 100K and 1M
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify_dynamic(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    int                         num_threads
) const {
    if (num_threads > 0) omp_set_num_threads(num_threads);

    std::vector<ClassificationResult> results(points.size());
    const int n          = static_cast<int>(points.size());
    const int chunk_size = std::max(1, static_cast<int>(std::sqrt(n) / 4));

    // schedule(dynamic, chunk) = work-stealing style task queue
    // if(n > 50000): skip OpenMP overhead for small datasets (pure sequential is faster)
    #pragma omp parallel for schedule(dynamic, chunk_size) if(n > 50000)
    for (int i = 0; i < n; ++i) {
        results[i] = { static_cast<uint64_t>(i),
                       classify_one(points[i], polygons, index) };
    }

    return results;
}

// ============================================================
// Radix sort for Morton codes (HIGH-RADIX variant)
//
// Sorts indices by their Morton codes using radix sort.
// Much faster than std::sort for this use case:
//   - std::sort: O(n log n) comparisons
//   - Radix sort: O(k*n) where k=4 (we use 4 passes of 16-bit radix)
//   - Expected: ~8-10x faster for 1M points
//
// Technique: Process 16 bits at a time, 4 passes for full 64-bit codes
// Stable sort ensures deterministic ordering
// ============================================================

static void radix_sort_by_morton(
    std::vector<size_t>&         order,
    const std::vector<uint64_t>& codes)
{
    const size_t n = order.size();
    if (n < 2) return;  // Already sorted
    
    std::vector<size_t> temp(n);

    // Process 4 times: each pass sorts 16 bits (radix = 2^16 = 65536)
    for (int shift = 0; shift < 64; shift += 16) {
        // Count occurrences of each 16-bit value
        size_t count[65536] = {};
        
        for (size_t i = 0; i < n; i++) {
            uint16_t digit = (codes[order[i]] >> shift) & 0xFFFF;
            count[digit]++;
        }

        // Convert counts to cumulative positions
        for (int i = 1; i < 65536; i++)
            count[i] += count[i-1];

        // Place elements in sorted order (process backwards for stability)
        for (int i = (int)n - 1; i >= 0; i--) {
            uint16_t digit = (codes[order[i]] >> shift) & 0xFFFF;
            temp[--count[digit]] = order[i];
        }

        // Swap for next iteration
        std::swap(order, temp);
    }
}

// ============================================================
//
// Two-phase approach for maximum cache efficiency:
//
// PHASE 1 — Sort:
//   Assign each point a Morton (Z-order) code based on its
//   (x, y) position normalised to [0,1]^2.
//   Sort points by Morton code → spatially nearby points
//   end up adjacent in memory.
//   Result: when a thread processes point[i] and point[i+1],
//   they likely query the SAME quadtree leaf → the leaf's
//   polygon list is still warm in L1/L2 cache.
//
// PHASE 2 — Tile-parallel:
//   Divide the sorted array into TILE_COUNT spatial tiles.
//   Each OpenMP task handles one tile.
//   schedule(dynamic,1) on tiles gives work-stealing at
//   tile granularity — large enough to amortise scheduling
//   overhead, small enough to rebalance across threads.
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify_tiled(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    int                         num_threads
) const {
    if (num_threads > 0) omp_set_num_threads(num_threads);

    const size_t n = points.size();
    if (n == 0) return {};

    // ---- Phase 1: compute Morton codes and sort indices ----

    // Find bounding box of the point set for normalisation
    double min_x = points[0].x, max_x = points[0].x;
    double min_y = points[0].y, max_y = points[0].y;
    for (const auto& p : points) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    double range_x = max_x - min_x;
    double range_y = max_y - min_y;
    if (range_x < 1e-12) range_x = 1.0;
    if (range_y < 1e-12) range_y = 1.0;

    // Build index array sorted by Morton code
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), 0);

    std::vector<uint64_t> codes(n);
    for (size_t i = 0; i < n; ++i) {
        double nx = (points[i].x - min_x) / range_x;
        double ny = (points[i].y - min_y) / range_y;
        codes[i]  = morton_code(nx, ny);
    }

    // OPTIMIZATION: Use radix sort instead of std::sort
    // Expected: 8-10x faster than std::sort for 1M points
    // std::sort is O(n log n) ≈ 20M comparisons
    // radix sort is O(4*n) ≈ 4M operations (4 passes of 16-bit radix)
    radix_sort_by_morton(order, codes);

    // ---- Phase 2: tile-parallel classification ----

    // Tile size: target ~1024 points per tile for good cache reuse
    const int threads    = (num_threads > 0) ? num_threads : omp_get_max_threads();
    const int tile_size  = std::max(256, static_cast<int>(n) / (threads * 8));
    const int num_tiles  = static_cast<int>((n + tile_size - 1) / tile_size);

    std::vector<ClassificationResult> results(n);

    // Compute adaptive chunk size for tile-level scheduling
    // Avoid 1M+ scheduler round-trips: aim for ~500 dispatches on 1M points
    const int tile_chunk = std::max(1, num_tiles / (threads * 20));

    #pragma omp parallel for schedule(dynamic, tile_chunk) num_threads(threads) if(n > 50000)
    for (int t = 0; t < num_tiles; ++t) {
        int start = t * tile_size;
        int end   = std::min(start + tile_size, static_cast<int>(n));

        for (int i = start; i < end; ++i) {
            size_t orig_idx = order[i];
            results[orig_idx] = { orig_idx,
                                  classify_one(points[orig_idx], polygons, index) };
        }
    }

    return results;
}

// ============================================================
// Morton code helpers
//
// Interleave bits of x and y to produce a Z-order curve value.
// Points nearby in 2D space get nearby Morton codes, so
// sorting by Morton code clusters spatially-close points together.
// ============================================================

uint64_t ParallelClassifier::spread_bits(uint32_t x) {
    // Spread 16 bits of x into even bit positions
    // x = ---- ---- ---- ---- fedc ba98 7654 3210
    uint64_t v = x & 0x0000FFFF;
    v = (v | (v << 16)) & 0x0000FFFF0000FFFFULL;
    v = (v | (v <<  8)) & 0x00FF00FF00FF00FFULL;
    v = (v | (v <<  4)) & 0x0F0F0F0F0F0F0F0FULL;
    v = (v | (v <<  2)) & 0x3333333333333333ULL;
    v = (v | (v <<  1)) & 0x5555555555555555ULL;
    return v;
}

uint64_t ParallelClassifier::morton_code(double norm_x, double norm_y) {
    // Quantise to 16-bit resolution — gives 65536 x 65536 grid
    uint32_t ix = static_cast<uint32_t>(std::min(norm_x, 0.9999999) * 65536.0);
    uint32_t iy = static_cast<uint32_t>(std::min(norm_y, 0.9999999) * 65536.0);
    return (spread_bits(iy) << 1) | spread_bits(ix);
}

// ============================================================
// Strategy 4b: Tiled with pre-sorted order (no internal sort)
//
// Variant of classify_tiled that skips Phase 1 (sorting)
// because the caller already provided sorted order.
//
// This is the "fair" measurement — only tiles, no sort overhead.
// ============================================================

std::vector<ClassificationResult> ParallelClassifier::classify_tiled_presorted(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    const std::vector<size_t>&  order,
    int                         num_threads
) const {
    if (num_threads > 0) omp_set_num_threads(num_threads);

    const size_t n = points.size();
    if (n == 0) return {};

    // NO SORT HERE — order[] is already provided from preprocessing

    // ---- Tile-parallel classification ----

    const int threads   = (num_threads > 0) ? num_threads : omp_get_max_threads();
    const int tile_size = std::max(256, static_cast<int>(n) / (threads * 8));
    const int num_tiles = static_cast<int>((n + tile_size - 1) / tile_size);

    std::vector<ClassificationResult> results(n);

    // Compute adaptive chunk size for tile-level scheduling
    // Avoid 1M+ scheduler round-trips: aim for ~500 dispatches on 1M points
    const int tile_chunk = std::max(1, num_tiles / (threads * 20));

    #pragma omp parallel for schedule(dynamic, tile_chunk) num_threads(threads) if(n > 50000)
    for (int t = 0; t < num_tiles; ++t) {
        int start = t * tile_size;
        int end   = std::min(start + tile_size, static_cast<int>(n));

        for (int i = start; i < end; ++i) {
            size_t orig_idx = order[i];
            results[orig_idx] = { orig_idx,
                                  classify_one(points[orig_idx], polygons, index) };
        }
    }

    return results;
}

}  // namespace pdc_geo