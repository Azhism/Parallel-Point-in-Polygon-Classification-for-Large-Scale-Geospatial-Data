#include "generator/distribution.hpp"
#include <cmath>

namespace pdc_gen {

std::vector<pdc_geo::Point> UniformDistribution::generate(
    size_t n,
    double x_min, double x_max,
    double y_min, double y_max,
    uint64_t seed) {
    
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> x_dist(x_min, x_max);
    std::uniform_real_distribution<double> y_dist(y_min, y_max);
    
    std::vector<pdc_geo::Point> points;
    points.reserve(n);
    
    for (uint64_t i = 0; i < n; i++) {
        points.emplace_back(x_dist(rng), y_dist(rng), i);
    }
    
    return points;
}

std::vector<pdc_geo::Point> ClusteredDistribution::generate(
    size_t n,
    size_t num_clusters,
    double x_min, double x_max,
    double y_min, double y_max,
    double cluster_std_dev,
    uint64_t seed) {
    
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> x_dist(x_min, x_max);
    std::uniform_real_distribution<double> y_dist(y_min, y_max);
    std::uniform_int_distribution<size_t> cluster_dist(0, num_clusters - 1);
    
    // Generate random cluster centers
    std::vector<pdc_geo::Point> centers;
    for (size_t i = 0; i < num_clusters; i++) {
        centers.emplace_back(x_dist(rng), y_dist(rng), i);
    }
    
    // Generate points around clusters
    std::normal_distribution<double> normal_dist(0.0, cluster_std_dev);
    std::vector<pdc_geo::Point> points;
    points.reserve(n);
    
    double region_width = x_max - x_min;
    double region_height = y_max - y_min;
    
    for (uint64_t i = 0; i < n; i++) {
        size_t cluster_idx = cluster_dist(rng);
        const auto& center = centers[cluster_idx];
        
        double x = center.x + normal_dist(rng) * region_width;
        double y = center.y + normal_dist(rng) * region_height;
        
        // Clamp to region
        x = std::max(x_min, std::min(x_max, x));
        y = std::max(y_min, std::min(y_max, y));
        
        points.emplace_back(x, y, i);
    }
    
    return points;
}

}  // namespace pdc_gen
