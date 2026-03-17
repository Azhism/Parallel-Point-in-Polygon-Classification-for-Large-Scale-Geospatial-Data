# Milestone 2 Plan: Parallel Point Classification and Load Balancing

## Overview

Milestone 2 parallelizes the M1 baseline using shared-memory parallelism (OpenMP, multithreading) while addressing load imbalance caused by non-uniform spatial distributions.

**Goal**: Achieve **3-6x speedup** on 4-8 cores while maintaining accuracy.

---

## Architectural Changes (M2 vs M1)

### M1 Pipeline (Sequential)
```
For each point:
  └─ Query Index → Ray-cast → Store result
```

### M2 Pipeline (Parallel + Load-Aware)
```
Assign points to worker threads:
  ├─ Thread 1: Query Index → Ray-cast (dynamic work queue)
  ├─ Thread 2: Query Index → Ray-cast (work-steal when idle)
  ├─ Thread 3: Query Index → Ray-cast 
  └─ Thread 4: Query Index → Ray-cast
  
With spatial optimization:
  ├─ Pre-sort points (Morton curve)
  ├─ Batch nearby points together
  └─ Improve cache locality
```

---

## Key Challenges & Solutions

### Challenge 1: Load Imbalance

**Problem**: Clustered data creates hotspots
- 90% of points in 10% of space
- Some threads process dense regions, others idle

**Solution**: Dynamic task distribution
```cpp
// M1: Static work division
#pragma omp parallel for
for (int i = 0; i < n; i++) process(points[i]);

// M2: Dynamic queue-based (recommended)
#pragma omp parallel
{
    #pragma omp for schedule(dynamic, chunk_size)
    for (int i = 0; i < n; i++) process(points[i]);
}
```

### Challenge 2: Cache Coherency

**Problem**: Random point order = poor cache locality

**Solution**: Spatial sort before parallelization
```cpp
// Pre-process: Sort points by Morton/Hilbert curve
auto sorted_points = morton_sort(points);

// Parallel: Nearby points → same cache line
#pragma omp parallel for
for (const auto& p : sorted_points) process(p);
```

### Challenge 3: Synchronization Overhead

**Problem**: Writing results concurrently causes contention

**Solution**: Thread-local accumulation
```cpp
#pragma omp parallel
{
    std::vector<QueryResult> local_results;  // Thread-local
    
    #pragma omp for schedule(dynamic)
    for (const auto& p : points) {
        local_results.push_back(classify(p));
    }
    
    #pragma omp critical
    {
        results.insert(results.end(), 
                      local_results.begin(), 
                      local_results.end());
    }
}
```

---

## Implementation Roadmap

### Phase 1: Basic OpenMP Parallelization (3 days)

**1.1 Add OpenMP Support**
- Enable `-fopenmp` in build script
- Link against libgomp/MSVC OpenMP

**1.2 Parallel Ray-Casting Loop**
```cpp
// new file: src/parallel/parallel_classifier.cpp
std::vector<QueryResult> classify_points_parallel(
    const std::vector<Point>& points,
    const std::vector<Polygon>& polygons,
    const RTreeIndex& index,
    int num_threads = 4
) {
    std::vector<QueryResult> results;
    results.reserve(points.size());
    
    #pragma omp parallel for num_threads(num_threads) \
            schedule(dynamic, 1024) shared(results)
    for (size_t i = 0; i < points.size(); i++) {
        auto candidates = index.query_point(points[i]);
        for (uint64_t poly_id : candidates) {
            auto clsf = RayCaster::point_in_polygon(
                points[i], polygons[poly_id]
            );
            if (clsf == RayCaster::Classification::INSIDE) {
                #pragma omp critical
                results.emplace_back(points[i].id, poly_id);
                break;
            }
        }
    }
    
    return results;
}
```

**1.3 Benchmark Parallel Scaling**
- Extend `benchmark_m1.cpp` to test 1, 2, 4, 8 threads
- Measure speedup relative to M1 baseline
- Profile hotspots with `perf` or Windows Performance Analyzer

### Phase 2: Spatial Optimization (4 days)

