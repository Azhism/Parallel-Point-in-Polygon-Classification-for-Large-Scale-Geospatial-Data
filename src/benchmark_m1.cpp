#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include "geometry/ray_casting.hpp"
#include "index/quadtree.hpp"
#include "index/strip_index.hpp"
#include "index/bbox_filter.hpp"
#include "index/geojson_loader.hpp"
#include "generator/distribution.hpp"
#include "generator/polygon_loader.hpp"

using namespace pdc_geo;
using namespace pdc_gen;
using namespace std::chrono;

// Pakistan geographic bounding box (WGS84 lon/lat)
static constexpr double PAK_LON_MIN = 60.87;
static constexpr double PAK_LON_MAX = 77.84;
static constexpr double PAK_LAT_MIN = 23.63;
static constexpr double PAK_LAT_MAX = 37.10;

// Query result: label which polygon(s) contain the point
struct QueryResult {
    uint64_t point_id;
    uint64_t polygon_id;  // ID of containing polygon, or UINT64_MAX if none
    
    QueryResult(uint64_t pid, uint64_t pgid) : point_id(pid), polygon_id(pgid) {}
};

struct LargeScaleMetrics {
    long long quadtree_build_ns = 0;
    long long strip_build_ns = 0;
    long long quadtree_query_ns = 0;
    long long strip_query_ns = 0;
    uint64_t mismatches = 0;
};

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

