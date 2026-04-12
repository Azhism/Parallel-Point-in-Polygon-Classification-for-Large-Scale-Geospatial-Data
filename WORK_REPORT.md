# Work Report: Point-in-Polygon Classification Project

**Project:** Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data  
**Status:** Week 2 Complete — Thread-Scaling Methodology Corrected (April 12, 2026)  
**Date:** April 12, 2026  
**Milestone:** Milestone 2 (Parallel Optimization)

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

## What Is Remaining

### High Priority

1. Refine workload partitioning to improve memory locality for quadtree-heavy paths.
2. Add more focused profiling (cache misses, memory bandwidth, hotspot attribution) to guide the next optimization step.
3. Expand automated test coverage for edge cases in polygon boundaries and multipolygon hole handling.

### Medium Priority

1. Improve documentation of strategy-selection guidance (when to prefer static, dynamic, hybrid, or work-stealing).
2. Add reproducibility notes for benchmark environment settings (thread affinity, power profile, background load).
3. Consolidate report files so milestone narrative and benchmark artifacts stay synchronized.

### Optional / Stretch

1. Evaluate alternative spatial indexing strategies or quadtree layout changes to reduce random memory access.
2. Investigate SIMD-friendly geometric kernels for candidate polygon checks.
3. Add larger synthetic scenarios to stress-test scalability beyond current baseline sizes.

---

## Current Status Snapshot

1. Milestone 1: Complete.
2. Milestone 2: Functionally complete and validated on synthetic datasets.
3. Main risk: Memory-bound behavior limits near-linear thread scaling.
4. Next focus: Profiling-guided optimization and stronger reproducibility/testing discipline.

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
| Memory-bound quadtree traversal limits scaling at higher threads | Medium | Identified; profiling and locality improvements planned |

---

## Conclusion

**Week 2 is complete and verified (April 12, 2026).**

1. Five parallel strategies are implemented and integrated into one benchmark pipeline.
2. Core correctness is validated across synthetic workloads.
3. Major technical risk is understood: memory behavior, not compute, is the primary limiter at higher parallelism.
4. The project is ready for a next phase focused on profiling-driven optimization and test/report hardening.
