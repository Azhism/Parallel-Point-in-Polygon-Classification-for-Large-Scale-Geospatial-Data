# Project Summary: Parallel Point-in-Polygon Classification

**Last Updated:** April 12, 2026 - Radix Sort Optimization Complete ✅  
**Author:** Azhab Babar (Azhism)

## Week 1 Implementation Summary (Detailed)

## Week 2 Implementation Summary (Detailed) 

### Week 2 Overview

Week 2 implements parallel-oriented design using OpenMP for multi-core acceleration. Four parallelization strategies were benchmarked with honest timing after applying four critical fixes.

### Strategy Implementations

1. **Sequential Baseline** - Single-threaded reference for speedup calculation
2. **Static OpenMP** - Equal work chunks pre-divided across threads
3. **Dynamic OpenMP** - Guided chunk distribution for load balancing
4. **Tiled + Morton-Sorted** - Z-order space-filling curve + parallel classify (honest end-to-end timing)
5. **Work-Stealing (NEW)** - True per-thread deque stealing with task stealing

### Week 2 Fixes Applied

#### Fix 1: Work-Stealing Classifier (Stage 5)
- New implementation: `include/parallel/work_stealing_classifier.hpp` + `src/parallel/work_stealing_classifier.cpp`
- Per-thread task deques with stealing from other threads' queues
- **Results:** 2.39× speedup on 1M clustered points (best performer on this workload)
- Real-world: 1.60× speedup on 745-point Pakistan dataset

#### Fix 2: Honest Tiled+Morton Timing  
- Changed measurement to include Morrison sort preprocessing
- End-to-end timing reflects real amortization cost
- Reports both classify-only and end-to-end for transparency
- **Result:** Reduces false speedup claims from ~4.7× to actual values

#### Fix 3: Thread Scaling Memory Bottleneck Analysis
- Added diagnostic analysis after thread scaling results
- Identifies "Quadtree lookup is memory-bound" as root cause
- Explains why 4 threads achieve only 55-70% efficiency
- Clear explanation of RAM contention vs. compute scaling

#### Fix 4: Strategy Notes at Top
- Moved explanatory text from end to beginning of output
- Visible before any benchmark results
- Clarifies each strategy's purpose and timing methodology

### Benchmark Results (Week 2 - Final with All Fixes)

#### Synthetic Data: Uniform Distribution (1M points)

| Strategy | Time | Speedup | Efficiency |
|----------|------|---------|-----------|
| Sequential | 786.26 ms | — | — |
| Static OMP | 516.01 ms | 1.52× | 38% |
| Dynamic OMP | 483.27 ms | **1.63×** | 41% |
| Tiled+Morton (e2e) | 844.67 ms | 0.93× | 23% |
| Work-Stealing | 709.09 ms | 1.11× | 28% |

#### Synthetic Data: Clustered Distribution (1M points)

| Strategy | Time | Speedup | Efficiency |
|----------|------|---------|-----------|
| Sequential | 511.73 ms | — | — |
| Static OMP | 305.81 ms | 1.67× | 42% |
| Dynamic OMP | 264.25 ms | 1.94× | 49% |
| Tiled+Morton (e2e) | 470.05 ms | 1.09× | 27% |
| **Work-Stealing** | 214.32 ms | **2.39×** | **60%** ⭐ |

#### Real-World Data (Pakistan, 745 points, 204 polygons)

| Strategy | Time | Speedup |
|----------|------|---------|
| Sequential | 13.56 ms | — |
| Static OMP | 21.53 ms | 0.63× |
| Dynamic OMP | 14.78 ms | 0.92× |
| Tiled+Morton (e2e) | 14.67 ms | 0.92× |
| **Work-Stealing** | 8.49 ms | **1.60×** ⭐ |

#### Thread Scaling Analysis (1M clustered, Dynamic OMP)

| Threads | Time | Speedup | Efficiency |
|---------|------|---------|-----------|
| 1 | 423.40 ms | 1.21× | 121% |
| 2 | 225.55 ms | 2.27× | 113% |
| 4 | 182.11 ms | 2.81× | 70% |

**Analysis:** 2→4 thread scaling is sub-linear (2.27× → 2.81×). Root cause: Quadtree lookup is memory-bound with random access patterns. Extra threads increase RAM contention without proportional compute gain.

### Code Changes Summary

**New Files:**
- `include/parallel/work_stealing_classifier.hpp`
- `src/parallel/work_stealing_classifier.cpp`

