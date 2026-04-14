#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include "parallel/parallel_classifier.hpp"
#include "distributed/spatial_partitioner.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace pdc_mpi {

struct MPIBenchmarkResult {
    double total_time_sec;
    double scatter_time_sec;
    double compute_time_sec;
    double gather_time_sec;
    uint64_t total_points;
    uint64_t local_points;
    int num_ranks;
    int num_threads_per_rank;
};

class MPIClassifier {
public:
    /**
     * Distributed classification. Rank 0 owns all points initially.
     * After this call, rank 0 has all results sorted by point_index.
     *
     * @param all_points   Non-empty only on rank 0
     * @param polygons     All polygons (needed on all ranks for replicate mode,
     *                     or on rank 0 for partition mode)
     * @param poly_mode    REPLICATE or SPATIAL_PARTITION
     * @param x_min,x_max,y_min,y_max  Spatial domain bounds
     * @param num_omp_threads  OpenMP threads per rank (0 = auto)
     * @param bench        Optional benchmark timing output
     */
    std::vector<pdc_geo::ClassificationResult> classify_distributed(
        const std::vector<pdc_geo::Point>&   all_points,
        const std::vector<pdc_geo::Polygon>& polygons,
        PolygonMode                          poly_mode,
        double x_min, double x_max,
        double y_min, double y_max,
        int                                  num_omp_threads = 0,
        MPIBenchmarkResult*                  bench = nullptr
    ) const;

    /**
     * Batched distributed classification for very large point sets.
     * Rank 0 generates points in batches to limit peak memory.
     *
     * @param total_points   Total number of points to classify
     * @param polygons       All polygons (available on all ranks)
     * @param poly_mode      REPLICATE or SPATIAL_PARTITION
     * @param distribution   "uniform" or "clustered"
     * @param batch_size     Points per batch (e.g., 10M)
     * @param x_min,...      Spatial domain bounds
     * @param num_omp_threads  OpenMP threads per rank (0 = auto)
     * @param bench          Optional aggregate benchmark timing
     */
    std::vector<pdc_geo::ClassificationResult> classify_distributed_batched(
        uint64_t                             total_points,
        const std::vector<pdc_geo::Polygon>& polygons,
        PolygonMode                          poly_mode,
        const std::string&                   distribution,
        uint64_t                             batch_size,
        double x_min, double x_max,
        double y_min, double y_max,
        int                                  num_omp_threads = 0,
        MPIBenchmarkResult*                  bench = nullptr
    ) const;
};

}  // namespace pdc_mpi
