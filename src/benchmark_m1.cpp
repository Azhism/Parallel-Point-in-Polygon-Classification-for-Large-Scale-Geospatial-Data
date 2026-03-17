#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <vector>
#include "geometry/ray_casting.hpp"
#include "index/quadtree.hpp"
#include "index/bbox_filter.hpp"
#include "generator/distribution.hpp"
#include "generator/polygon_loader.hpp"

using namespace pdc_geo;
using namespace pdc_gen;
using namespace std::chrono;

// Query result: label which polygon(s) contain the point
struct QueryResult {
    uint64_t point_id;
    uint64_t polygon_id;  // ID of containing polygon, or UINT64_MAX if none
    
    QueryResult(uint64_t pid, uint64_t pgid) : point_id(pid), polygon_id(pgid) {}
};

// Benchmark stage 1: Brute-force with bounding-box filter (baseline)
std::pair<std::vector<QueryResult>, long long> benchmark_brute_force_with_bbox(
    const std::vector<Point>& points,
    const std::vector<Polygon>& polygons) {
    
    std::vector<QueryResult> results;
    auto start = high_resolution_clock::now();
    
    for (const auto& point : points) {
        // Get candidates via bbox filter
        auto candidates = BBoxFilter::get_candidates(point, polygons);
        
        // Check each candidate
        uint64_t found_polygon = UINT64_MAX;
        for (uint64_t poly_id : candidates) {
            const Polygon& poly = polygons[poly_id];
            if (RayCaster::point_in_polygon(point, poly) == RayCaster::Classification::INSIDE) {
                found_polygon = poly.id;
                break;  // Found first containing polygon
            }
        }
        
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
        
        // Check each candidate
        uint64_t found_polygon = UINT64_MAX;
        for (uint64_t poly_id : candidates) {
            const Polygon& poly = polygons[poly_id];
            if (RayCaster::point_in_polygon(point, poly) == RayCaster::Classification::INSIDE) {
                found_polygon = poly.id;
                break;
            }
        }
        
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

int main() {
    std::cout << "=== Milestone 1: Sequential Baseline with Spatial Indexing ===" << std::endl;
    std::cout << std::endl;
    
    // Parameters
    const double REGION_X_MIN = 0.0, REGION_X_MAX = 100.0;
    const double REGION_Y_MIN = 0.0, REGION_Y_MAX = 100.0;
    
    std::vector<size_t> point_counts = {100000, 1000000};  // 100K and 1M for quick test
    std::vector<std::string> distributions = {"uniform", "clustered"};
    
    // Create polygon grid (100x100 = 10,000 polygons)
    std::cout << "Creating polygon grid (100x100)..." << std::endl;
    auto polygons = PolygonLoader::create_grid(
        REGION_X_MIN, REGION_Y_MIN,
        REGION_X_MAX, REGION_Y_MAX,
        100, 100
    );
    std::cout << "  Polygons: " << polygons.size() << std::endl;
    std::cout << std::endl;
    
    // Benchmark for each dataset
    for (const auto& dist_type : distributions) {
        std::cout << "=== " << dist_type << " distribution ===" << std::endl;
        
        for (size_t num_points : point_counts) {
            std::cout << "\nDataset: " << num_points << " points" << std::endl;
            
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
            long long index_build_ns;
            auto [results_quadtree, time_quadtree] = benchmark_with_quadtree(points, polygons, &index_build_ns);
            double throughput_quadtree = (double)num_points / (time_quadtree / 1e9);
            std::cout << " " << std::fixed << std::setprecision(2)
                      << throughput_quadtree << " pts/sec ("
                      << (time_quadtree / 1e6) << " ms)"
                      << " [index build: " << (index_build_ns / 1e6) << " ms]" << std::endl;
            
            // Speedup
            double speedup = (double)time_brute / time_quadtree;
            std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
            
            // Validate
            validate_results(results_brute, results_quadtree);
        }
        
        std::cout << std::endl;
    }
    
    std::cout << "=== Benchmark Complete ===" << std::endl;
    return 0;
}