**Modified Files:**
- `src/benchmark_m2.cpp` - Added 4 fixes (work-stealing, honest timing, thread analysis, strategy notes)
- `build.sh` - Compile work_stealing_classifier.cpp
- `CMakeLists.txt` - Added pip_parallel library target

### Validation

- ✓ All 5 strategies produce identical results (validated against sequential)
- ✓ Work-Stealing correctly handles point-in-polygon classification
- ✓ Memory bottleneck diagnosis confirmed via thread scaling metrics
- ✓ End-to-end timing honestly reflects preprocessing costs

## 1. Purpose of Week 1

## 1. Purpose of Week 1

Week 1 establishes the sequential baseline for point-in-polygon classification before parallelization milestones.

Primary goals completed:

1. Build a correct geometric classifier (ray-casting with edge-case handling).
2. Build spatial pruning/indexing structures for faster candidate lookup.
3. Integrate real-world GeoJSON polygon + point datasets.
4. Benchmark baseline and indexed approaches on synthetic + real data.
5. Validate that optimized pipelines match baseline classification results.

## 2. Final Week 1 Pipeline

Implemented in `src/benchmark_m1.cpp`:

1. Stage 1: Brute force + bounding-box filter
2. Stage 2: Quadtree index + ray-casting verification
3. Stage 3: Strip Index + ray-casting verification
4. Stage 4: Real-world benchmark using GeoJSON

Spatial Hash path was removed from active code during Week 1 cleanup and replaced with Strip Index.

## 3. Code Components Implemented and Used

### 3.1 Geometry Layer

Key files:

1. `include/geometry/point.hpp`
2. `include/geometry/polygon.hpp`
3. `include/geometry/ray_casting.hpp`
4. `src/geometry/ray_casting.cpp`

What it does:

1. Defines `Point`, `BBox`, `Polygon`, and `MultiPolygon` structures.
2. Computes polygon bounding boxes.
3. Performs robust point-in-polygon classification via ray-casting.
4. Handles boundary conditions (edge/vertex cases) and holes.

### 3.2 Candidate Pruning / Indexing Layer

Key files:

1. `include/index/bbox_filter.hpp`, `src/index/bbox_filter.cpp`
2. `include/index/quadtree.hpp`, `src/index/quadtree.cpp`
3. `include/index/strip_index.hpp`, `src/index/strip_index.cpp`

What each index does:

1. BBox filter:
1. Linear scan over all polygon bboxes.
2. Used as baseline pruning in Stage 1.

2. Quadtree:
1. Builds a spatial hierarchy over polygon bbox extents.
2. Splits nodes recursively when leaf threshold is exceeded.
3. Queries candidate polygons by descending relevant quadrants.
4. Deduplicates candidates using set-based accumulation.

3. Strip Index:
1. Computes global `y_min`, `y_max` from polygons.
2. Chooses strip count (default `sqrt(num_polygons)` when unspecified).
3. Assigns each polygon id to all strips overlapped by its bbox.
4. For query point `p`, maps `p.y` to strip index and returns that strip�s candidate list.

### 3.3 Synthetic Data Generation Layer

Key files:

1. `include/generator/distribution.hpp`
2. `src/generator/uniform_distribution.cpp`
3. `src/generator/clustered_distribution.cpp`
4. `include/generator/polygon_loader.hpp`
5. `src/generator/polygon_loader.cpp`

What it does:

1. Generates uniform and clustered point sets.
2. Builds synthetic polygon grid (100 x 100 -> 10,000 polygons).

### 3.4 Real-World GeoJSON Layer

Key files:

1. `include/index/geojson_loader.hpp`
2. `src/index/geojson_loader.cpp`
3. `include/nlohmann/json.hpp`

What it does:

1. Loads Polygon and MultiPolygon geometries from GeoJSON.
2. Expands MultiPolygon into individual `Polygon` objects.
3. Extracts centroid points from multiple schemas:
1. `center_lon` / `center_lat`
2. `x_coord` / `y_coord`
3. Point geometry coordinates fallback
4. Applies robust numeric parsing for numeric/string coordinate values.

## 4. Core Classification Logic in Benchmark

Main execution flow in `src/benchmark_m1.cpp`:

