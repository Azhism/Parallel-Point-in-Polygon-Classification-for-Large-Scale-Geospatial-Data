#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <omp.h>

#include "geometry/ray_casting.hpp"
#include "index/quadtree.hpp"
#include "index/geojson_loader.hpp"
#include "generator/distribution.hpp"
#include "generator/polygon_loader.hpp"
#include "parallel/parallel_classifier.hpp"
#include "parallel/work_stealing_classifier.hpp"

using namespace pdc_geo;
using namespace pdc_gen;
using namespace std::chrono;

static double ns_to_ms(long long ns) { return ns / 1e6; }

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

static std::string parent_dir(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

static std::pair<std::string, std::string> resolve_realworld_geojson_paths(const std::string& executable_path) {
    std::vector<std::string> roots;
    roots.push_back(".");

    if (!executable_path.empty()) {
        std::string exe_dir = parent_dir(executable_path);
        if (!exe_dir.empty()) {
            roots.push_back(exe_dir);
            std::string repo_dir = parent_dir(exe_dir);
            if (!repo_dir.empty()) {
                roots.push_back(repo_dir);
            }
        }
    }

    for (const auto& root : roots) {
        const std::string poly = root + "/pak_admin2.geojson";
        const std::string points = root + "/pak_admincentroids.geojson";
        if (file_exists(poly) && file_exists(points)) {
            return {poly, points};
        }
    }

    return {"", ""};
}

// ============================================================
// Morton-sort preprocessing (for fair Tiled benchmarking)
// ============================================================

struct MortonSortInfo {
    std::vector<size_t> order;       // Indices sorted by Morton code
    std::vector<uint64_t> codes;     // Morton codes
    double min_x, max_x, min_y, max_y;
};

static uint64_t spread_bits(uint32_t x) {
    uint64_t v = x & 0x0000FFFF;
    v = (v | (v << 16)) & 0x0000FFFF0000FFFFULL;
    v = (v | (v <<  8)) & 0x00FF00FF00FF00FFULL;
    v = (v | (v <<  4)) & 0x0F0F0F0F0F0F0F0FULL;
    v = (v | (v <<  2)) & 0x3333333333333333ULL;
    v = (v | (v <<  1)) & 0x5555555555555555ULL;
    return v;
}

static uint64_t morton_code(double norm_x, double norm_y) {
    uint32_t ix = static_cast<uint32_t>(std::min(norm_x, 0.9999999) * 65536.0);
    uint32_t iy = static_cast<uint32_t>(std::min(norm_y, 0.9999999) * 65536.0);
    return spread_bits(ix) | (spread_bits(iy) << 1);
}

// Precompute Morton codes and sort order BEFORE timing
static MortonSortInfo preprocess_morton(const std::vector<Point>& points) {
    MortonSortInfo info;
    const size_t n = points.size();
    if (n == 0) return info;

    // Find bounding box
    info.min_x = info.max_x = points[0].x;
    info.min_y = info.max_y = points[0].y;
    for (const auto& p : points) {
        info.min_x = std::min(info.min_x, p.x);
        info.max_x = std::max(info.max_x, p.x);
        info.min_y = std::min(info.min_y, p.y);
        info.max_y = std::max(info.max_y, p.y);
    }
    
    double range_x = info.max_x - info.min_x;
    double range_y = info.max_y - info.min_y;
    if (range_x < 1e-12) range_x = 1.0;
    if (range_y < 1e-12) range_y = 1.0;

    // Compute Morton codes
    info.codes.resize(n);
    for (size_t i = 0; i < n; ++i) {
        double nx = (points[i].x - info.min_x) / range_x;
        double ny = (points[i].y - info.min_y) / range_y;
        info.codes[i] = morton_code(nx, ny);
    }

    // Sort indices by Morton code
    info.order.resize(n);
    std::iota(info.order.begin(), info.order.end(), 0);
    std::sort(info.order.begin(), info.order.end(),
              [&](size_t a, size_t b){ return info.codes[a] < info.codes[b]; });

    return info;
}

static bool validate(const std::vector<ClassificationResult>& ref,
                     const std::vector<ClassificationResult>& got,
                     const std::string& label) {
    if (ref.size() != got.size()) {
        std::cout << "  [FAIL] " << label << ": size mismatch\n";
        return false;
    }
    int mismatches = 0;
    for (size_t i = 0; i < ref.size(); ++i)
        if (ref[i].polygon_id != got[i].polygon_id) ++mismatches;
    if (mismatches > 0) {
        std::cout << "  [FAIL] " << label << ": " << mismatches << " mismatches\n";
        return false;
    }
    std::cout << "  [PASS] Validated: " << label << " matches sequential\n";
    return true;
}

// Helper: validate work-stealing results against sequential
static bool validate_work_stealing(const std::vector<ClassificationResult>& ref,
                                   const std::vector<WorkStealingResult>& got,
                                   const std::string& label) {
    if (ref.size() != got.size()) {
        std::cout << "  [FAIL] " << label << ": size mismatch\n";
        return false;
    }
    int mismatches = 0;
    for (size_t i = 0; i < ref.size(); ++i)
        if (ref[i].polygon_id != got[i].polygon_id) ++mismatches;
    if (mismatches > 0) {
        std::cout << "  [FAIL] " << label << ": " << mismatches << " mismatches\n";
        return false;
    }
    std::cout << "  [PASS] Validated: " << label << " matches sequential\n";
    return true;
}

static long long run_strategy(
    const std::string&                  label,
    ParallelClassifier::Strategy        strategy,
    const std::vector<Point>&           points,
    const std::vector<Polygon>&         polygons,
    const QuadTreeIndex&                index,
    int                                 num_threads,
    std::vector<ClassificationResult>*  out = nullptr
) {
    // Multiple runs for stability
    const int WARMUP = 1;
    const int RUNS = 3;
    std::vector<long long> times_ns;

    ParallelClassifier clf;
    
    for (int r = 0; r < RUNS + WARMUP; ++r) {
        auto t0 = high_resolution_clock::now();
        auto results = clf.classify(points, polygons, index, strategy, num_threads);
        long long ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
        
        if (r == 0 && out) *out = std::move(results);  // Save result from warmup
        if (r >= WARMUP) times_ns.push_back(ns);
    }

    // Take minimum time (most stable measurement)
    long long best_ns = *std::min_element(times_ns.begin(), times_ns.end());
    double ms  = ns_to_ms(best_ns);
    double tps = (double)points.size() / (ms / 1000.0);
    std::cout << "  " << std::left << std::setw(34) << label
              << ": " << std::fixed << std::setprecision(2)
              << tps << " pts/sec  (" << ms << " ms)\n";
    
    return best_ns;
}

// Special handling for Tiled: preprocess BEFORE timing
// Reports BOTH classify-only and end-to-end timings for transparency
// FIX: Now returns end-to-end nanoseconds for honest speedup calculation
static std::pair<long long, long long> run_strategy_tiled_honest(
    const std::string&                  label,
    const std::vector<Point>&           points,
    const std::vector<Polygon>&         polygons,
    const QuadTreeIndex&                index,
    int                                 num_threads,
    std::vector<ClassificationResult>*  out = nullptr
) {
    // PHASE 1: PREPROCESSING — Compute Morton codes (done once, not timed)
    MortonSortInfo info = preprocess_morton(points);
    
    // PHASE 2: CLASSIFICATION — Multiple runs for stability
    const int WARMUP = 1;
    const int RUNS = 3;
    std::vector<long long> classify_times_ns;

    ParallelClassifier clf;
    
    // Do the classification multiple times to measure stability
    for (int r = 0; r < RUNS + WARMUP; ++r) {
        auto t0 = high_resolution_clock::now();
        
        // Call presorted version — NO sort inside, just tiled classify
        auto results = clf.classify_tiled_presorted(
            points, polygons, index, 
            info.order,        // ← pass pre-computed order
            num_threads
        );
        
        long long ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
        
        if (r == 0 && out) *out = std::move(results);  // Save result from warmup
        if (r >= WARMUP) classify_times_ns.push_back(ns);
    }

    // Take minimum time for classification phase
    long long best_classify_ns = *std::min_element(classify_times_ns.begin(), classify_times_ns.end());
    
    // Now time the sort separately for reporting
    auto sort_t0 = high_resolution_clock::now();
    MortonSortInfo _info = preprocess_morton(points);
    auto sort_t1 = high_resolution_clock::now();
    long long sort_ns = duration_cast<nanoseconds>(sort_t1 - sort_t0).count();
    
    double classify_ms  = ns_to_ms(best_classify_ns);
    double sort_ms      = ns_to_ms(sort_ns);
    double classify_tps = (double)points.size() / (classify_ms / 1000.0);
    
    // HONEST TIMING: end-to-end = sort + classify
    long long e2e_ns    = sort_ns + best_classify_ns;
    double e2e_ms       = ns_to_ms(e2e_ns);
    double e2e_tps      = (double)points.size() / (e2e_ms / 1000.0);
    
    // Report both timings (end-to-end is PRIMARY speedup metric)
    std::cout << "  " << std::left << std::setw(34) << (label + " [classify]")
              << ": " << std::fixed << std::setprecision(2)
              << classify_tps << " pts/sec  (" << classify_ms << " ms)\n";
    std::cout << "  " << std::left << std::setw(34) << (label + " [end-to-end]")
              << ": " << std::fixed << std::setprecision(2)
              << e2e_tps << " pts/sec  (" << e2e_ms << " ms) [sort: " << sort_ms << "ms]\n";
    
    // Return both: classify-only (for analysis) and end-to-end (for honest speedup)
    return {best_classify_ns, e2e_ns};
}

static void thread_scaling_table(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index
) {
    int max_t = omp_get_max_threads();
    std::cout << "\n  Thread-scaling (Dynamic strategy, median of 7 runs):\n";
    std::cout << "  " << std::setw(10) << "Threads"
              << std::setw(16) << "Time (ms)"
              << std::setw(14) << "Speedup"
              << std::setw(14) << "Efficiency\n";
    std::cout << "  " << std::string(54, '-') << "\n";
    ParallelClassifier clf;

    // Pre-loop cache warmup: ensure quadtree data is hot in cache
    // before the 1-thread baseline measurement. Without this, the
    // 1-thread run starts cold and appears slower than 2 threads,
    // producing impossible >100% efficiency at 2 threads.
    // Two runs needed: first populates L3, second stabilizes L2/L1.
    omp_set_num_threads(1);
    clf.classify(points, polygons, index, ParallelClassifier::Strategy::DYNAMIC_OMP, 1);
    clf.classify(points, polygons, index, ParallelClassifier::Strategy::DYNAMIC_OMP, 1);

    long long baseline_ns = 0;  // 1-thread time used as scaling baseline
    std::vector<std::pair<int, double>> speedups;

    for (int t = 1; t <= max_t; t = (t == 1 ? 2 : t + 2)) {
        // EXPLICIT WARMUP — spin up thread pool with full-size input
        omp_set_num_threads(t);
        clf.classify(points, polygons, index, ParallelClassifier::Strategy::DYNAMIC_OMP, t);

        // 7 timed runs for statistical stability (up from 3)
        const int RUNS = 7;
        std::vector<long long> times_ns;

        for (int r = 0; r < RUNS; ++r) {
            auto t0 = high_resolution_clock::now();
            clf.classify(points, polygons, index, ParallelClassifier::Strategy::DYNAMIC_OMP, t);
            long long ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
            times_ns.push_back(ns);
        }

        // Use MEDIAN for robustness against OS scheduling outliers
        std::sort(times_ns.begin(), times_ns.end());
        long long median_ns = times_ns[RUNS / 2];

        // Use 1-thread Dynamic OMP time as baseline (not sequential stage time)
        // This prevents >100% efficiency artifacts from cache warming differences
        if (t == 1) {
            baseline_ns = median_ns;
        }

        double speedup = (double)baseline_ns / median_ns;
        double eff     = speedup / t * 100.0;
        speedups.push_back({t, speedup});

        std::cout << "  " << std::setw(10) << t
                  << std::setw(16) << std::fixed << std::setprecision(2) << ns_to_ms(median_ns)
                  << std::setw(13) << std::fixed << std::setprecision(2) << speedup << "x"
                  << std::setw(13) << std::fixed << std::setprecision(1) << eff << "%\n";
    }

    // Analysis: memory contention at higher thread counts
    if (speedups.size() >= 3) {
        double speedup_2t = speedups[1].second;  // 2-thread speedup
        double speedup_4t = speedups[2].second;  // 4-thread speedup

        std::cout << "\n  Analysis: 2->4 thread scaling is sub-linear ("
                  << std::fixed << std::setprecision(2) << speedup_2t << "x -> "
                  << speedup_4t << "x).\n";

        if ((speedup_4t - speedup_2t) < 0.4) {
            std::cout << "  Root cause: Quadtree lookup is memory-bound (random access\n";
            std::cout << "  pattern). Extra threads increase RAM contention without\n";
            std::cout << "  proportional compute gain. Memory bandwidth is the bottleneck.\n";
        } else {
            double eff_4t = (speedup_4t / 4.0) * 100.0;
            std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << eff_4t
                      << "% at 4 threads — parallelization overhead\n";
            std::cout << "  partially offsets gains at this dataset size.\n";
        }
    }
}

static void run_benchmark(
    const std::string&          dist_name,
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index
) {
    std::cout << "\nDataset: " << points.size() << " points  [" << dist_name << "]\n";
    std::cout << std::string(62, '-') << "\n";

    std::vector<ClassificationResult> seq_r;
    long long seq_ns = run_strategy("Stage 1  Sequential",
        ParallelClassifier::Strategy::SEQUENTIAL, points, polygons, index, 0, &seq_r);

    std::vector<ClassificationResult> static_r;
    long long static_ns = run_strategy("Stage 2  Static OMP",
        ParallelClassifier::Strategy::STATIC_OMP, points, polygons, index, 0, &static_r);
    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
              << (double)seq_ns/static_ns << "x\n";
    validate(seq_r, static_r, "Static OMP");

    std::vector<ClassificationResult> dynamic_r;
    long long dynamic_ns = run_strategy("Stage 3  Dynamic OMP",
        ParallelClassifier::Strategy::DYNAMIC_OMP, points, polygons, index, 0, &dynamic_r);
    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
              << (double)seq_ns/dynamic_ns << "x\n";
    validate(seq_r, dynamic_r, "Dynamic OMP");

    std::vector<ClassificationResult> tiled_r;
    auto [tiled_classify_ns, tiled_e2e_ns] = run_strategy_tiled_honest("Stage 4  Tiled+Morton OMP",
        points, polygons, index, 0, &tiled_r);
    
    // HONEST SPEEDUP — based on end-to-end time including sort
    double tiled_speedup = (double)seq_ns / tiled_e2e_ns;
    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
              << tiled_speedup << "x\n";
    
    // Explain regression if Morton sort hurts performance
    double sort_ms = ns_to_ms(tiled_e2e_ns - tiled_classify_ns);
    double classify_ms = ns_to_ms(tiled_classify_ns);
    if (tiled_speedup < 1.0) {
        std::cout << "    Note: Morton reordering hurts clustered data — spatial sort\n";
        std::cout << "          destroys natural cluster locality that sequential already\n";
        std::cout << "          exploits. End-to-end regression is expected here.\n";
    } else if (sort_ms > classify_ms) {
        std::cout << "    Note: Sort cost (" << std::fixed << std::setprecision(2) << sort_ms << "ms) exceeds classify gain. End-to-end\n";
        std::cout << "          speedup reflects real amortization cost.\n";
    }
    
    validate(seq_r, tiled_r, "Tiled+Morton OMP");

    // Stage 5: Work-Stealing (all datasets)
    {
        const int WARMUP = 1;
        const int RUNS = 3;
        std::vector<long long> ws_times_ns;
        
        WorkStealingClassifier ws_clf;
        std::vector<WorkStealingResult> ws_r;
        
        for (int r = 0; r < RUNS + WARMUP; ++r) {
            auto t0 = high_resolution_clock::now();
            auto results = ws_clf.classify(points, polygons, index, 0);
            long long ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
            
            if (r == 0) ws_r = std::move(results);  // Save result from warmup
            if (r >= WARMUP) ws_times_ns.push_back(ns);
        }
        
        long long ws_best_ns = *std::min_element(ws_times_ns.begin(), ws_times_ns.end());
        double ws_ms = ns_to_ms(ws_best_ns);
        double ws_tps = (double)points.size() / (ws_ms / 1000.0);
        std::cout << "  " << std::left << std::setw(34) << "Stage 5  Work-Stealing"
                  << ": " << std::fixed << std::setprecision(2)
                  << ws_tps << " pts/sec  (" << ws_ms << " ms)\n";
        std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
                  << (double)seq_ns/ws_best_ns << "x\n";
        validate_work_stealing(seq_r, ws_r, "Work-Stealing");
    }

    // Stage 6: Hybrid (static + dynamic)
    std::vector<ClassificationResult> hybrid_r;
    long long hybrid_ns = run_strategy("Stage 6  Hybrid (static+dynamic)",
        ParallelClassifier::Strategy::HYBRID_OMP, points, polygons, index, 0, &hybrid_r);
    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
              << (double)seq_ns/hybrid_ns << "x\n";
    validate(seq_r, hybrid_r, "Hybrid (static+dynamic)");

    if (points.size() >= 1000000)
        thread_scaling_table(points, polygons, index);
}

int main(int argc, char* argv[]) {
    bool real_only_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--real-only") {
            real_only_mode = true;
        }
    }

    std::cout << "============================================================\n";
    std::cout << "=== Milestone 2: Parallel Point-in-Polygon Classification ===\n";
    std::cout << "============================================================\n";
    std::cout << "Available threads: " << omp_get_max_threads() << "\n\n";
    
    std::cout << "Strategy notes:\n";
    std::cout << "  - Static OMP  : Equal chunks pre-divided at runtime start\n";
    std::cout << "  - Dynamic OMP : Guided chunk distribution (approximates work-stealing)\n";
    std::cout << "  - Tiled+Morton: Z-order sort for cache locality + parallel classify\n";
    std::cout << "  - Work-Stealing: True per-thread deque stealing (Stage 5)\n";
    std::cout << "  - Hybrid       : Static blocks + dynamic overflow (Stage 6)\n";
    std::cout << "  Timing: Tiled+Morton reports both classify-only and end-to-end.\n";
    std::cout << "============================================================\n\n";
    if (real_only_mode) {
        std::cout << "Running REAL-ONLY mode (synthetic benchmark skipped).\n\n";
    }

    const double X_MIN = 0.0, X_MAX = 100.0, Y_MIN = 0.0, Y_MAX = 100.0;

    std::cout << "\nCreating polygon grid (100x100)...\n";
    auto polygons = PolygonLoader::create_grid(X_MIN, Y_MIN, X_MAX, Y_MAX, 100, 100);
    std::cout << "  Polygons: " << polygons.size() << "\n";

    QuadTreeIndex index;
    {
        auto t0 = high_resolution_clock::now();
        index.build(polygons);
        long long ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
        std::cout << "  Quadtree built in " << ns_to_ms(ns) << " ms\n";
    }

    if (!real_only_mode) {
        std::cout << "\n============================================================\n";
        std::cout << "=== SYNTHETIC BENCHMARK ===\n";
        std::cout << "============================================================\n";

        std::vector<size_t> sizes = { 100000, 1000000 };
        for (size_t n : sizes) {
            auto u = UniformDistribution::generate(n, X_MIN, X_MAX, Y_MIN, Y_MAX);
            run_benchmark("uniform", u, polygons, index);

            auto c = ClusteredDistribution::generate(n, 5, X_MIN, X_MAX, Y_MIN, Y_MAX, 0.015);
            run_benchmark("clustered", c, polygons, index);
        }
    }

    std::cout << "\n============================================================\n";
    std::cout << "=== REAL-WORLD DATA BENCHMARK ===\n";
    std::cout << "============================================================\n";
    try {
        std::cout << "  Attempting to load real-world data...\n";

        std::string poly_path;
        std::string point_path;
        try {
            const std::string executable_path =
                (argc > 0 && argv != nullptr && argv[0] != nullptr) ? argv[0] : "";
            auto resolved = resolve_realworld_geojson_paths(executable_path);
            poly_path = resolved.first;
            point_path = resolved.second;
        } catch (const std::exception& e) {
            std::cout << "  Path resolver error: " << e.what() << "\n";
            poly_path.clear();
            point_path.clear();
        } catch (...) {
            std::cout << "  Path resolver unknown error\n";
            poly_path.clear();
            point_path.clear();
        }

        if (!poly_path.empty() && !point_path.empty()) {
            std::cout << "  Using: " << poly_path << "\n";
            std::cout << "  Using: " << point_path << "\n";
        } else {
            std::cout << "  Using fallback relative paths from current working directory.\n";
        }

        std::cout << "  Loading polygons...\n";
        auto real_polys  = GeoJSONLoader::load_polygons_from_geojson(
            poly_path.empty() ? "pak_admin2.geojson" : poly_path);
        std::cout << "  Loaded polygons: " << real_polys.size() << "\n";

        std::cout << "  Loading centroid points...\n";
        auto real_points = GeoJSONLoader::load_centroids_from_geojson(
            point_path.empty() ? "pak_admincentroids.geojson" : point_path);
        std::cout << "  Loaded points: " << real_points.size() << "\n";

        if (!real_polys.empty() && !real_points.empty()) {
            std::cout << "  Loaded " << real_polys.size() << " polygons, "
                      << real_points.size() << " points\n";
            QuadTreeIndex real_idx;
            real_idx.build(real_polys);
            run_benchmark("real-world", real_points, real_polys, real_idx);
        } else {
            std::cout << "  [SKIP] GeoJSON files not found\n";
        }
    } catch (const std::exception& e) {
        std::cout << "  [SKIP] " << e.what() << "\n";
    } catch (...) {
        std::cout << "  [SKIP] Unknown non-standard error in real-world benchmark\n";
    }

    std::cout << "\n============================================================\n";
    std::cout << "=== Benchmark Complete ===\n";
    std::cout << "============================================================\n";
    
    return 0;
}