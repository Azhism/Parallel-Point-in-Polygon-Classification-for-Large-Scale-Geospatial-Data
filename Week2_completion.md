# Week 2 Completion Report: Parallel Point-in-Polygon Classification

## Status

Week 2 parallelization is complete.

Implemented pipeline in `src/benchmark_m2.cpp` with five parallel strategies using OpenMP.

## Week 2 Scope

Week 2 extends the sequential Week 1 baseline with parallel-oriented design using OpenMP for multi-core acceleration.

Primary goals completed:

1. Implement sequential baseline for comparison
2. Build static OpenMP parallel strategy
3. Build dynamic OpenMP load-balanced strategy
4. Build tiled + Morton-sorted cache-optimized strategy
5. Build true work-stealing classifier
6. Benchmark all strategies on synthetic data
7. Validate correctness of parallel implementations
8. Analyze thread scaling and efficiency
9. **Bonus**: Implement Hybrid (Static + Dynamic) OpenMP scheduling

## What Was Implemented

### 1. Parallel Classifier Infrastructure

**File:** `include/parallel/parallel_classifier.hpp` and `src/parallel/parallel_classifier.cpp`

**Components:**

1. **Unified dispatcher** - Routes to appropriate parallel strategy
2. **Classification strategies:**
   - SEQUENTIAL: Non-parallel baseline
   - STATIC_OMP: Static work distribution across threads
   - DYNAMIC_OMP: Dynamic load-balanced distribution
   - TILED: Tiled + Morton-order sorted optimization
   - HYBRID_OMP: Static blocks + dynamic overflow queue

### 2. Parallelization Strategies

#### Strategy 1: Sequential Baseline
- Single-threaded point-in-polygon classification
- Uses quadtree index for candidate filtering
- Baseline for speedup comparison

#### Strategy 2: Static OpenMP
- Work divided equally among threads at compile time
- Adaptive chunk size (`sqrt(n)/4`) to avoid L3 cache thrashing
- **Best for uniform distributions**

#### Strategy 3: Dynamic OpenMP
- Work units distributed dynamically to threads
- Thread takes next available work when idle
- Better load balancing for skewed workloads
- **Best for clustered distributions**

#### Strategy 4: Tiled + Morton-Sorted
- Points reorganized using Morton (Z-order) curve
- Spatial locality improves cache efficiency
- Tiled processing for better cache utilization
- End-to-end timing includes sort cost (honest reporting)

#### Strategy 5: Work-Stealing
- Per-thread task deques with stealing from other threads' queues
- Thread works front-to-back on own deque; steals from back of victim deque
- Minimizes lock contention; adapts to non-uniform workloads

#### Strategy 6: Hybrid Static+Dynamic (Bonus)
- Each thread gets a pre-assigned static chunk (e.g. 80% total work) with zero overhead
- Fast-finishing threads pull remaining work from an atomic dynamic overflow pool
- Balances minimal scheduling overhead (like Static) with resilience against skewed distributions (like Dynamic)

### 3. Build System Updates

**File:** `build.ps1` (PowerShell) / `build.sh` (bash)

**Changes:**
- Added `-fopenmp` compiler flag for OpenMP support
- Added `parallel_classifier.cpp` and `work_stealing_classifier.cpp` compilation
- Updated to compile both benchmark_m1 and benchmark_m2

### 4. Data Integration

**Synthetic data:**
- Uniform distribution (100K, 1M points)
- Clustered distribution (100K, 1M points)
- Polygon grid: 10,000 square regions (100×100)

---

## Benchmark Results (Week 2 - Final - April 12, 2026)

> **System:** 4 hardware threads  
> **Methodology:** Min-of-3 (with 1 warmup) for strategy benchmarks; **Median-of-7** for thread-scaling tables  
> **Thread-scaling baseline:** 1-thread Dynamic OMP time (not sequential stage time)

### Strategy Overview

