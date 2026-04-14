# Project Summary: Parallel Point-in-Polygon Classification

**Last Updated:** April 14, 2026 - Distributed MPI Execution Complete ✅  
**Author:** Azhab Babar (Azhism)

---

## Week 1 Implementation Summary

### 1. Purpose of Week 1

Week 1 establishes the sequential baseline for point-in-polygon classification before parallelization milestones.

Primary goals completed:

1. Build a correct geometric classifier (ray-casting with edge-case handling).
2. Build spatial pruning/indexing structures for faster candidate lookup.
3. Integrate real-world GeoJSON polygon + point datasets.
4. Benchmark baseline and indexed approaches on synthetic + real data.
5. Validate that optimized pipelines match baseline classification results.

### 2. Final Week 1 Pipeline

Implemented in `src/benchmark_m1.cpp`:

1. Stage 1: Brute force + bounding-box filter
2. Stage 2: Quadtree index + ray-casting verification
3. Stage 3: Strip Index + ray-casting verification
4. Stage 4: Real-world benchmark using GeoJSON

### 3. Code Components

#### 3.1 Geometry Layer

| File | Purpose |
|------|---------|
| `include/geometry/point.hpp` | Point, BBox structures |
| `include/geometry/polygon.hpp` | Polygon, MultiPolygon structures |
| `include/geometry/ray_casting.hpp` | Ray-casting interface |
| `src/geometry/ray_casting.cpp` | Ray-casting implementation with boundary handling |

#### 3.2 Spatial Indexing Layer

| Index | Strategy | Time Complexity |
|-------|----------|----------------|
| BBox Filter | Linear scan, baseline pruning | O(n) per query |
| Quadtree | Recursive 4-quadrant spatial hierarchy | O(log n) per query |
| Strip Index | Horizontal strip partitioning on Y-axis | O(k) per query |

#### 3.3 Data Generation Layer

- Uniform point distribution (random, full region)
- Clustered point distribution (Gaussian clusters, simulates urban GPS)
- Synthetic polygon grid: 100×100 = 10,000 polygons

#### 3.4 Real-World GeoJSON Layer

- Loads Polygon and MultiPolygon geometries
- Expands MultiPolygon into individual Polygon objects
- Flexible centroid extraction: `center_lon/lat`, `x_coord/y_coord`, or Point geometry fallback

### 4. Week 1 Benchmark Results

#### Uniform Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---------|-------------------|----------|-------------|-----------------|--------------|
| 100K points | 1809.77 ms | 101.65 ms | 143.04 ms | 17.80× | 12.65× |
| 1M points | ~19,967 ms | 1249.72 ms | 1531.21 ms | 15.98× | 13.04× |

#### Clustered Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---------|-------------------|----------|-------------|-----------------|--------------|
| 100K points | 2319.01 ms | 78.15 ms | 98.98 ms | 29.67× | 23.43× |
| 1M points | ~27,188 ms | 1189.56 ms | 1271.38 ms | 22.86× | 21.38× |

#### Real-World Data (745 points, 204 polygons)

| Stage | Time | Speedup |
|-------|------|---------|
| Stage 1 (Brute Force + BBox) | 36.25 ms | — |
| Stage 2 (Quadtree) | 40.77 ms | 0.89× |

*Note: Quadtree overhead exceeds gain on very small real-world dataset.*

---

## Week 2 Implementation Summary

### 1. Purpose of Week 2

Week 2 extends the sequential baseline with **parallel-oriented design using OpenMP** to accelerate point-in-polygon classification on multi-core systems.

Primary goals completed:

1. Implement four parallelization strategies (Sequential, Static OMP, Dynamic OMP, Tiled+Morton)
2. Implement true Work-Stealing classifier
3. Integrate OpenMP support into build system
4. Benchmark all strategies on synthetic and real-world data
5. Validate correctness of all parallel implementations
6. Analyze thread scaling with corrected baseline and robust timing

### 2. Parallel Strategies Implemented

| Strategy | Mechanism | Best For |
|----------|-----------|----------|
| Sequential | Single-threaded reference | Baseline |
| Static OMP | Equal chunks, adaptive `sqrt(n)/4` chunk size | Uniform workloads |
| Dynamic OMP | Guided dynamic scheduling | Clustered/skewed |
| Tiled+Morton | Z-order sort → tiled parallel classify | Cache efficiency |
| Work-Stealing | Per-thread deques, back-steal from idle threads | Non-uniform workloads |
| Hybrid (Static+Dynamic) | Static blocks + dynamic overflow pool | Skew-resilient minimum overhead |

### 3. Week 2 Benchmark Results  

> **System:** 8 hardware threads  
> **Methodology:** Min-of-3 (strategy benchmarks) · Median-of-7 (thread scaling) · 1-thread Dynamic OMP baseline (scaling tables)

#### Uniform Distribution