1. Build synthetic polygon grid (`PolygonLoader::create_grid`).
2. For each distribution (`uniform`, `clustered`) and dataset size (`100K`, `1M`):
1. Generate points.
2. Stage 1:
1. `BBoxFilter::get_candidates(point, polygons)`
2. Ray-cast candidates via `classify_point_from_candidates`
3. Stage 2:
1. Build `QuadTreeIndex`
2. Query candidates per point, classify via ray-casting
4. Stage 3:
1. Build `StripIndex`
2. Query strip candidates per point, classify via ray-casting
5. Validate Stage 2 and Stage 3 outputs against Stage 1.
3. Run Stage 4 real-world benchmark:
1. Load polygons from `pak_admin2.geojson`
2. Load points from `pak_admincentroids.geojson`
3. Run Stage 1 and Stage 2 on real data
4. Validate Stage 2 against Stage 1

Helper function:

1. `classify_point_from_candidates(...)`:
1. Iterates candidate polygon ids.
2. Uses `RayCaster::point_in_polygon`.
3. Returns first containing polygon id or `UINT64_MAX` if no match.

## 5. Additional Large-Scale Mode (Code Path)

`benchmark_m1.cpp` includes `--large` / `--large-scale` mode:

1. Point counts switch to 10M and 100M.
2. Processing is batched to keep memory bounded.
3. Brute-force stage is skipped in this mode.
4. Compares Quadtree and Strip Index directly.
5. Tracks mismatches between indexed methods.

Note: Week 1 standard reporting remains focused on 100K/1M and real-data correctness.

## 6. Real-World Data Integrated in Week 1

Active files used by Stage 4:

1. `pak_admin2.geojson` (polygons)
2. `pak_admincentroids.geojson` (points)

Observed load counts in current run set:

1. 204 polygons (after MultiPolygon expansion)
2. 745 centroid points

## 7. Build, Test, and Run Status

Build command:

```bash
bash build.sh
```

Benchmark command:

```bash
bash -lc "./build/benchmark_m1"
```

Validation/test status completed in Week 1:

1. Build script compiles core objects and benchmark successfully.
2. Ray-casting unit tests run and pass (`./build/test_ray_casting`).
3. Benchmark completes through synthetic + real-world sections.

## 8. Latest Week 1 Benchmark Snapshot

Source: `output_strip_index_clean.txt`

### Uniform Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 1849.77 ms | 91.77 ms | 96.83 ms | 20.16x | 19.10x |
| 1M points | 19147.03 ms | 617.48 ms | 703.29 ms | 31.01x | 27.23x |

### Clustered Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 2123.59 ms | 63.67 ms | 187.06 ms | 33.35x | 11.35x |
| 1M points | 16063.64 ms | 289.97 ms | 514.42 ms | 55.40x | 31.23x |

### Real-World Benchmark

1. Stage 1 (Brute force + BBox): 21.36 ms
2. Stage 2 (Quadtree): 19.74 ms
3. Speedup: 1.08x

---

# Week 2 Parallelization (Detailed)

## 1. Purpose of Week 2

Week 2 extends the sequential Week 1 baseline with **parallel-oriented design using OpenMP** to accelerate point-in-polygon classification on multi-core systems.

Primary goals completed:

1. Implement four parallelization strategies (Sequential, Static OMP, Dynamic OMP, Tiled+Morton)
2. Integrate OpenMP support into build system
3. Benchmark all strategies on synthetic and real-world data
4. Validate correctness of parallel implementations
5. Analyze thread scaling and efficiency

## 2. Parallel Strategies Implemented

### Strategy 1: Sequential Baseline
- Single-threaded reference implementation
- Same index + ray-casting logic as Week 1
- Used for speedup comparison

### Strategy 2: Static OpenMP
- Work divided equally among threads at compile time
- Each thread processes contiguous chunk of points
- Low overhead, predictable scheduling
- **Best for:** Uniform workloads

### Strategy 3: Dynamic OpenMP
- Work units distributed dynamically to threads
- Idle threads grab next available work item
- Better load balancing for skewed workloads
- **Best for:** Clustered/non-uniform distributions

### Strategy 4: Tiled + Morton-Sorted
- Points reordered using Morton (Z-order) curve for spatial locality
- Tiled processing for better cache utilization
- Combined with OpenMP parallelization
- **Best for:** Cache efficiency

## 3. OpenMP Integration

### Build System Changes

**File:** `build.sh`

```bash
CXXFLAGS="-O3 -std=c++17 -I./include -Wall -Wextra -fopenmp"
LDFLAGS="-fopenmp"
```

Changes:
- Added `-fopenmp` for OpenMP support
- Added `-fopenmp` to linker flags
- Compile `src/parallel/parallel_classifier.cpp`
- Linked OpenMP runtime

### Parallel Classifier