**2.1 Implement Morton Curve Ordering**
```cpp
// new file: include/optimization/morton_curve.hpp
class MortonSort {
public:
    static std::vector<Point> sort_points(
        const std::vector<Point>& points
    );
private:
    static uint64_t compute_morton_code(double x, double y);
};
```

**2.2 Cache-Local Batching**
```cpp
// new file: src/optimization/batch_processor.cpp
struct Batch {
    std::vector<Point> points;
    BBox bbox;  // Region this batch covers
};

std::vector<Batch> create_spatial_batches(
    const std::vector<Point>& points,
    size_t batch_size = 4096
);
```

**2.3 Measure Cache Efficiency**
- Track L1/L2 cache miss rate before/after sorting
- Compare throughput: random order vs sorted

### Phase 3: Advanced Load Balancing (5 days)

**3.1 Profile Polygon Load Distribution**
```cpp
// Analyze per-polygon workload
std::map<uint64_t, size_t> polygon_point_count;
for (const auto& result : results) {
    polygon_point_count[result.polygon_id]++;
}

// Log load skew
double max_load = *std::max_element(...);
double min_load = *std::min_element(...);
std::cout << "Load skew ratio: " << max_load / min_load << "\n";
```

**3.2 Implement Work-Stealing Queue**
```cpp
// new file: include/parallel/work_queue.hpp
template <typename Task>
class WorkStealingQueue {
public:
    void push(const Task& task);
    bool pop(Task& task);
    bool steal(Task& task);  // Thread tries to steal from others
};
```

**3.3 Hybrid Static/Dynamic Scheduling**
```cpp
// For uniform regions: static chunks (low overhead)
// For dense regions: dynamic/stealing (balanced load)
#pragma omp parallel for schedule(guided, 1024)
```

### Phase 4: Evaluation & Scaling Analysis (4 days)

**4.1 Strong Scaling Benchmark**
```
Fixed Problem Size: 10M points, 100 polygons
Vary Threads: 1, 2, 4, 8

Expected Results:
  1 thread:  1.0x (baseline)
  2 threads: 1.9x  
  4 threads: 3.5x  (90% efficiency)
  8 threads: 6.0x  (75% efficiency)
```

**4.2 Weak Scaling Benchmark**
```
Fixed Points/Thread: 10M / num_threads
Vary Threads: 1, 2, 4, 8

Expected: Time roughly constant (~5 sec)
Actual test: Increase points proportionally
```

**4.3 Distribution Sensitivity**
```
Compare across:
  - Uniform distribution (best case)
  - Clustered distribution (worst case)  
  - Mixed (50% cluster, 50% uniform)
```

---

## New Files to Create

```
src/parallel/
  ├── parallel_classifier.cpp      ✓ OpenMP main loop
  └── dynamic_scheduler.cpp        ✓ Advanced scheduling

include/optimization/
  ├── morton_curve.hpp             ✓ Spatial sort
  ├── batch_processor.hpp          ✓ Batch creation
  └── cache_optimizer.hpp          ✓ Cache tuning

include/parallel/
  ├── work_queue.hpp               ✓ Work-stealing
  └── load_analyzer.hpp            ✓ Profiling

tests/
  └── test_parallel_correctness.cpp ✓ Parallel validation

benchmarks/
  ├── strong_scaling.cpp           ✓ Fixed problem, vary threads
  └── weak_scaling.cpp             ✓ Fixed work per thread
```

---

## Compilation Changes

### build.sh (updated for M2)

```bash
# Add OpenMP flags
CXXFLAGS="-O3 -std=c++17 -I./include -fopenmp"

# Compile new parallel modules
g++ $CXXFLAGS -c src/parallel/parallel_classifier.cpp ...
g++ $CXXFLAGS -c src/optimization/morton_curve.cpp ...

# Link with OpenMP
g++ $CXXFLAGS ... -fopenmp -o build/benchmark_m2_strong_scaling
```

### CMakeLists.txt (for future adoption)

```cmake
find_package(OpenMP REQUIRED)
target_link_libraries(pip_core PUBLIC OpenMP::OpenMP_CXX)
```

---

## Testing Strategy

### Correctness Validation