```
- Static OMP   : Equal chunks pre-divided at runtime start
- Dynamic OMP  : Guided chunk distribution (approximates work-stealing)
- Tiled+Morton : Z-order sort for cache locality + parallel classify
- Work-Stealing: True per-thread deque stealing (Stage 5)
- Hybrid       : Static blocks + dynamic overflow (Stage 6)
Timing: Tiled+Morton reports both classify-only and end-to-end.
```

---

### Synthetic Data: Uniform Distribution

#### 100K Points
| Strategy | Time | Throughput | Speedup |
|----------|------|-----------|---------|
| Sequential | 74.42 ms | 1,343,648.97 pts/sec | — |
| Static OMP | 38.68 ms | 2,585,248.57 pts/sec | 1.92× |
| Dynamic OMP | 40.36 ms | 2,477,571.78 pts/sec | 1.84× |
| Tiled+Morton (e2e) | 43.02 ms | 2,324,619.11 pts/sec | 1.73× |
| **Work-Stealing** | **38.43 ms** | **2,602,303.04 pts/sec** | **1.94×** ⭐ |
| Hybrid (Static+Dynamic) | 39.03 ms | 2,561,954.46 pts/sec | 1.91× |

#### 1M Points
| Strategy | Time | Throughput | Speedup |
|----------|------|-----------|---------|
| Sequential | 771.88 ms | 1,295,530.28 pts/sec | — |
| **Static OMP** | **392.61 ms** | **2,547,067.90 pts/sec** | **1.97×** ⭐ |
| Dynamic OMP | 401.49 ms | 2,490,720.20 pts/sec | 1.92× |
| Tiled+Morton (e2e) | 505.45 ms | 1,978,448.76 pts/sec | 1.53× |
| Work-Stealing | 395.62 ms | 2,527,658.27 pts/sec | 1.95× |
| Hybrid (Static+Dynamic) | 403.59 ms | 2,477,756.56 pts/sec | 1.91× |

#### Thread Scaling (1M Uniform, Dynamic OMP, median-of-7)
| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1 | 770.22 | 1.00× | 100.0% |
| 2 | 493.65 | 1.56× | 78.0% |
| 4 | 404.45 | 1.90× | 47.6% |

**Analysis:** 2→4 thread scaling is sub-linear (1.56× → 1.90×).  
Root cause from benchmark notes: quadtree lookup is memory-bound, so additional threads increase memory bandwidth contention.

---

### Synthetic Data: Clustered Distribution

#### 100K Points
| Strategy | Time | Throughput | Speedup |
|----------|------|-----------|---------|
| Sequential | 45.64 ms | 2,190,954.86 pts/sec | — |
| Static OMP | 30.58 ms | 3,270,250.21 pts/sec | 1.49× |
| Dynamic OMP | 30.62 ms | 3,265,668.68 pts/sec | 1.49× |
| Tiled+Morton (e2e) | 39.81 ms | 2,511,931.68 pts/sec | 1.15× |
| Work-Stealing | 34.27 ms | 2,917,757.18 pts/sec | 1.33× |
| **Hybrid (Static+Dynamic)** | **30.23 ms** | **3,308,245.80 pts/sec** | **1.51×** ⭐ |

#### 1M Points
| Strategy | Time | Throughput | Speedup |
|----------|------|-----------|---------|
| Sequential | 513.52 ms | 1,947,333.21 pts/sec | — |
| Static OMP | 294.68 ms | 3,393,473.60 pts/sec | 1.74× |
| **Dynamic OMP** | **291.89 ms** | **3,425,968.08 pts/sec** | **1.76×** ⭐ |
| Tiled+Morton (e2e) | 493.89 ms | 2,024,731.28 pts/sec | 1.04× |
| Work-Stealing | 301.92 ms | 3,312,152.12 pts/sec | 1.70× |
| Hybrid (Static+Dynamic) | 302.05 ms | 3,310,664.11 pts/sec | 1.70× |

