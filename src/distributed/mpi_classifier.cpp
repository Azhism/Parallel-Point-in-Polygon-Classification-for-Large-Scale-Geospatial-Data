#include "distributed/mpi_classifier.hpp"
#include "distributed/mpi_types.hpp"
#include "index/quadtree.hpp"
#include "generator/distribution.hpp"

#include <mpi.h>
#include <omp.h>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <iostream>
#include <limits>

namespace pdc_mpi {

// ============================================================
// classify_distributed — full point set in memory
// ============================================================

std::vector<pdc_geo::ClassificationResult> MPIClassifier::classify_distributed(
    const std::vector<pdc_geo::Point>&   all_points,
    const std::vector<pdc_geo::Polygon>& polygons,
    PolygonMode                          poly_mode,
    double x_min, double x_max,
    double y_min, double y_max,
    int                                  num_omp_threads,
    MPIBenchmarkResult*                  bench
) const {
    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    if (num_omp_threads > 0) omp_set_num_threads(num_omp_threads);

    double t_total_start = MPI_Wtime();
    double t_scatter = 0.0, t_compute = 0.0, t_gather = 0.0;

    // ---- Phase 1: Distribute polygons ----
    std::vector<pdc_geo::Polygon> local_polygons;

    if (poly_mode == PolygonMode::REPLICATE) {
        // Broadcast all polygons to every rank
        std::vector<double> poly_buf;
        int buf_size = 0;

        if (rank == 0) {
            poly_buf = serialize_polygons(polygons);
            buf_size = static_cast<int>(poly_buf.size());
        }

        MPI_Bcast(&buf_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (rank != 0) {
            poly_buf.resize(buf_size);
        }

        MPI_Bcast(poly_buf.data(), buf_size, MPI_DOUBLE, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            local_polygons = polygons;
        } else {
            local_polygons = deserialize_polygons(poly_buf);
        }
    } else {
        // Spatial partition: rank 0 sends each rank only its relevant polygons
        auto regions = SpatialPartitioner::create_strips(x_min, x_max, y_min, y_max, num_ranks);

        if (rank == 0) {
            // Rank 0 keeps its own filtered subset
            local_polygons = SpatialPartitioner::filter_polygons_for_region(polygons, regions[0]);

            // Send filtered subsets to other ranks
            for (int r = 1; r < num_ranks; ++r) {
                auto filtered = SpatialPartitioner::filter_polygons_for_region(polygons, regions[r]);
                auto buf = serialize_polygons(filtered);
                int buf_size = static_cast<int>(buf.size());
                MPI_Send(&buf_size, 1, MPI_INT, r, 0, MPI_COMM_WORLD);
                MPI_Send(buf.data(), buf_size, MPI_DOUBLE, r, 1, MPI_COMM_WORLD);
            }
        } else {
            int buf_size;
            MPI_Recv(&buf_size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<double> buf(buf_size);
            MPI_Recv(buf.data(), buf_size, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            local_polygons = deserialize_polygons(buf);
        }
    }

    // For spatial partition mode, the filtered polygon subset has non-contiguous
    // global IDs (e.g., 47, 48, ..., 5299). The QuadTree uses poly.id as an array
    // index internally, so we must renumber to contiguous local indices [0, N).
    // Keep a mapping to restore global IDs after classification.
    std::vector<uint64_t> local_to_global_poly_id;
    if (poly_mode == PolygonMode::SPATIAL_PARTITION) {
        local_to_global_poly_id.resize(local_polygons.size());
        for (size_t i = 0; i < local_polygons.size(); ++i) {
            local_to_global_poly_id[i] = local_polygons[i].id;
            local_polygons[i].id = i;
        }
    }

    // Build local quadtree index
    pdc_geo::QuadTreeIndex local_index;
    local_index.build(local_polygons);

    // ---- Phase 2: Scatter points ----
    double t_scatter_start = MPI_Wtime();

    auto regions = SpatialPartitioner::create_strips(x_min, x_max, y_min, y_max, num_ranks);

    std::vector<int> sendcounts(num_ranks, 0);
    std::vector<int> sdispls(num_ranks, 0);
    std::vector<pdc_geo::Point> send_buf;

    if (rank == 0) {
        // Stamp global indices and partition points
        std::vector<pdc_geo::Point> stamped_points = all_points;
        for (size_t i = 0; i < stamped_points.size(); ++i) {
            stamped_points[i].id = i;
        }

        auto partitioned = SpatialPartitioner::partition_points(stamped_points, regions);

        // Flatten into contiguous send buffer
        for (int r = 0; r < num_ranks; ++r) {
            sendcounts[r] = static_cast<int>(partitioned[r].size());
        }

        sdispls[0] = 0;
        for (int r = 1; r < num_ranks; ++r) {
            sdispls[r] = sdispls[r - 1] + sendcounts[r - 1];
        }

        int total_send = sdispls[num_ranks - 1] + sendcounts[num_ranks - 1];
        send_buf.resize(total_send);
        for (int r = 0; r < num_ranks; ++r) {
            std::copy(partitioned[r].begin(), partitioned[r].end(),
                      send_buf.begin() + sdispls[r]);
        }
    }

    // Scatter counts so each rank knows how many points it will receive
    int local_count = 0;
    MPI_Scatter(sendcounts.data(), 1, MPI_INT,
                &local_count, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Scatter actual points
    std::vector<pdc_geo::Point> local_points(local_count);
    MPI_Scatterv(send_buf.data(), sendcounts.data(), sdispls.data(), MPI_POINT,
                 local_points.data(), local_count, MPI_POINT,
                 0, MPI_COMM_WORLD);

    t_scatter = MPI_Wtime() - t_scatter_start;

    // ---- Phase 3: Local classification (hybrid MPI+OpenMP) ----
    double t_compute_start = MPI_Wtime();

    pdc_geo::ParallelClassifier clf;
    auto local_results = clf.classify(local_points, local_polygons, local_index,
                                       pdc_geo::ParallelClassifier::Strategy::DYNAMIC_OMP,
                                       num_omp_threads);

    // Remap point_index to global index using Point::id
    for (size_t i = 0; i < local_results.size(); ++i) {
        local_results[i].point_index = local_points[i].id;
    }

    // In spatial partition mode, remap local polygon index back to global polygon ID
    if (poly_mode == PolygonMode::SPATIAL_PARTITION) {
        for (auto& r : local_results) {
            if (r.polygon_id < local_to_global_poly_id.size()) {
                r.polygon_id = local_to_global_poly_id[r.polygon_id];
            }
        }
    }

    t_compute = MPI_Wtime() - t_compute_start;

    // ---- Phase 4: Gather results ----
    double t_gather_start = MPI_Wtime();

    std::vector<int> recvcounts(num_ranks, 0);
    MPI_Gather(&local_count, 1, MPI_INT,
               recvcounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> rdispls(num_ranks, 0);
    int total_results = 0;
    if (rank == 0) {
        for (int r = 1; r < num_ranks; ++r) {
            rdispls[r] = rdispls[r - 1] + recvcounts[r - 1];
        }
        total_results = rdispls[num_ranks - 1] + recvcounts[num_ranks - 1];
    }

    std::vector<pdc_geo::ClassificationResult> gathered_results;
    if (rank == 0) {
        gathered_results.resize(total_results);
    }

    MPI_Gatherv(local_results.data(), local_count, MPI_CLASSIFICATION,
                gathered_results.data(), recvcounts.data(), rdispls.data(),
                MPI_CLASSIFICATION, 0, MPI_COMM_WORLD);

    t_gather = MPI_Wtime() - t_gather_start;

    // ---- Phase 5: Reorder results by global point_index (rank 0 only) ----
    std::vector<pdc_geo::ClassificationResult> final_results;
    if (rank == 0) {
        final_results.resize(all_points.size());
        for (const auto& r : gathered_results) {
            if (r.point_index < final_results.size()) {
                final_results[r.point_index] = r;
            }
        }
    }

    double t_total = MPI_Wtime() - t_total_start;

    // Fill benchmark timing
    if (bench) {
        bench->total_time_sec = t_total;
        bench->scatter_time_sec = t_scatter;
        bench->compute_time_sec = t_compute;
        bench->gather_time_sec = t_gather;
        bench->total_points = (rank == 0) ? all_points.size() : 0;
        bench->local_points = static_cast<uint64_t>(local_count);
        bench->num_ranks = num_ranks;
        bench->num_threads_per_rank = omp_get_max_threads();
    }

    return final_results;
}

// ============================================================
// classify_distributed_batched — for 100M+ points
// ============================================================

std::vector<pdc_geo::ClassificationResult> MPIClassifier::classify_distributed_batched(
    uint64_t                             total_points,
    const std::vector<pdc_geo::Polygon>& polygons,
    PolygonMode                          poly_mode,
    const std::string&                   distribution,
    uint64_t                             batch_size,
    double x_min, double x_max,
    double y_min, double y_max,
    int                                  num_omp_threads,
    MPIBenchmarkResult*                  bench
) const {
    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    double t_total_start = MPI_Wtime();
    double t_scatter_accum = 0.0, t_compute_accum = 0.0, t_gather_accum = 0.0;

    uint64_t num_batches = (total_points + batch_size - 1) / batch_size;

    std::vector<pdc_geo::ClassificationResult> all_results;
    if (rank == 0) {
        all_results.reserve(total_points);
    }

    for (uint64_t b = 0; b < num_batches; ++b) {
        uint64_t batch_start = b * batch_size;
        uint64_t batch_count = std::min(batch_size, total_points - batch_start);

        // Rank 0 generates this batch's points
        std::vector<pdc_geo::Point> batch_points;
        if (rank == 0) {
            uint64_t seed = 42 + b;
            if (distribution == "clustered") {
                batch_points = pdc_gen::ClusteredDistribution::generate(
                    batch_count, 5, x_min, x_max, y_min, y_max, 0.015, seed);
            } else {
                batch_points = pdc_gen::UniformDistribution::generate(
                    batch_count, x_min, x_max, y_min, y_max, seed);
            }
        }

        // Classify this batch
        MPIBenchmarkResult batch_bench;
        auto batch_results = classify_distributed(
            batch_points, polygons, poly_mode,
            x_min, x_max, y_min, y_max,
            num_omp_threads, &batch_bench
        );

        t_scatter_accum += batch_bench.scatter_time_sec;
        t_compute_accum += batch_bench.compute_time_sec;
        t_gather_accum += batch_bench.gather_time_sec;

        // Rank 0 appends results with offset
        if (rank == 0) {
            for (auto& r : batch_results) {
                r.point_index += batch_start;
            }
            all_results.insert(all_results.end(),
                               batch_results.begin(), batch_results.end());
        }
    }

    double t_total = MPI_Wtime() - t_total_start;

    if (bench) {
        bench->total_time_sec = t_total;
        bench->scatter_time_sec = t_scatter_accum;
        bench->compute_time_sec = t_compute_accum;
        bench->gather_time_sec = t_gather_accum;
        bench->total_points = total_points;
        bench->local_points = total_points / static_cast<uint64_t>(num_ranks);
        bench->num_ranks = num_ranks;
        bench->num_threads_per_rank = omp_get_max_threads();
    }

    return all_results;
}

}  // namespace pdc_mpi
