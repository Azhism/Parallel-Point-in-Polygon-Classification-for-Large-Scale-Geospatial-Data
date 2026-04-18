#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <omp.h>

#include "generator/polygon_loader.hpp"
#include "geometry/ray_casting.hpp"
#include "index/quadtree.hpp"
#include "ipc/ipc.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace pdc_geo;
using namespace pdc_gen;
using namespace std::chrono;

namespace {

enum class Distribution {
    Uniform,
    Clustered
};

enum class PolygonMode {
    Replicated,
    Sharded
};

struct Config {
    std::vector<size_t> dataset_sizes{1000000, 10000000};
    size_t batch_size = 250000;
    int workers = std::max(1, omp_get_max_threads());
    bool quick = false;
    bool full = false;
    bool skip_scaling = false;
    bool skip_process = false;
};

struct WorkerContext {
    double x_min = 0.0;
    double x_max = 0.0;
    std::vector<pdc_geo::Polygon> polygons;
    std::vector<uint64_t> original_ids;
    QuadTreeIndex index;
};

struct Aggregate {
    uint64_t points = 0;
    uint64_t matched = 0;
    uint64_t unmatched = 0;
    uint64_t candidate_checks = 0;
    uint64_t checksum = 0;
};

struct RunMetrics {
    Aggregate aggregate;
    double index_build_ms = 0.0;
    double generation_partition_ms = 0.0;
    double classify_ms = 0.0;
    double total_ms = 0.0;
    uint64_t polygons_indexed = 0;
};

struct ProcessRunMetrics {
    Aggregate aggregate;
    double write_ms = 0.0;
    double process_ms = 0.0;
    double read_ms = 0.0;
    double total_ms = 0.0;
};

struct ClusterModel {
    std::vector<Point> centers;
    double std_dev = 0.015;
};

static constexpr double X_MIN = 0.0;
static constexpr double X_MAX = 100.0;
static constexpr double Y_MIN = 0.0;
static constexpr double Y_MAX = 100.0;
static constexpr uint64_t NO_POLYGON = std::numeric_limits<uint64_t>::max();

static double ns_to_ms(long long ns) {
    return ns / 1e6;
}

static std::string distribution_name(Distribution d) {
    return d == Distribution::Uniform ? "uniform" : "clustered";
}

static std::string polygon_mode_name(PolygonMode mode) {
    return mode == PolygonMode::Replicated ? "replicated polygons" : "spatially sharded polygons";
}

static uint64_t mix_u64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static void merge_aggregate(Aggregate& dst, const Aggregate& src) {
    dst.points += src.points;
    dst.matched += src.matched;
    dst.unmatched += src.unmatched;
    dst.candidate_checks += src.candidate_checks;
    dst.checksum ^= src.checksum;
}

static void merge_worker_result(Aggregate& dst, const pdc_ipc::WorkerResult& src) {
    dst.points += static_cast<uint64_t>(src.points_processed);
    dst.matched += static_cast<uint64_t>(src.matched);
    dst.unmatched += static_cast<uint64_t>(src.unmatched);
    dst.candidate_checks += static_cast<uint64_t>(src.candidate_checks);
    dst.checksum ^= src.checksum;
}

static Aggregate classify_bucket(const std::vector<Point>& points,
                                 const WorkerContext& worker) {
    Aggregate aggregate;
    aggregate.points = static_cast<uint64_t>(points.size());

    for (const auto& point : points) {
        auto candidates = worker.index.query_point(point);
        aggregate.candidate_checks += static_cast<uint64_t>(candidates.size());

        uint64_t polygon_id = NO_POLYGON;
        for (uint64_t local_id : candidates) {
            if (local_id >= worker.polygons.size()) {
                continue;
            }

            auto classification = RayCaster::point_in_polygon(point, worker.polygons[local_id]);
            if (classification == RayCaster::Classification::INSIDE ||
                classification == RayCaster::Classification::ON_BOUNDARY) {
                polygon_id = worker.original_ids[local_id];
                break;
            }
        }

        if (polygon_id == NO_POLYGON) {
            aggregate.unmatched++;
        } else {
            aggregate.matched++;
        }

        aggregate.checksum ^= mix_u64((point.id << 32) ^ polygon_id);
    }

    return aggregate;
}

static ClusterModel make_cluster_model(size_t num_clusters,
                                       double std_dev,
                                       uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> x_dist(X_MIN, X_MAX);
    std::uniform_real_distribution<double> y_dist(Y_MIN, Y_MAX);

    ClusterModel model;
    model.std_dev = std_dev;
    model.centers.reserve(num_clusters);
    for (size_t i = 0; i < num_clusters; ++i) {
        model.centers.emplace_back(x_dist(rng), y_dist(rng), i);
    }
    return model;
}

static Point generate_uniform_point(uint64_t id, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> x_dist(X_MIN, X_MAX);
    std::uniform_real_distribution<double> y_dist(Y_MIN, Y_MAX);
    return Point(x_dist(rng), y_dist(rng), id);
}

static Point generate_clustered_point(uint64_t id,
                                      std::mt19937_64& rng,
                                      const ClusterModel& model) {
    std::uniform_int_distribution<size_t> cluster_dist(0, model.centers.size() - 1);
    std::normal_distribution<double> normal_dist(0.0, model.std_dev);

    const auto& center = model.centers[cluster_dist(rng)];
    double x = center.x + normal_dist(rng) * (X_MAX - X_MIN);
    double y = center.y + normal_dist(rng) * (Y_MAX - Y_MIN);

    x = std::max(X_MIN, std::min(X_MAX, x));
    y = std::max(Y_MIN, std::min(Y_MAX, y));
    return Point(x, y, id);
}

static int worker_for_point(const Point& p, int workers) {
    double normalized = (p.x - X_MIN) / (X_MAX - X_MIN);
    int worker = static_cast<int>(normalized * workers);
    if (worker < 0) {
        worker = 0;
    }
    if (worker >= workers) {
        worker = workers - 1;
    }
    return worker;
}

static std::vector<WorkerContext> build_worker_contexts(const std::vector<pdc_geo::Polygon>& source,
                                                        int workers,
                                                        PolygonMode mode,
                                                        double* build_ms,
                                                        uint64_t* polygons_indexed) {
    auto start = high_resolution_clock::now();
    std::vector<WorkerContext> contexts(workers);

    double stripe_width = (X_MAX - X_MIN) / workers;
    for (int w = 0; w < workers; ++w) {
        contexts[w].x_min = X_MIN + stripe_width * w;
        contexts[w].x_max = (w == workers - 1) ? X_MAX : contexts[w].x_min + stripe_width;

        if (mode == PolygonMode::Replicated) {
            contexts[w].polygons = source;
            contexts[w].original_ids.reserve(source.size());
            for (size_t i = 0; i < source.size(); ++i) {
                contexts[w].polygons[i].id = static_cast<uint64_t>(i);
                contexts[w].original_ids.push_back(source[i].id);
            }
        } else {
            for (const auto& poly : source) {
                if (poly.bbox.max_x >= contexts[w].x_min && poly.bbox.min_x <= contexts[w].x_max) {
                    pdc_geo::Polygon copy = poly;
                    copy.id = static_cast<uint64_t>(contexts[w].polygons.size());
                    contexts[w].original_ids.push_back(poly.id);
                    contexts[w].polygons.push_back(copy);
                }
            }
        }

        contexts[w].index.build(contexts[w].polygons);
    }

    uint64_t total_indexed = 0;
    for (const auto& context : contexts) {
        total_indexed += static_cast<uint64_t>(context.polygons.size());
    }

    auto end = high_resolution_clock::now();
    *build_ms = ns_to_ms(duration_cast<nanoseconds>(end - start).count());
    *polygons_indexed = total_indexed;
    return contexts;
}

static RunMetrics run_batched_distributed(size_t total_points,
                                          Distribution distribution,
                                          int workers,
                                          PolygonMode mode,
                                          size_t batch_size,
                                          const std::vector<pdc_geo::Polygon>& source_polygons) {
    RunMetrics metrics;
    auto total_start = high_resolution_clock::now();

    metrics.polygons_indexed = 0;
    auto contexts = build_worker_contexts(
        source_polygons, workers, mode, &metrics.index_build_ms, &metrics.polygons_indexed);

    ClusterModel cluster_model = make_cluster_model(5, 0.015, 42);
    uint64_t processed = 0;
    uint64_t batch_id = 0;

    while (processed < total_points) {
        size_t this_batch = std::min(batch_size, total_points - static_cast<size_t>(processed));
        std::vector<std::vector<Point>> buckets(workers);
        for (auto& bucket : buckets) {
            bucket.reserve((this_batch / workers) + 128);
        }

        auto generation_start = high_resolution_clock::now();
        std::mt19937_64 rng(42 + batch_id * 104729ULL);
        for (size_t i = 0; i < this_batch; ++i) {
            uint64_t point_id = processed + static_cast<uint64_t>(i);
            Point point = (distribution == Distribution::Uniform)
                ? generate_uniform_point(point_id, rng)
                : generate_clustered_point(point_id, rng, cluster_model);

            buckets[worker_for_point(point, workers)].push_back(point);
        }
        auto generation_end = high_resolution_clock::now();
        metrics.generation_partition_ms +=
            ns_to_ms(duration_cast<nanoseconds>(generation_end - generation_start).count());

        auto classify_start = high_resolution_clock::now();
        std::vector<std::future<Aggregate>> futures;
        futures.reserve(workers);
        for (int w = 0; w < workers; ++w) {
            futures.push_back(std::async(
                std::launch::async,
                [&buckets, &contexts, w]() {
                    return classify_bucket(buckets[w], contexts[w]);
                }));
        }

        for (auto& future : futures) {
            merge_aggregate(metrics.aggregate, future.get());
        }
        auto classify_end = high_resolution_clock::now();
        metrics.classify_ms +=
            ns_to_ms(duration_cast<nanoseconds>(classify_end - classify_start).count());

        processed += static_cast<uint64_t>(this_batch);
        batch_id++;
    }

    auto total_end = high_resolution_clock::now();
    metrics.total_ms = ns_to_ms(duration_cast<nanoseconds>(total_end - total_start).count());
    return metrics;
}

static void generate_partitioned_points(size_t total_points,
                                        Distribution distribution,
                                        int workers,
                                        size_t batch_size,
                                        std::vector<std::vector<Point>>* buckets) {
    buckets->assign(workers, {});
    for (auto& bucket : *buckets) {
        bucket.reserve((total_points / static_cast<size_t>(workers)) + 128);
    }

    ClusterModel cluster_model = make_cluster_model(5, 0.015, 42);
    uint64_t processed = 0;
    uint64_t batch_id = 0;
    while (processed < total_points) {
        size_t this_batch = std::min(batch_size, total_points - static_cast<size_t>(processed));
        std::mt19937_64 rng(42 + batch_id * 104729ULL);
        for (size_t i = 0; i < this_batch; ++i) {
            uint64_t point_id = processed + static_cast<uint64_t>(i);
            Point point = (distribution == Distribution::Uniform)
                ? generate_uniform_point(point_id, rng)
                : generate_clustered_point(point_id, rng, cluster_model);
            (*buckets)[worker_for_point(point, workers)].push_back(point);
        }
        processed += static_cast<uint64_t>(this_batch);
        batch_id++;
    }
}

static std::string quote_arg(const std::string& arg) {
    return "\"" + arg + "\"";
}

#ifdef _WIN32
static std::string windows_error_message(DWORD code) {
    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string message = size && buffer ? std::string(buffer, size) : "unknown Windows error";
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

static ProcessRunMetrics run_process_ipc_once(size_t total_points,
                                              Distribution distribution,
                                              int workers,
                                              PolygonMode mode,
                                              size_t batch_size,
                                              const std::vector<pdc_geo::Polygon>& source_polygons,
                                              const std::string& ipc_dir,
                                              const std::string& worker_exe) {
    ProcessRunMetrics metrics;
    auto total_start = high_resolution_clock::now();

    std::filesystem::create_directories(ipc_dir);
    const std::string polygon_path = ipc_dir + "\\polygons.bin";

    std::vector<std::vector<Point>> buckets;
    auto write_start = high_resolution_clock::now();
    generate_partitioned_points(total_points, distribution, workers, batch_size, &buckets);
    pdc_ipc::write_polygons(polygon_path, source_polygons);

    double stripe_width = (X_MAX - X_MIN) / workers;
    std::vector<std::string> input_paths(workers);
    std::vector<std::string> result_paths(workers);
    for (int w = 0; w < workers; ++w) {
        input_paths[w] = ipc_dir + "\\worker_" + std::to_string(w) + "_input.bin";
        result_paths[w] = ipc_dir + "\\worker_" + std::to_string(w) + "_result.bin";

        pdc_ipc::WorkerInputHeader header;
        header.worker_id = w;
        header.total_points = static_cast<int32_t>(buckets[w].size());
        header.polygon_mode = static_cast<int32_t>(
            mode == PolygonMode::Replicated ? pdc_ipc::PolygonMode::Replicated
                                            : pdc_ipc::PolygonMode::Sharded);
        header.stripe_x_min = X_MIN + stripe_width * w;
        header.stripe_x_max = (w == workers - 1) ? X_MAX : header.stripe_x_min + stripe_width;
        pdc_ipc::write_worker_input(input_paths[w], header, buckets[w]);
    }
    auto write_end = high_resolution_clock::now();
    metrics.write_ms = ns_to_ms(duration_cast<nanoseconds>(write_end - write_start).count());

    auto process_start = high_resolution_clock::now();
    std::vector<HANDLE> handles;
    handles.reserve(workers);
    for (int w = 0; w < workers; ++w) {
        std::string command = quote_arg(worker_exe) + " " +
            quote_arg(input_paths[w]) + " " +
            quote_arg(polygon_path) + " " +
            quote_arg(result_paths[w]);

        STARTUPINFOA startup_info;
        PROCESS_INFORMATION process_info;
        ZeroMemory(&startup_info, sizeof(startup_info));
        ZeroMemory(&process_info, sizeof(process_info));
        startup_info.cb = sizeof(startup_info);

        std::vector<char> mutable_command(command.begin(), command.end());
        mutable_command.push_back('\0');

        BOOL ok = CreateProcessA(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info);

        if (!ok) {
            throw std::runtime_error("CreateProcess failed for worker " + std::to_string(w) +
                                     ": " + windows_error_message(GetLastError()));
        }

        handles.push_back(process_info.hProcess);
        CloseHandle(process_info.hThread);
    }

    DWORD wait_result = WaitForMultipleObjects(
        static_cast<DWORD>(handles.size()),
        handles.data(),
        TRUE,
        INFINITE);
    if (wait_result == WAIT_FAILED) {
        throw std::runtime_error("WaitForMultipleObjects failed: " +
                                 windows_error_message(GetLastError()));
    }

    for (int w = 0; w < workers; ++w) {
        DWORD exit_code = 0;
        GetExitCodeProcess(handles[w], &exit_code);
        CloseHandle(handles[w]);
        if (exit_code != 0) {
            throw std::runtime_error("worker " + std::to_string(w) +
                                     " exited with code " + std::to_string(exit_code));
        }
    }
    auto process_end = high_resolution_clock::now();
    metrics.process_ms = ns_to_ms(duration_cast<nanoseconds>(process_end - process_start).count());

    auto read_start = high_resolution_clock::now();
    for (int w = 0; w < workers; ++w) {
        auto result = pdc_ipc::read_worker_result(result_paths[w]);
        merge_worker_result(metrics.aggregate, result);
    }
    auto read_end = high_resolution_clock::now();
    metrics.read_ms = ns_to_ms(duration_cast<nanoseconds>(read_end - read_start).count());

    auto total_end = high_resolution_clock::now();
    metrics.total_ms = ns_to_ms(duration_cast<nanoseconds>(total_end - total_start).count());
    return metrics;
}
#endif

static void print_run(size_t points,
                      Distribution distribution,
                      int workers,
                      PolygonMode mode,
                      const RunMetrics& metrics) {
    double classify_throughput = points / (metrics.classify_ms / 1000.0);
    double avg_candidates = metrics.aggregate.points == 0
        ? 0.0
        : static_cast<double>(metrics.aggregate.candidate_checks) / metrics.aggregate.points;

    std::cout << std::left << std::setw(10) << points
              << std::setw(11) << distribution_name(distribution)
              << std::setw(8) << workers
              << std::setw(27) << polygon_mode_name(mode)
              << std::right << std::setw(12) << std::fixed << std::setprecision(2)
              << metrics.classify_ms
              << std::setw(14) << std::fixed << std::setprecision(2)
              << classify_throughput
              << std::setw(12) << std::fixed << std::setprecision(2)
              << metrics.total_ms
              << std::setw(10) << std::fixed << std::setprecision(2)
              << avg_candidates
              << "  " << std::hex << metrics.aggregate.checksum << std::dec
              << "\n";
}

static void print_header() {
    std::cout << std::left << std::setw(10) << "Points"
              << std::setw(11) << "Dist"
              << std::setw(8) << "Workers"
              << std::setw(27) << "Polygon mode"
              << std::right << std::setw(12) << "Class ms"
              << std::setw(14) << "Class pts/s"
              << std::setw(12) << "Total ms"
              << std::setw(10) << "Avg cand"
              << "  Checksum\n";
    std::cout << std::string(116, '-') << "\n";
}

static void run_main_matrix(const Config& config,
                            const std::vector<pdc_geo::Polygon>& polygons) {
    std::cout << "\n=== Large-Scale Batched Throughput ===\n";
    std::cout << "Batch size: " << config.batch_size
              << ", workers: " << config.workers
              << ", default mode: replicated polygons\n";
    print_header();

    for (size_t points : config.dataset_sizes) {
        for (Distribution distribution : {Distribution::Uniform, Distribution::Clustered}) {
            auto metrics = run_batched_distributed(
                points, distribution, config.workers, PolygonMode::Replicated,
                config.batch_size, polygons);
            print_run(points, distribution, config.workers, PolygonMode::Replicated, metrics);
        }
    }
}

static void run_tradeoff_table(const Config& config,
                               const std::vector<pdc_geo::Polygon>& polygons) {
    size_t points = config.quick ? 250000 : 1000000;

    std::cout << "\n=== Polygon Replication vs Spatial Sharding ===\n";
    std::cout << "Trade-off: replication avoids boundary routing complexity but indexes "
              << "more polygon copies; sharding reduces index size but requires spatial routing.\n";
    print_header();

    for (Distribution distribution : {Distribution::Uniform, Distribution::Clustered}) {
        for (PolygonMode mode : {PolygonMode::Replicated, PolygonMode::Sharded}) {
            auto metrics = run_batched_distributed(
                points, distribution, config.workers, mode, config.batch_size, polygons);
            print_run(points, distribution, config.workers, mode, metrics);
            std::cout << "  Indexed polygon copies: " << metrics.polygons_indexed << "\n";
        }
    }
}

static void run_strong_scaling(const Config& config,
                               const std::vector<pdc_geo::Polygon>& polygons) {
    size_t points = config.quick ? 250000 : 1000000;

    std::cout << "\n=== Strong Scaling (fixed " << points << " points, replicated polygons) ===\n";
    print_header();

    for (int workers : {1, 2, 4}) {
        if (workers > config.workers) {
            continue;
        }

        auto uniform_metrics = run_batched_distributed(
            points, Distribution::Uniform, workers, PolygonMode::Replicated,
            config.batch_size, polygons);
        print_run(points, Distribution::Uniform, workers, PolygonMode::Replicated, uniform_metrics);

        auto clustered_metrics = run_batched_distributed(
            points, Distribution::Clustered, workers, PolygonMode::Replicated,
            config.batch_size, polygons);
        print_run(points, Distribution::Clustered, workers, PolygonMode::Replicated, clustered_metrics);
    }
}

static void run_weak_scaling(const Config& config,
                             const std::vector<pdc_geo::Polygon>& polygons) {
    size_t points_per_worker = config.quick ? 100000 : 250000;

    std::cout << "\n=== Weak Scaling (" << points_per_worker
              << " points per worker, replicated polygons) ===\n";
    print_header();

    for (int workers : {1, 2, 4}) {
        if (workers > config.workers) {
            continue;
        }

        size_t total_points = points_per_worker * static_cast<size_t>(workers);
        auto uniform_metrics = run_batched_distributed(
            total_points, Distribution::Uniform, workers, PolygonMode::Replicated,
            config.batch_size, polygons);
        print_run(total_points, Distribution::Uniform, workers, PolygonMode::Replicated, uniform_metrics);

        auto clustered_metrics = run_batched_distributed(
            total_points, Distribution::Clustered, workers, PolygonMode::Replicated,
            config.batch_size, polygons);
        print_run(total_points, Distribution::Clustered, workers, PolygonMode::Replicated, clustered_metrics);
    }
}

static void run_process_ipc_comparison(const Config& config,
                                       const std::vector<pdc_geo::Polygon>& polygons) {
#ifdef _WIN32
    size_t points = config.quick ? 100000 : 1000000;
    std::string ipc_dir = "ipc";
    std::string worker_exe = "build\\worker.exe";

    std::cout << "\n=== Multi-Process IPC Benchmark (file-based worker.exe) ===\n";
    std::cout << "Dataset size: " << points << " points per distribution, workers: "
              << config.workers << "\n";
    std::cout << "IPC files are written under .\\" << ipc_dir << "\n";
    std::cout << std::left << std::setw(11) << "Dist"
              << std::setw(27) << "Polygon mode"
              << std::right << std::setw(12) << "Write ms"
              << std::setw(12) << "Worker ms"
              << std::setw(10) << "Read ms"
              << std::setw(12) << "Total ms"
              << std::setw(14) << "Total pts/s"
              << "  Checksum\n";
    std::cout << std::string(102, '-') << "\n";

    for (Distribution distribution : {Distribution::Uniform, Distribution::Clustered}) {
        for (PolygonMode mode : {PolygonMode::Replicated, PolygonMode::Sharded}) {
            auto metrics = run_process_ipc_once(
                points, distribution, config.workers, mode, config.batch_size,
                polygons, ipc_dir, worker_exe);
            double throughput = points / (metrics.total_ms / 1000.0);
            std::cout << std::left << std::setw(11) << distribution_name(distribution)
                      << std::setw(27) << polygon_mode_name(mode)
                      << std::right << std::setw(12) << std::fixed << std::setprecision(2)
                      << metrics.write_ms
                      << std::setw(12) << metrics.process_ms
                      << std::setw(10) << metrics.read_ms
                      << std::setw(12) << metrics.total_ms
                      << std::setw(14) << throughput
                      << "  " << std::hex << metrics.aggregate.checksum << std::dec
                      << "\n";
        }
    }
#else
    (void)config;
    (void)polygons;
    std::cout << "\n=== Multi-Process IPC Benchmark skipped: Windows CreateProcess path only ===\n";
#endif
}

static std::vector<size_t> parse_size_list(const std::string& text) {
    std::vector<size_t> sizes;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (!part.empty()) {
            sizes.push_back(static_cast<size_t>(std::stoull(part)));
        }
    }
    return sizes;
}

static Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--quick") {
            config.quick = true;
            config.dataset_sizes = {100000, 1000000};
            config.batch_size = 100000;
        } else if (arg == "--full") {
            config.full = true;
            config.dataset_sizes = {1000000, 10000000, 100000000};
        } else if (arg == "--skip-scaling") {
            config.skip_scaling = true;
        } else if (arg == "--skip-process") {
            config.skip_process = true;
        } else if (arg == "--workers" && i + 1 < argc) {
            config.workers = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--batch-size" && i + 1 < argc) {
            config.batch_size = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--sizes" && i + 1 < argc) {
            auto sizes = parse_size_list(argv[++i]);
            if (!sizes.empty()) {
                config.dataset_sizes = sizes;
            }
        }
    }

    config.workers = std::max(1, config.workers);
    config.batch_size = std::max<size_t>(1, config.batch_size);
    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    Config config = parse_args(argc, argv);

    std::cout << "============================================================\n";
    std::cout << "=== Milestone 3: Scalable Batch + Distributed Execution ===\n";
    std::cout << "============================================================\n";
    std::cout << "OpenMP max threads: " << omp_get_max_threads() << "\n";
    std::cout << "Worker model: spatial master/worker partitions with independent indices\n";
    std::cout << "Aggregation: counts + checksum only, no per-point result materialization\n";
    std::cout << "100M mode: use --full when you want the longest required-scale run\n";

    std::cout << "\nCreating polygon grid (100x100)...\n";
    auto polygons = PolygonLoader::create_grid(X_MIN, Y_MIN, X_MAX, Y_MAX, 100, 100);
    std::cout << "  Polygons: " << polygons.size() << "\n";

    run_main_matrix(config, polygons);
    run_tradeoff_table(config, polygons);

    if (!config.skip_scaling) {
        run_strong_scaling(config, polygons);
        run_weak_scaling(config, polygons);
    }

    if (!config.skip_process) {
        run_process_ipc_comparison(config, polygons);
    }

    std::cout << "\n============================================================\n";
    std::cout << "=== Milestone 3 Benchmark Complete ===\n";
    std::cout << "============================================================\n";
    return 0;
}