#### Thread Scaling (1M Clustered, Dynamic OMP, median-of-7)
| Threads | Time (ms) | Speedup | Efficiency |
|---------|-----------|---------|-----------|
| 1 | 604.22 | 1.00× | 100.0% |
| 2 | 519.50 | 1.16× | 58.2% |
| 4 | 342.07 | 1.77× | 44.2% |

**Analysis:** 2→4 thread scaling is sub-linear (1.16× → 1.77×).  
Efficiency: 44.2% at 4 threads, indicating overhead and memory effects are significant for this workload shape.

---

## Fixes Applied to Thread-Scaling Methodology (April 12, 2026)

### Fix A: Corrected Scaling Baseline
- **Problem:** Thread-scaling speedups were computed against the Stage 1 sequential time. By the time the scaling loop runs, caches are warm, making the 1-thread Dynamic OMP run faster than the "cold" sequential baseline — producing impossible >100% efficiency.
- **Solution:** Use the **1-thread Dynamic OMP time** as the scaling baseline. Ensures 1-thread efficiency = 100.0% exactly, and all subsequent efficiencies are honest relative measures.

### Fix B: Median-of-7 Timing
- **Problem:** Min-of-3 allowed OS scheduling spikes to create non-monotonic anomalies (e.g., 2-thread runs occasionally approaching 4-thread timings unexpectedly).
- **Solution:** Collect **7 timed runs** and take the **median**. Robust against single-run OS outliers while still reflecting achievable performance.

### Fix C: Double Cache Warmup (Pre-loop)
- **Problem:** The thread-scaling efficiency for 2 threads on 1M Uniform points still showed >100% initially because 1 thread was running on cold cache vs. 2 threads running on hot cache.
- **Solution:** Introduced a 2-pass sequence of classification strictly prior to the scaling loop to bring standard L2/L3 caches completely online, finally pushing valid 1->2 scalability properties inline (80-90% efficiency expected instead of >100%).

---

## Validation Summary

| Validation Type | Count | Status |
|-----------------|-------|--------|
| Correctness checks | 20 | ✓ PASS |
| Data mismatches | 0 | ✓ NONE |
| Polygon ID matches | 100% | ✓ MATCH |
| Build success | 1 | ✓ SUCCESS |

All parallel implementations produce identical results to sequential baseline.

---

## Summary of Week 2

**Milestone 2 Status:** ✓ COMPLETE

**Key Achievements:**
- Up to **1.97×** speedup on 1M uniform points (Static OMP, 4 threads)
- Up to **1.76×** speedup on 1M clustered points (Dynamic OMP, 4 threads)
- **Monotonic, honest thread-scaling** tables with proper efficiency metrics
- Memory bottleneck identified: Quadtree random-access patterns cap scaling on 4-thread system
- All 20 correctness validations pass (zero mismatches)

**Remaining Notes:**
- Tiled+Morton sort cost dominates for 1M points — classify-only throughput is higher, but end-to-end gain is limited by O(n log n) sorting overhead
- Uniform and clustered distributions both show sub-linear scaling from 2 to 4 threads on this machine due to memory bandwidth pressure

## Files Modified/Created

| File | Type | Change |
|------|------|--------|
| `include/parallel/parallel_classifier.hpp` | Header | Used |
| `src/parallel/parallel_classifier.cpp` | Source | Adaptive chunk size, cache-aware scheduling |
| `include/parallel/work_stealing_classifier.hpp` | Header | NEW |
| `src/parallel/work_stealing_classifier.cpp` | Source | NEW — Work-stealing implementation |
| `src/benchmark_m2.cpp` | Source | Fixed thread-scaling baseline + median-of-7 |
| `build.ps1` | Build | PowerShell build (Windows-native, no WSL) |
| `Week2_completion.md` | Docs | Updated with corrected results |
