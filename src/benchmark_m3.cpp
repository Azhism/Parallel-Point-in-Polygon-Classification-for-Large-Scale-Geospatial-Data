#include <mpi.h>
#include <omp.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>

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

static const double X_MIN = 0.0, X_MAX = 100.0, Y_MIN = 0.0, Y_MAX = 100.0;

static std::string mode_name(PolygonMode m) {
    return (m == PolygonMode::REPLICATE) ? "replicate" : "partition";
}

static void print_header(int rank) {
    if (rank != 0) return;
    std::cout << "\n" << std::left
              << std::setw(12) << "Points"
              << std::setw(12) << "Dist"
              << std::setw(12) << "PolyMode"
              << std::setw(8)  << "Ranks"
              << std::setw(8)  << "Thr/R"
              << std::setw(12) << "Total(ms)"
              << std::setw(12) << "Scatter(ms)"
              << std::setw(12) << "Compute(ms)"
              << std::setw(12) << "Gather(ms)"
              << std::setw(16) << "pts/sec"
              << std::setw(10) << "Comm%"
              << std::setw(10) << "LdBal"
              << "\n"
              << std::string(134, '-') << "\n";
}

static void print_row(
    int rank,
    uint64_t total_points,
    const std::string& dist,
    PolygonMode poly_mode,
    const MPIBenchmarkResult& b,
    double load_balance
) {
    if (rank != 0) return;

    double total_ms   = b.total_time_sec * 1000.0;
    double scatter_ms = b.scatter_time_sec * 1000.0;
    double compute_ms = b.compute_time_sec * 1000.0;
    double gather_ms  = b.gather_time_sec * 1000.0;
    double pts_sec    = static_cast<double>(total_points) / b.total_time_sec;
    double comm_pct   = (b.scatter_time_sec + b.gather_time_sec) / b.total_time_sec * 100.0;

    std::cout << std::left
              << std::setw(12) << total_points
              << std::setw(12) << dist
              << std::setw(12) << mode_name(poly_mode)
              << std::setw(8)  << b.num_ranks
              << std::setw(8)  << b.num_threads_per_rank
              << std::fixed << std::setprecision(1)
              << std::setw(12) << total_ms
              << std::setw(12) << scatter_ms
              << std::setw(12) << compute_ms
              << std::setw(12) << gather_ms
              << std::setprecision(0)
              << std::setw(16) << pts_sec
              << std::setprecision(1)
              << std::setw(10) << comm_pct
              << std::setprecision(2)
              << std::setw(10) << load_balance
              << "\n";
}

