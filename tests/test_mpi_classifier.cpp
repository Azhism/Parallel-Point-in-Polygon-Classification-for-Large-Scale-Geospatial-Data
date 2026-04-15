#include <mpi.h>
#include <iostream>
#include <vector>
#include <cstdint>
#include <limits>

#include "distributed/mpi_types.hpp"
#include "distributed/mpi_classifier.hpp"
#include "distributed/spatial_partitioner.hpp"
#include "parallel/parallel_classifier.hpp"
#include "index/quadtree.hpp"
#include "generator/distribution.hpp"
#include "generator/polygon_loader.hpp"

using namespace pdc_geo;
using namespace pdc_gen;
using namespace pdc_mpi;

static bool validate_results(
    const std::vector<ClassificationResult>& ref,
    const std::vector<ClassificationResult>& got,
    const std::string& label
) {
    if (ref.size() != got.size()) {
        std::cout << "  [FAIL] " << label << ": size mismatch ("
                  << ref.size() << " vs " << got.size() << ")\n";
        return false;
    }
    int mismatches = 0;
    for (size_t i = 0; i < ref.size(); ++i) {
        if (ref[i].polygon_id != got[i].polygon_id) {
            ++mismatches;
            if (mismatches <= 5) {
                std::cout << "    mismatch at point " << i
                          << ": expected " << ref[i].polygon_id
                          << ", got " << got[i].polygon_id << "\n";
            }
        }
    }
    if (mismatches > 0) {
        std::cout << "  [FAIL] " << label << ": " << mismatches << " mismatches\n";
        return false;
    }
    std::cout << "  [PASS] " << label << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    register_mpi_types();

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    if (rank == 0) {
        std::cout << "============================================================\n";
        std::cout << "=== MPI Classifier Correctness Test ===\n";
        std::cout << "=== Ranks: " << num_ranks << " ===\n";
        std::cout << "============================================================\n\n";
    }

    const double X_MIN = 0.0, X_MAX = 100.0, Y_MIN = 0.0, Y_MAX = 100.0;
    const size_t N_POINTS = 100000;

    // All ranks create the polygon grid (needed for index building)
    auto polygons = PolygonLoader::create_grid(X_MIN, Y_MIN, X_MAX, Y_MAX, 100, 100);

    // Rank 0 generates test points
    std::vector<Point> points;
    if (rank == 0) {
        points = UniformDistribution::generate(N_POINTS, X_MIN, X_MAX, Y_MIN, Y_MAX);
    }

    // Compute sequential reference on rank 0
    std::vector<ClassificationResult> seq_results;
    if (rank == 0) {
        QuadTreeIndex seq_index;
        seq_index.build(polygons);
        ParallelClassifier seq_clf;
        seq_results = seq_clf.classify(points, polygons, seq_index,
                                        ParallelClassifier::Strategy::SEQUENTIAL, 1);
    }

    MPIClassifier mpi_clf;
    bool all_pass = true;

    // Test 1: Polygon Replication Mode
    {
        auto results = mpi_clf.classify_distributed(
            points, polygons, PolygonMode::REPLICATE,
            X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);

        if (rank == 0) {
            all_pass &= validate_results(seq_results, results,
                "Replication mode (100K uniform, " + std::to_string(num_ranks) + " ranks)");
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Test 2: Spatial Partition Mode
    {
        auto results = mpi_clf.classify_distributed(
            points, polygons, PolygonMode::SPATIAL_PARTITION,
            X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);

        if (rank == 0) {
            all_pass &= validate_results(seq_results, results,
                "Spatial partition mode (100K uniform, " + std::to_string(num_ranks) + " ranks)");
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Test 3: Clustered distribution (replication)
    {
        std::vector<Point> clustered_pts;
        std::vector<ClassificationResult> clustered_ref;

        if (rank == 0) {
            clustered_pts = ClusteredDistribution::generate(
                N_POINTS, 5, X_MIN, X_MAX, Y_MIN, Y_MAX, 0.015);

            QuadTreeIndex idx;
            idx.build(polygons);
            ParallelClassifier clf;
            clustered_ref = clf.classify(clustered_pts, polygons, idx,
                                          ParallelClassifier::Strategy::SEQUENTIAL, 1);
        }

        auto results = mpi_clf.classify_distributed(
            clustered_pts, polygons, PolygonMode::REPLICATE,
            X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);

        if (rank == 0) {
            all_pass &= validate_results(clustered_ref, results,
                "Replication mode (100K clustered, " + std::to_string(num_ranks) + " ranks)");
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Test 4: Batched mode (small batch for testing)
    {
        auto results = mpi_clf.classify_distributed_batched(
            50000, polygons, PolygonMode::REPLICATE, "uniform", 25000,
            X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);

        if (rank == 0) {
            // Generate same points sequentially to compare
            std::vector<ClassificationResult> batch_ref;
            QuadTreeIndex idx;
            idx.build(polygons);
            ParallelClassifier clf;

            for (uint64_t b = 0; b < 2; ++b) {
                auto batch_pts = UniformDistribution::generate(
                    25000, X_MIN, X_MAX, Y_MIN, Y_MAX, 42 + b);
                auto br = clf.classify(batch_pts, polygons, idx,
                                        ParallelClassifier::Strategy::SEQUENTIAL, 1);
                for (auto& r : br) {
                    r.point_index += b * 25000;
                }
                batch_ref.insert(batch_ref.end(), br.begin(), br.end());
            }

            all_pass &= validate_results(batch_ref, results,
                "Batched mode (50K uniform, 2 batches of 25K, " + std::to_string(num_ranks) + " ranks)");
        }
    }

    if (rank == 0) {
        std::cout << "\n============================================================\n";
        if (all_pass) {
            std::cout << "=== ALL TESTS PASSED ===\n";
        } else {
            std::cout << "=== SOME TESTS FAILED ===\n";
        }
        std::cout << "============================================================\n";
    }

    free_mpi_types();
    MPI_Finalize();
    return all_pass ? 0 : 1;
}