1. **Serial vs Parallel Equivalence**
```cpp
auto results_serial = classify_points_sequential(...);
auto results_parallel = classify_points_parallel(...);
assert(results_serial == results_parallel);
```

2. **Thread Count Independence**
```cpp
// Results should be identical regardless of # threads
for (int nthreads : {1, 2, 4, 8}) {
    auto results = classify_points_parallel(..., nthreads);
    assert(results == golden_results);
}
```

3. **Determinism (optional)**
```cpp
// With same random seed, parallel results should be reproducible
```

### Performance Metrics

1. **Speedup** = Sequential_Time / Parallel_Time
2. **Efficiency** = Speedup / num_threads
3. **Cache Miss Rate** (via profiler)
4. **Load Balance** Ratio = Max_Work / Min_Work

---

## Expected Performance Targets

### Uniform Distribution (1M points, 100 polygons)

| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1       | 2000      | 1.0x    | 100%      |
| 2       | 1050      | 1.9x    | 95%       |
| 4       | 580       | 3.4x    | 85%       |
| 8       | 340       | 5.9x    | 74%       |

### Clustered Distribution (1M points, high skew)

| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1       | 2500      | 1.0x    | 100%      |
| 2       | 1400      | 1.8x    | 90%       |
| 4       | 700       | 3.6x    | 90%       |
| 8       | 420       | 6.0x    | 75%       |

*(With spatial sorting + load balancing)*

---

## Debugging & Profiling

### OpenMP Debugging
```bash
# Detect race conditions
export OMP_NUM_THREADS=4
export OMP_SCHEDULE=dynamic,1
./build/benchmark_m2 --validate
```

### Performance Profiling
```bash
# Linux/MSYS2
perf record -e cache-references,cache-misses ./build/benchmark_m2
perf report

# Windows (VTune or Windows Performance Analyzer)
```

### Common Issues & Fixes

| Issue | Symptom | Solution |
|-------|---------|----------|
| False sharing | Low speedup despite low load | Pad data structures, use thread-local storage |
| Synchronization bottleneck | Slow relative to serial | Use atomic operations, critical sections only where necessary |
| Bad NUMA access | Random performance variance (NUMA systems) | Explicit thread affinity binding |
| Load imbalance | Some threads idle while others work | Use schedule(dynamic) or work-stealing |

---

## Deliverables (M2 Completion)

- ✅ OpenMP parallelization with 3-6x speedup
- ✅ Spatial sorting (Morton curve) implementation
- ✅ Dynamic load balancing (work-stealing queue)
- ✅ Strong scaling analysis (1M points, 1-8 threads)
- ✅ Weak scaling analysis (fixed work per thread)
- ✅ Cache optimization metrics
- ✅ Updated benchmark harness
- ✅ Comprehensive performance report

---

## Success Criteria

1. **Correctness**: Parallel results match sequential results (100%)
2. **Speedup**: ≥3.5x on 4 cores (for any distribution)
3. **Efficiency**: ≥80% efficiency on uniform distribution
4. **Scalability**: Linear to 4 cores, superlinear with spatial optimization
5. **Load Analysis**: Report actual load imbalance ratios

---

## Timeline Estimate

- Phase 1 (OpenMP basics): **3 days**
- Phase 2 (Spatial optimization): **4 days**
- Phase 3 (Advanced load balancing): **5 days**
- Phase 4 (Evaluation + polish): **4 days**

**Total**: ~2 weeks for M2

---

## Next Phase: Milestone 3

Once M2 is complete:
- Transition to **distributed execution** (MPI)
- Scale to **10M-100M points**
- Benchmark on **multiple machines**

---

## References

- [OpenMP 5.0 Specification](https://www.openmp.org/)
- [Morton Curve (Z-order curve)](https://en.wikipedia.org/wiki/Z-order_curve)
- [Work-stealing schedulers](https://en.wikipedia.org/wiki/Work_stealing)
- [Parallel algorithm patterns](https://www.cs.cmu.edu/~15210/papers.html)

---

**Status**: ✅ M1 Complete → 📋 M2 Roadmap Ready

See [M1_SUMMARY.md](M1_SUMMARY.md) for what's been completed.