static double compute_load_balance(double local_compute_sec) {
    int num_ranks;
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    double max_compute = 0.0, sum_compute = 0.0;
    MPI_Reduce(&local_compute_sec, &max_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_compute_sec, &sum_compute, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    double avg_compute = sum_compute / num_ranks;
    return (avg_compute > 0.0) ? max_compute / avg_compute : 1.0;
}

static bool validate_1m(
    const std::vector<ClassificationResult>& distributed_results,
    const std::vector<Polygon>& polygons
) {
    // Generate same 1M points and classify sequentially
    auto points = UniformDistribution::generate(1000000, X_MIN, X_MAX, Y_MIN, Y_MAX);
    QuadTreeIndex idx;
    idx.build(polygons);
    ParallelClassifier clf;
    auto seq = clf.classify(points, polygons, idx,
                             ParallelClassifier::Strategy::SEQUENTIAL, 1);

    if (seq.size() != distributed_results.size()) {
        std::cout << "  [FAIL] Validation: size mismatch ("
                  << seq.size() << " vs " << distributed_results.size() << ")\n";
        return false;
    }

    int mismatches = 0;
    for (size_t i = 0; i < seq.size(); ++i) {
        if (seq[i].polygon_id != distributed_results[i].polygon_id) {
            ++mismatches;
        }
    }

    if (mismatches > 0) {
        std::cout << "  [FAIL] Validation: " << mismatches << " mismatches out of "
                  << seq.size() << "\n";
        return false;
    }
    std::cout << "  [PASS] 1M uniform validation matches sequential\n";
    return true;
}

static void run_benchmark(
    const std::vector<Polygon>& polygons,
    uint64_t total_points,
    const std::string& dist,
    PolygonMode poly_mode,
    int rank,
    int /*num_ranks*/
) {
    MPIClassifier mpi_clf;
    MPIBenchmarkResult bench{};

    uint64_t batch_size = std::min(total_points, static_cast<uint64_t>(10000000));

    // Warmup run
    mpi_clf.classify_distributed_batched(
        std::min(total_points, static_cast<uint64_t>(100000)),
        polygons, poly_mode, dist, batch_size,
        X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);

    MPI_Barrier(MPI_COMM_WORLD);

    // Timed run
    mpi_clf.classify_distributed_batched(
        total_points, polygons, poly_mode, dist, batch_size,
        X_MIN, X_MAX, Y_MIN, Y_MAX, 0, &bench);

    double load_balance = compute_load_balance(bench.compute_time_sec);

    print_row(rank, total_points, dist, poly_mode, bench, load_balance);

    // Validate 1M uniform replication: all ranks must participate in the MPI classify,
    // then rank 0 compares results against sequential
    if (total_points == 1000000 && dist == "uniform" && poly_mode == PolygonMode::REPLICATE) {
        auto validation_results = mpi_clf.classify_distributed_batched(
            1000000, polygons, PolygonMode::REPLICATE, "uniform", 1000000,
            X_MIN, X_MAX, Y_MIN, Y_MAX, 0, nullptr);
        if (rank == 0) {
            validate_1m(validation_results, polygons);
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    register_mpi_types();

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    // Parse command-line arguments
    std::vector<uint64_t> sizes = {1000000, 10000000};
    bool include_100m = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--100m" || arg == "--100M") {
            include_100m = true;
        }
        if (arg == "--sizes" && i + 1 < argc) {
            sizes.clear();
            sizes.push_back(std::stoull(argv[++i]));
        }
    }

    if (include_100m) {
        sizes.push_back(100000000);
    }

    if (rank == 0) {
        std::cout << "============================================================\n";
        std::cout << "=== Milestone 3: Distributed MPI Benchmark ===\n";
        std::cout << "============================================================\n";
        std::cout << "MPI Ranks: " << num_ranks << "\n";
        std::cout << "OMP Threads/rank: " << omp_get_max_threads() << "\n";
        std::cout << "Total cores used: " << num_ranks * omp_get_max_threads() << "\n";
        std::cout << "Polygon grid: 100x100 = 10,000 polygons\n";
        std::cout << "============================================================\n";
    }

    // Create polygon grid (all ranks)
    auto polygons = PolygonLoader::create_grid(X_MIN, Y_MIN, X_MAX, Y_MAX, 100, 100);

    if (rank == 0) {
        std::cout << "\nPolygons loaded: " << polygons.size() << "\n";
    }

    // ============================================================
    // Main benchmark matrix
    // ============================================================
    std::vector<std::string> dists = {"uniform", "clustered"};
    std::vector<PolygonMode> modes = {PolygonMode::REPLICATE, PolygonMode::SPATIAL_PARTITION};

    print_header(rank);

    for (auto total_n : sizes) {
        for (const auto& dist : dists) {
            for (auto mode : modes) {
                run_benchmark(polygons, total_n, dist, mode, rank, num_ranks);
                MPI_Barrier(MPI_COMM_WORLD);
            }
        }

        if (rank == 0) {
            std::cout << std::string(134, '-') << "\n";
        }
    }

    // ============================================================
    // Scaling analysis summary
    // ============================================================
    if (rank == 0) {
        std::cout << "\n============================================================\n";
        std::cout << "=== Scaling Analysis Notes ===\n";
        std::cout << "============================================================\n";
        std::cout << "Configuration: " << num_ranks << " ranks x "
                  << omp_get_max_threads() << " threads = "
                  << num_ranks * omp_get_max_threads() << " cores\n\n";

        std::cout << "Strong scaling: Run with mpirun -np 1,2,4,8 on fixed point count.\n";
        std::cout << "Weak scaling: Scale points proportionally with ranks.\n\n";

        int total_cores = num_ranks * omp_get_max_threads();
        std::cout << "Recommended runs for full analysis (adjust for your core count):\n";
        std::cout << "  OMP_NUM_THREADS=" << total_cores << " mpirun -np 1 ./build/benchmark_m3\n";
        if (total_cores >= 2)
            std::cout << "  OMP_NUM_THREADS=" << total_cores/2 << " mpirun -np 2 ./build/benchmark_m3\n";
        if (total_cores >= 4)
            std::cout << "  OMP_NUM_THREADS=" << total_cores/4 << " mpirun -np 4 ./build/benchmark_m3\n";
        std::cout << "  OMP_NUM_THREADS=1 mpirun -np " << total_cores << " ./build/benchmark_m3\n";
        std::cout << "\nFor 100M points: add --100m flag\n";

        std::cout << "\n============================================================\n";
        std::cout << "=== Trade-off Analysis ===\n";
        std::cout << "============================================================\n";
        std::cout << "Polygon Replication vs Spatial Partitioning:\n";
        std::cout << "  - Replication: Higher memory/rank, zero polygon comm after bcast,\n";
        std::cout << "    no boundary issues. Better for small polygon counts (10K).\n";
        std::cout << "  - Partitioning: Lower memory/rank, requires overlap buffer,\n";
        std::cout << "    boundary complexity. Better for millions of polygons.\n\n";
        std::cout << "Communication vs Computation:\n";
        std::cout << "  - At 1M points: comm overhead may be 20-40%% of total.\n";
        std::cout << "  - At 100M points: compute dominates, comm drops to <5%%.\n";
        std::cout << "  - Crossover depends on rank count and network latency.\n";
    }

    if (rank == 0) {
        std::cout << "\n============================================================\n";
        std::cout << "=== Benchmark Complete ===\n";
        std::cout << "============================================================\n";
    }

    free_mpi_types();
    MPI_Finalize();
    return 0;
}