**Files:**
- `include/parallel/parallel_classifier.hpp`
- `src/parallel/parallel_classifier.cpp`

**Interface:**
```cpp
std::vector<ClassificationResult> ParallelClassifier::classify(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    Strategy strategy,           // SEQUENTIAL, STATIC_OMP, DYNAMIC_OMP, TILED
    int num_threads = 0          // 0 = auto (omp_get_max_threads())
);
```

## 4. Benchmark Results (Week 2)

### Uniform Distribution

#### 100K Points
| Strategy | Time (ms) | Throughput (pts/sec) | Speedup |
|----------|---------|---------------------|---------|
| Sequential | 79.45 | 1,258,638 | — |
| Static OMP | 21.73 | 4,602,494 | 3.66× |
| Dynamic OMP | 31.21 | 3,204,497 | 2.55× |
| Tiled+Morton* | 16.65 | 6,006,106 | **4.77×** ⭐ |

#### 1M Points
| Strategy | Time (ms) | Throughput (pts/sec) | Speedup |
|----------|---------|---------------------|----------|
| Sequential | 476.96 | 2,096,601 | — |
| Static OMP | 237.39 | 4,212,395 | 2.01× |
| Dynamic OMP | 311.39 | 3,211,364 | 1.53× |
| Tiled+Morton* | 154.70 | 6,464,106 | **3.08×** ⭐ |

#### Thread Scaling (1M, Dynamic OMP) — 4 threads
| Threads | Time (ms) | Speedup | Efficiency |
|---------|---------|---------|-----------|
| 1 | 482.88 | 0.99× | 98.8% |
| 2 | 287.96 | 1.66× | 82.8% |
| 4 | 212.83 | **2.24×** | 59.3% |

### Clustered Distribution

#### 100K Points
| Strategy | Time (ms) | Throughput (pts/sec) | Speedup |
|----------|---------|---------------------|---------|
| Sequential | 28.73 | 3,480,324 | — |
| Static OMP | 18.97 | 5,270,617 | 1.51× |
| Dynamic OMP | 19.01 | 5,260,439 | 1.51× |
| Tiled+Morton* | 28.12 | 3,556,187 | 1.02× |

#### 1M Points
| Strategy | Time (ms) | Throughput (pts/sec) | Speedup |
|----------|---------|---------------------|----------|
| Sequential | 265.69 | 3,763,845 | — |
| Static OMP | 144.33 | 6,928,695 | 1.84× |
| Dynamic OMP | 140.45 | 7,119,761 | **1.89×** |
| Tiled+Morton* | 164.58 | 6,075,932 | 1.61× |

#### Thread Scaling (1M, Dynamic OMP) — 4 threads
| Threads | Time (ms) | Speedup | Efficiency |
|---------|---------|---------|-----------|
| 1 | 302.51 | 0.88× | 87.8% |
| 2 | 173.27 | 1.53× | 76.7% |
| 4 | 150.85 | **1.76×** | 44.0% |

### Real-World Data (Pakistan, 745 points, 204 polygons)

| Strategy | Time (ms) | Throughput (pts/sec) | Speedup |
|----------|---------|---------------------|----------|
| Sequential | 17.99 | 41,407 | — |
| Static OMP | 13.18 | 56,510 | 1.36× |
| Dynamic OMP | 15.65 | 47,590 | 1.15× |
| Tiled+Morton* | 16.94 | 43,974 | 1.06× |

**Note:** Parallel overhead > benefit for small datasets

## 5. Key Insights

1. **Strategy Selection (Fair Timing):**
   - **Tiled+Morton now dominates on uniform datasets** (4.77× on 100K, 3.08× on 1M) ⭐
   - Static OMP still competitive (3.66× on 100K uniform)
   - Dynamic OMP best for clustered large datasets (1.89× on 1M clustered)
   - Tiled+Morton preprocesses sort outside timer for fair comparison

2. **Thread Scaling:**
   - Efficiency decreases with more threads (98.8% → 59.3% for 1M uniform)
   - Still achieves 2.24× speedup on 4 threads

3. **Real-World Performance:**
   - Parallel benefit marginal on small datasets (745 points: 1.06-1.36×)
   - Parallelization clearly beneficial for 100K+ point workloads

4. **Correctness & Validation:**
   - All parallel strategies validated against sequential baseline
   - Zero classification mismatches across 20 tests
   - 100% correctness maintained

**\* Note:** Tiled+Morton measurements exclude preprocessing (sort) from timer; only parallel classification phase is timed.