static std::string parent_dir(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

std::pair<std::string, std::string> resolve_realworld_geojson_paths(const std::string& executable_path) {
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

uint64_t classify_point_from_candidates(const Point& point,
                                        const std::vector<Polygon>& polygons,
                                        const std::vector<uint64_t>& candidates) {
    for (uint64_t poly_id : candidates) {
        const Polygon& poly = polygons[poly_id];
        auto classification = RayCaster::point_in_polygon(point, poly);
        if (classification == RayCaster::Classification::INSIDE ||
            classification == RayCaster::Classification::ON_BOUNDARY) {
            return poly.id;
        }
    }
    return UINT64_MAX;
}

// Benchmark stage 1: Brute-force with bounding-box filter (baseline)
std::pair<std::vector<QueryResult>, long long> benchmark_brute_force_with_bbox(
    const std::vector<Point>& points,
    const std::vector<Polygon>& polygons) {
    
    std::vector<QueryResult> results;
    auto start = high_resolution_clock::now();
    
    for (const auto& point : points) {
        // Get candidates via bbox filter
        auto candidates = BBoxFilter::get_candidates(point, polygons);
        
        uint64_t found_polygon = classify_point_from_candidates(point, polygons, candidates);
        results.emplace_back(point.id, found_polygon);
    }
    
    auto end = high_resolution_clock::now();
    long long elapsed_ns = duration_cast<nanoseconds>(end - start).count();
    
    return {results, elapsed_ns};
}

// Benchmark stage 2: With Quadtree index
std::pair<std::vector<QueryResult>, long long> benchmark_with_quadtree(
    const std::vector<Point>& points,
    const std::vector<Polygon>& polygons,
    long long* index_build_ns = nullptr) {
    
    // Build index
    auto index_start = high_resolution_clock::now();
    QuadTreeIndex index;
    index.build(polygons);
    auto index_end = high_resolution_clock::now();
    if (index_build_ns) *index_build_ns = duration_cast<nanoseconds>(index_end - index_start).count();
    
    std::vector<QueryResult> results;
    auto start = high_resolution_clock::now();
    
    for (const auto& point : points) {
        // Query Quadtree
        auto candidates = index.query_point(point);

        uint64_t found_polygon = classify_point_from_candidates(point, polygons, candidates);
        results.emplace_back(point.id, found_polygon);
    }
    
    auto end = high_resolution_clock::now();
    long long elapsed_ns = duration_cast<nanoseconds>(end - start).count();
    
    return {results, elapsed_ns};
}

// Benchmark stage 3: With Strip Index
std::pair<std::vector<QueryResult>, long long> benchmark_with_strip_index(
    const std::vector<Point>& points,
    const std::vector<Polygon>& polygons,
    long long* index_build_ns = nullptr) {
    
    // Build index
    auto index_start = high_resolution_clock::now();
    StripIndex index;
    index.build(polygons);
    auto index_end = high_resolution_clock::now();
    if (index_build_ns) *index_build_ns = duration_cast<nanoseconds>(index_end - index_start).count();
    
    std::vector<QueryResult> results;
    auto start = high_resolution_clock::now();
    
    for (const auto& point : points) {
        // Query Strip Index
        auto candidates = index.query_point(point);
        
        uint64_t found_polygon = classify_point_from_candidates(point, polygons, candidates);
        results.emplace_back(point.id, found_polygon);
    }
    
    auto end = high_resolution_clock::now();
    long long elapsed_ns = duration_cast<nanoseconds>(end - start).count();
    
    return {results, elapsed_ns};
}

// Validate results match between two methods
void validate_results(const std::vector<QueryResult>& results1,
                      const std::vector<QueryResult>& results2) {
    if (results1.size() != results2.size()) {
        std::cerr << "ERROR: Result size mismatch!" << std::endl;
        return;
    }
    
    int mismatches = 0;
    for (size_t i = 0; i < results1.size(); i++) {
        if (results1[i].point_id != results2[i].point_id ||
            results1[i].polygon_id != results2[i].polygon_id) {
            mismatches++;
        }
    }
    
    if (mismatches > 0) {
        std::cerr << "ERROR: " << mismatches << " result mismatches!" << std::endl;
    } else {
        std::cout << "✓ Results validated: all queries match." << std::endl;
    }
}

LargeScaleMetrics benchmark_large_scale_indexed(
    size_t num_points,
    const std::string& dist_type,
    const std::vector<Polygon>& polygons,
    double region_x_min,
    double region_x_max,
    double region_y_min,
    double region_y_max,
    size_t batch_size = 250000) {

    LargeScaleMetrics metrics;

    auto build_start_qt = high_resolution_clock::now();
    QuadTreeIndex quadtree;
    quadtree.build(polygons);
    auto build_end_qt = high_resolution_clock::now();
    metrics.quadtree_build_ns = duration_cast<nanoseconds>(build_end_qt - build_start_qt).count();

    auto build_start_strip = high_resolution_clock::now();
    StripIndex strip;
    strip.build(polygons);
    auto build_end_strip = high_resolution_clock::now();
    metrics.strip_build_ns = duration_cast<nanoseconds>(build_end_strip - build_start_strip).count();

    size_t processed = 0;
    while (processed < num_points) {
        size_t this_batch = std::min(batch_size, num_points - processed);

        std::vector<Point> points;
        if (dist_type == "uniform") {
            points = UniformDistribution::generate(
                this_batch, region_x_min, region_x_max, region_y_min, region_y_max
            );
        } else {
            points = ClusteredDistribution::generate(
                this_batch, 5,
                region_x_min, region_x_max, region_y_min, region_y_max,
                0.015
            );
        }

        std::vector<uint64_t> quadtree_result(points.size(), UINT64_MAX);

        auto query_start_qt = high_resolution_clock::now();
        for (size_t i = 0; i < points.size(); ++i) {
            auto candidates = quadtree.query_point(points[i]);
            quadtree_result[i] = classify_point_from_candidates(points[i], polygons, candidates);
        }
        auto query_end_qt = high_resolution_clock::now();
        metrics.quadtree_query_ns += duration_cast<nanoseconds>(query_end_qt - query_start_qt).count();

        auto query_start_strip = high_resolution_clock::now();
        for (size_t i = 0; i < points.size(); ++i) {
            auto candidates = strip.query_point(points[i]);
            uint64_t strip_result = classify_point_from_candidates(points[i], polygons, candidates);
            if (strip_result != quadtree_result[i]) {
                metrics.mismatches++;
            }
        }
        auto query_end_strip = high_resolution_clock::now();
        metrics.strip_query_ns += duration_cast<nanoseconds>(query_end_strip - query_start_strip).count();

        processed += this_batch;
    }

    return metrics;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Milestone 1: Sequential Baseline with Spatial Indexing ===" << std::endl;

    bool large_scale_mode = false;
    bool real_only_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--large" || arg == "--large-scale") {
            large_scale_mode = true;
        } else if (arg == "--real-only") {
            real_only_mode = true;
        }
    }
    
    // Parameters
    const double REGION_X_MIN = 0.0, REGION_X_MAX = 100.0;
    const double REGION_Y_MIN = 0.0, REGION_Y_MAX = 100.0;
    
    std::vector<size_t> point_counts = large_scale_mode
        ? std::vector<size_t>{10000000, 100000000}
        : std::vector<size_t>{100000, 1000000};
    std::vector<std::string> distributions = {"uniform", "clustered"};

    if (large_scale_mode) {
        std::cout << "Running LARGE-SCALE mode (10M-100M points, batched indexed processing)." << std::endl;
        std::cout << "Brute-force stage is skipped in this mode to keep runtime practical." << std::endl;
    }
    if (real_only_mode) {
        std::cout << "Running REAL-ONLY mode (synthetic benchmark skipped)." << std::endl;
    }
    
    // Create polygon grid (100x100 = 10,000 polygons)
    std::cout << "Creating polygon grid (100x100)..." << std::endl;
    auto polygons = PolygonLoader::create_grid(
        REGION_X_MIN, REGION_Y_MIN,
        REGION_X_MAX, REGION_Y_MAX,
        100, 100
    );
    std::cout << "  Polygons: " << polygons.size() << std::endl;
    
    if (!real_only_mode) {
        // Benchmark for each dataset
        for (const auto& dist_type : distributions) {
            std::cout << "=== " << dist_type << " distribution ===" << std::endl;
            
            for (size_t num_points : point_counts) {
            std::cout << "Dataset: " << num_points << " points" << std::endl;

            if (large_scale_mode) {
                auto metrics = benchmark_large_scale_indexed(
                    num_points,
                    dist_type,
                    polygons,
                    REGION_X_MIN,
                    REGION_X_MAX,
                    REGION_Y_MIN,
                    REGION_Y_MAX
                );

                double throughput_qt = (double)num_points / (metrics.quadtree_query_ns / 1e9);
                double throughput_strip = (double)num_points / (metrics.strip_query_ns / 1e9);

                std::cout << "  Stage 1 (Brute force + BBox): skipped in large-scale mode" << std::endl;
                std::cout << "  Stage 2 (Quadtree index): "
                          << std::fixed << std::setprecision(2)
                          << throughput_qt << " pts/sec ("
                          << (metrics.quadtree_query_ns / 1e6) << " ms)"
                          << " [build: " << (metrics.quadtree_build_ns / 1e6) << " ms]" << std::endl;

                std::cout << "  Stage 3 (Strip Index): "
                          << std::fixed << std::setprecision(2)
                          << throughput_strip << " pts/sec ("
                          << (metrics.strip_query_ns / 1e6) << " ms)"
                          << " [build: " << (metrics.strip_build_ns / 1e6) << " ms]" << std::endl;

                double strip_vs_qt = (double)metrics.quadtree_query_ns / metrics.strip_query_ns;
                std::cout << "    Speedup (Strip vs Quadtree): "
                          << std::fixed << std::setprecision(2) << strip_vs_qt << "x" << std::endl;

                if (metrics.mismatches > 0) {
                    std::cerr << "ERROR: " << metrics.mismatches
                              << " mismatches between Quadtree and Strip Index!" << std::endl;
                } else {
                    std::cout << "✓ Results validated: Quadtree and Strip Index match." << std::endl;
                }

                continue;
            }
            
            // Generate dataset
            std::vector<Point> points;
            if (dist_type == "uniform") {
                points = UniformDistribution::generate(
                    num_points, REGION_X_MIN, REGION_X_MAX, REGION_Y_MIN, REGION_Y_MAX
                );
            } else {
                points = ClusteredDistribution::generate(
                    num_points, 5,
                    REGION_X_MIN, REGION_X_MAX, REGION_Y_MIN, REGION_Y_MAX,
                    0.015  // cluster std dev
                );
            }
            
            // Benchmark: Brute force + bbox
            std::cout << "  Stage 1 (Brute force + BBox):";
            std::cout.flush();
            auto [results_brute, time_brute] = benchmark_brute_force_with_bbox(points, polygons);
            double throughput_brute = (double)num_points / (time_brute / 1e9);
            std::cout << " " << std::fixed << std::setprecision(2) 
                      << throughput_brute << " pts/sec ("
                      << (time_brute / 1e6) << " ms)" << std::endl;
            
            // Benchmark: Quadtree
            std::cout << "  Stage 2 (Quadtree index):";
            std::cout.flush();
            long long index_build_ns_qt;
            auto [results_quadtree, time_quadtree] = benchmark_with_quadtree(points, polygons, &index_build_ns_qt);
            double throughput_quadtree = (double)num_points / (time_quadtree / 1e9);
            std::cout << " " << std::fixed << std::setprecision(2)
                      << throughput_quadtree << " pts/sec ("
                      << (time_quadtree / 1e6) << " ms)"
                      << " [build: " << (index_build_ns_qt / 1e6) << " ms]" << std::endl;
            double speedup_qt = (double)time_brute / time_quadtree;
            std::cout << "    Speedup: " << std::fixed << std::setprecision(2) << speedup_qt << "x" << std::endl;
            
            // Benchmark: Strip Index
            std::cout << "  Stage 3 (Strip Index):";
            std::cout.flush();
            long long index_build_ns_strip;
            auto [results_strip, time_strip] = benchmark_with_strip_index(points, polygons, &index_build_ns_strip);
            double throughput_strip = (double)num_points / (time_strip / 1e9);
            std::cout << " " << std::fixed << std::setprecision(2)
                      << throughput_strip << " pts/sec ("
                      << (time_strip / 1e6) << " ms)"
                      << " [build: " << (index_build_ns_strip / 1e6) << " ms]" << std::endl;
            double speedup_strip = (double)time_brute / time_strip;
            std::cout << "    Speedup: " << std::fixed << std::setprecision(2) << speedup_strip << "x" << std::endl;
            
            // Validate
            validate_results(results_brute, results_quadtree);
            validate_results(results_brute, results_strip);

                std::cout << std::endl;
            }

        }
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "=== SYNTHETIC BENCHMARK COMPLETE ===" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // Stage 4: Real-world data benchmark
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "=== REAL-WORLD DATA BENCHMARK ===" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    try {
        // Auto-detect best available polygon file: Level 4 > Level 3 > Level 2
        struct PolyCandidate { std::string file; std::string label; };
        std::vector<PolyCandidate> poly_candidates = {
            {"pak_admin4.geojson", "Level 4 - Union Councils (~6,000 polygons)"},
            {"pak_admin3.geojson", "Level 3 - Tehsils (~600 loaded polygons)"},
            {"pak_admin2.geojson", "Level 2 - Districts (204 polygons)"},
        };

        std::string poly_path;
        std::string poly_label;
        for (const auto& c : poly_candidates) {
            if (file_exists(c.file)) {
                poly_path  = c.file;
                poly_label = c.label;
                break;
            }
        }

        if (poly_path.empty()) {
            std::cout << "⚠ No polygon file found. Place one of the following in the project root:" << std::endl;
            std::cout << "   pak_admin4.geojson  (GADM Level 4 — best for benchmarking)" << std::endl;
            std::cout << "   pak_admin3.geojson  (GADM Level 3)" << std::endl;
            std::cout << "   pak_admin2.geojson  (GADM Level 2 — already included)" << std::endl;
            std::cout << "   Download from: gadm.org → Pakistan → GeoJSON" << std::endl;
        } else {
            std::cout << "Polygon file : " << poly_path << std::endl;
            std::cout << "Admin level  : " << poly_label << std::endl;

            std::cout << "Loading polygons..." << std::endl;
            auto real_polygons = GeoJSONLoader::load_polygons_from_geojson(poly_path);
            std::cout << "  Loaded: " << real_polygons.size() << " polygons" << std::endl;

            if (real_polygons.empty()) {
                std::cout << "⚠ No polygons loaded — skipping real-world benchmark." << std::endl;
            } else {
                // Generate synthetic GPS points over Pakistan's geographic bounds.
                // These simulate real events (deliveries, phone signals, ride pings)
                // rather than the 745 administrative centroids we used before.
                const size_t REAL_N = 100000;
                std::cout << "Generating " << REAL_N
                          << " synthetic GPS points over Pakistan bounds "
                          << "[lon " << PAK_LON_MIN << "-" << PAK_LON_MAX
                          << ", lat " << PAK_LAT_MIN << "-" << PAK_LAT_MAX << "]"
                          << std::endl;

                std::vector<std::pair<std::string, std::vector<Point>>> real_datasets = {
                    {"uniform",
                     UniformDistribution::generate(
                         REAL_N,
                         PAK_LON_MIN, PAK_LON_MAX,
                         PAK_LAT_MIN, PAK_LAT_MAX)},
                    {"clustered",
                     ClusteredDistribution::generate(
                         REAL_N, 8,
                         PAK_LON_MIN, PAK_LON_MAX,
                         PAK_LAT_MIN, PAK_LAT_MAX,
                         0.8)},  // ~90 km std dev — mimics urban concentration
                };

                std::cout << "Benchmarking " << real_polygons.size()
                          << " real Pakistan polygons vs " << REAL_N
                          << " synthetic GPS points..." << std::endl;

                for (const auto& [dist_label, real_points] : real_datasets) {
                    std::cout << "\n=== real-world / " << dist_label
                              << " distribution ===" << std::endl;

                    // Stage 1: Brute force + BBox
                    std::cout << "  Stage 1 (Brute force + BBox):";
                    std::cout.flush();
                    auto [r_brute, t_brute] = benchmark_brute_force_with_bbox(real_points, real_polygons);
                    double tp_brute = (double)REAL_N / (t_brute / 1e9);
                    std::cout << " " << std::fixed << std::setprecision(2)
                              << tp_brute << " pts/sec ("
                              << (t_brute / 1e6) << " ms)" << std::endl;

                    // Stage 2: Quadtree
                    std::cout << "  Stage 2 (Quadtree index):";
                    std::cout.flush();
                    long long qt_build_ns;
                    auto [r_qt, t_qt] = benchmark_with_quadtree(real_points, real_polygons, &qt_build_ns);
                    double tp_qt = (double)REAL_N / (t_qt / 1e9);
                    std::cout << " " << std::fixed << std::setprecision(2)
                              << tp_qt << " pts/sec ("
                              << (t_qt / 1e6) << " ms)"
                              << " [build: " << (qt_build_ns / 1e6) << " ms]" << std::endl;
                    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
                              << ((double)t_brute / t_qt) << "x" << std::endl;

                    // Stage 3: Strip Index
                    std::cout << "  Stage 3 (Strip Index):";
                    std::cout.flush();
                    long long strip_build_ns;
                    auto [r_strip, t_strip] = benchmark_with_strip_index(real_points, real_polygons, &strip_build_ns);
                    double tp_strip = (double)REAL_N / (t_strip / 1e9);
                    std::cout << " " << std::fixed << std::setprecision(2)
                              << tp_strip << " pts/sec ("
                              << (t_strip / 1e6) << " ms)"
                              << " [build: " << (strip_build_ns / 1e6) << " ms]" << std::endl;
                    std::cout << "    Speedup: " << std::fixed << std::setprecision(2)
                              << ((double)t_brute / t_strip) << "x" << std::endl;

                    validate_results(r_brute, r_qt);
                    validate_results(r_brute, r_strip);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "⚠ Error in real-world benchmark: " << e.what() << std::endl;
        std::cout << "  Continuing with synthetic data results only." << std::endl;
    } catch (...) {
        std::cout << "⚠ Unknown error in real-world benchmark." << std::endl;
    }
    
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "=== Benchmark Complete ===" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    return 0;
}
