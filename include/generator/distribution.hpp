#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include <vector>
#include <random>

namespace pdc_gen {

/**
 * Generate uniformly distributed points within a rectangular region.
 */
class UniformDistribution {
 public:
    /**
     * Generate n uniform random points in [x_min, x_max] x [y_min, y_max].
     */
    static std::vector<pdc_geo::Point> generate(
        size_t n,
        double x_min, double x_max,
        double y_min, double y_max,
        uint64_t seed = 42
    );
};

/**
 * Generate clustered points simulating urban GPS distributions.
 */
class ClusteredDistribution {
 public:
    /**
     * Generate n clustered points around k cluster centers.
     * Each point is sampled from a Gaussian distribution centered at a cluster.
     */
    static std::vector<pdc_geo::Point> generate(
        size_t n,
        size_t num_clusters,
        double x_min, double x_max,
        double y_min, double y_max,
        double cluster_std_dev = 0.01,  // Standard deviation of cluster
        uint64_t seed = 42
    );
};

}  // namespace pdc_gen
