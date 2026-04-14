# Work Report: Point-in-Polygon Classification Project

**Project:** Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data  
**Status:** Week 3 Complete — Distributed MPI Execution (April 14, 2026)  
**Date:** April 14, 2026  
**Milestone:** Milestone 3 (Scalable Batch Processing and Distributed Execution)

---

## Completed Work

### Week 1 — Sequential Baseline with Spatial Indexing

| Component | Status | Details |
|-----------|--------|---------|
| **Point & Polygon structures** | ✓ Complete | `Point`, `BBox`, `Polygon`, `MultiPolygon` |
| **Ray-casting algorithm** | ✓ Complete | Robust with edge/vertex/hole handling |
| **BBox Filter** | ✓ Complete | Linear scan baseline pruning |
| **Quadtree Index** | ✓ Complete | Recursive spatial hierarchy, `MAX_DEPTH=12`, `MAX_PER_LEAF=6` |
| **Strip Index** | ✓ Complete | Horizontal strip partitioning |
| **Uniform distribution generator** | ✓ Complete | `std::mt19937_64` uniform RNG |
| **Clustered distribution generator** | ✓ Complete | Gaussian clusters over random centers |
| **GeoJSON loader** | ✓ Complete | Polygon, MultiPolygon, flexible centroid schemas |
| **Unit tests** | ✓ Complete | Ray-casting edge cases pass |

**Files:** `include/geometry/`, `include/index/`, `include/generator/`, `src/geometry/`, `src/index/`, `src/generator/`, `tests/test_ray_casting.cpp`

---

### Week 2 — Parallel Classification

| Component | Status | Details |
|-----------|--------|---------|
| **Unified dispatcher** | ✓ Complete | Routes to 5 parallel strategies |
| **Static OMP** | ✓ Complete | `schedule(static, sqrt(n)/4)` — cache-aware chunk sizing |
| **Dynamic OMP** | ✓ Complete | `schedule(dynamic, sqrt(n)/4)` — adaptive load balancing |
| **Tiled+Morton** | ✓ Complete | High-radix radix sort + tiled classify (honest e2e timing) |
| **Work-Stealing** | ✓ Complete | Per-thread deques, back-steal from victim thread |
| **Hybrid (Static+Dyn)** | ✓ Complete | Static chunks + atomic dynamic overflow pool |
| **Thread-scaling table** | ✓ Fixed | Median-of-7, 1-thread baseline, Double Cache Warmup |

**Files:** `include/parallel/parallel_classifier.hpp`, `src/parallel/parallel_classifier.cpp`, `include/parallel/work_stealing_classifier.hpp`, `src/parallel/work_stealing_classifier.cpp`, `src/benchmark_m2.cpp`

---

## What We Have Done

### Architecture and Core Implementation

1. Built a complete sequential point-in-polygon pipeline with robust geometry primitives and ray-casting.
2. Added two spatial indexing paths (quadtree and strip index) to reduce candidate polygon checks.
3. Implemented synthetic data generators for repeatable testing under uniform and clustered workloads.
4. Integrated GeoJSON parsing to support polygon and multipolygon input formats.

### Parallelization Work (Week 2)

1. Added a unified classification dispatcher to switch strategies cleanly.
2. Implemented five execution modes:
	- Static OpenMP scheduling
	- Dynamic OpenMP scheduling
	- Tiled + Morton-order preprocessing with parallel classification
	- Work-stealing with per-thread deques
	- Hybrid static+dynamic scheduling
3. Kept correctness checks tied to sequential output for every strategy.
4. Updated build workflow for Windows and Linux/macOS with OpenMP support.

### Quality and Verification

1. Verified all synthetic runs against sequential reference output.
2. Added/finalized thread-scaling measurement methodology to reduce noisy conclusions.
3. Confirmed project compiles and runs for both milestone executables.

---

## Problems Encountered and How We Addressed Them

### 1. Measurement Methodology Produced Misleading Scaling

- Problem: Early scaling comparisons mixed cold-cache and warm-cache runs, which could make efficiency appear unrealistically high.
- Action taken: Rebased scaling to 1-thread dynamic timing for the scaling loop and added extra warmup.
- Outcome: Scaling interpretation is now more defensible and internally consistent.