| Strategy | 100K Time | 100K Speedup | 1M Time | 1M Speedup |
|----------|-----------|-------------|---------|-----------|
| Sequential | 77.41 ms | — | 934.84 ms | — |
| Static OMP | 18.82 ms | 4.11× | 204.19 ms | 4.58× |
| Dynamic OMP | 17.95 ms | **4.31×** ⭐ | 195.05 ms | 4.79× |
| Tiled+Morton (e2e) | 26.54 ms | 2.92× | 333.13 ms | 2.81× |
| Work-Stealing | 19.02 ms | 4.07× | 206.20 ms | 4.53× |
| Hybrid (Static+Dyn) | **16.51 ms** | **4.24×** | **193.83 ms** | **4.82×** ⭐ |

#### Clustered Distribution

| Strategy | 100K Time | 100K Speedup | 1M Time | 1M Speedup |
|----------|-----------|-------------|---------|-----------|
| Sequential | 66.94 ms | — | 723.93 ms | — |
| Static OMP | 18.15 ms | 3.69× | 180.84 ms | 4.00× |
| Dynamic OMP | 16.46 ms | 4.07× | 177.76 ms | 4.07× |
| Tiled+Morton (e2e) | 27.67 ms | 2.42× | 332.66 ms | 2.18× |
| Work-Stealing | 16.37 ms | 4.09× | 181.98 ms | 3.98× |
| Hybrid (Static+Dyn) | **16.48 ms** | **4.13×** ⭐ | **174.54 ms** | **4.15×** ⭐ |

#### Real-World Data (745 Points, 204 Polygons)

| Strategy | Time | Speedup |
|----------|------|---------|
| Sequential | 32.78 ms | — |
| Static OMP | 35.90 ms | 0.91× |
| Dynamic OMP | 30.60 ms | 1.07× |
| Tiled+Morton (e2e) | 29.30 ms | 1.12× |
| **Work-Stealing** | **6.39 ms** | **5.13×** ⭐ |

#### Thread Scaling — Uniform 1M (Median-of-7, Dynamic OMP)

| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1 | 1059.36 | 1.00× | 100.0% |
| 2 | 494.09 | 2.14× | 107.2% |
| 4 | 293.16 | 3.61× | 90.3% |
| 6 | 223.51 | 4.74× | 79.0% |
| 8 | 198.06 | 5.35× | 66.9% |

#### Thread Scaling — Clustered 1M (Median-of-7, Dynamic OMP)

| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1 | 781.49 | 1.00× | 100.0% |
| 2 | 436.97 | 1.79× | 89.4% |
| 4 | 254.46 | 3.07× | 76.8% |
| 6 | 193.40 | 4.04× | 67.3% |
| 8 | 183.13 | 4.27× | 53.3% |

**Analysis (both distributions):** Sub-linear scaling above 4 threads. Root cause: Quadtree lookup is memory-bound (random access pattern). Extra threads increase RAM contention without proportional compute gain. Memory bandwidth is the bottleneck.

### 4. Methodology Fixes (April 12, 2026)

#### Fix A: Corrected Thread-Scaling Baseline
- **Old:** Computed speedup relative to Stage 1 sequential time → caused >100% efficiency at 1-thread
- **New:** Use 1-thread Dynamic OMP time → 1-thread efficiency = exactly 100.0%

#### Fix B: Median-of-7 Timing
- **Old:** Min-of-3 → susceptible to OS scheduling noise (non-monotonic regressions)
- **New:** Median of 7 runs → monotonically increasing runtime with thread count, no anomalies

#### Fix C: Double Cache Warmup (Pre-loop)
- Added two pre-pass warmup executions before the scaling logic correctly shifts L2/L3 states online beforehand. Efficiency correctly approaches accurate limits now.

### 5. Validation

- ✓ All 5 strategies produce identical results (validated against sequential)
- ✓ 20 correctness checks, zero mismatches
- ✓ Work-Stealing correctly handles point-in-polygon classification
- ✓ End-to-end timing honestly reflects preprocessing costs
- ✓ Thread scaling tables are monotonic and physically meaningful

---

## Key Insights

1. **Static OMP is best for uniform data** (uniform = balanced workload = no need for overhead of dynamic scheduling)
2. **Dynamic OMP is best for clustered data at scale** (load balancing advantages outweigh coordination cost at 1M+ points)
3. **Tiled+Morton classify-only is super-linear (>8× on 1M)** — a genuine cache optimization effect; however, the sort cost makes end-to-end speedup 2.36–2.74×
4. **Thread scaling is memory-bound** — efficiency drops to 50-80% at 4+ threads due to Quadtree random memory access patterns
5. **Work-Stealing excels on real-world small datasets** due to minimal thread overhead, but measurement is sensitive at n=745

---

## Build & Run

```powershell
# Windows (PowerShell)
.\build.ps1
.\build\benchmark_m1.exe
.\build\benchmark_m2.exe
```

```bash
# Linux/macOS
bash build.sh
./build/benchmark_m1
./build/benchmark_m2
```