### 2. Run-to-Run Timing Variability

- Problem: A small number of runs allowed OS scheduling spikes to skew conclusions.
- Action taken: Switched thread-scaling aggregation to median-of-7 runs.
- Outcome: Reduced outlier influence and improved reproducibility of trend analysis.

### 3. Parallel Gains Limited at Higher Thread Counts

- Problem: Quadtree traversal is memory-access heavy; additional threads increase contention and reduce scaling efficiency.
- Action taken: Tested multiple scheduling policies (static/dynamic/hybrid/work-stealing) and cache-oriented tiled approach.
- Outcome: Parallel speedup is real but sub-linear on current hardware; bottleneck is identified rather than hidden.

### 4. Platform Friction on Windows

- Problem: Shell-script flow is inconvenient in a native Windows environment.
- Action taken: Maintained PowerShell-native build path.
- Outcome: Reliable local build/run workflow without WSL dependency.

---

### Week 3 — Distributed MPI Execution

| Component | Status | Details |
|-----------|--------|---------|
| **MPI type registration** | ✓ Complete | Custom datatypes for Point and ClassificationResult |
| **Polygon serialization** | ✓ Complete | Serialize/deserialize polygons for MPI broadcast/send |
| **Spatial partitioner** | ✓ Complete | Strip and grid decomposition, polygon filtering with overlap |
| **MPI classifier (replicate)** | ✓ Complete | Broadcast polygons, scatter points, gather results |
| **MPI classifier (partition)** | ✓ Complete | Filter polygons per region, renumber IDs for quadtree |
| **Batch processing** | ✓ Complete | 10M batches for 100M+ point support |
| **Correctness tests** | ✓ Complete | All modes pass with 2 and 4 MPI ranks |
| **Benchmark harness** | ✓ Complete | Full matrix: sizes x distributions x polygon modes |
| **Build system** | ✓ Complete | Conditional MPI compilation in build.sh, CMakeLists.txt, build.ps1 |

**Files:** `include/distributed/`, `src/distributed/`, `src/benchmark_m3.cpp`, `tests/test_mpi_classifier.cpp`

---

## What Is Remaining

All three milestones are functionally complete.

### Optional Enhancements

1. Multi-node distributed benchmarking (requires cluster access)
2. SIMD vectorization for ray-casting
3. Alternative spatial indexing (R-tree) for comparison
4. Profiling-guided cache optimization for quadtree traversal

---

## Current Status Snapshot

1. Milestone 1: Complete — sequential baseline with 3 spatial indices.
2. Milestone 2: Complete — 5 OpenMP parallel strategies, thread scaling analysis.
3. Milestone 3: Complete — distributed MPI execution, batch processing, scalability analysis.
4. All correctness tests pass across all milestones.

## Build & Run

```powershell
# Windows PowerShell
.\build.ps1
.\build\benchmark_m1.exe
.\build\benchmark_m2.exe
```

```bash
# Linux / macOS
bash build.sh
./build/benchmark_m1
./build/benchmark_m2
```

---

## Known Issues & Mitigations

| Issue | Impact | Status |
|-------|--------|--------|
| `build.sh` requires WSL on Windows | Medium | Mitigated by `build.ps1` (PowerShell equivalent) |
| Tiled+Morton sort cost dominates at 1M+ points | Low | Documented; honest e2e timing reported |
| Memory-bound quadtree traversal limits scaling at higher threads | Medium | Identified as shared memory bandwidth bottleneck |
| Spatial strip partitioning causes load imbalance on clustered data | Medium | Documented; replication mode handles this better |
| All MPI ranks on same machine limits distributed scaling demonstration | Low | Analysis notes that true gains require multi-node |

---

## Conclusion

**All three milestones are complete and verified (April 14, 2026).**

1. Sequential baseline with 3 spatial indices (quadtree achieves 55x speedup on clustered data).
2. Five OpenMP parallel strategies validated and benchmarked with thread scaling analysis.
3. Distributed MPI execution with hybrid MPI+OpenMP, batch processing for 100M points, and two polygon distribution modes.
4. Core correctness validated across all strategies, distributions, and rank configurations.
5. Key trade-offs analyzed: polygon replication vs partitioning, communication vs computation overhead.
