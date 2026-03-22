# How to Run the Codebase

## Overview

This project is a **Point-in-Polygon Classification** system that benchmarks sequential and indexed approaches for classifying large point sets against polygon regions.

The codebase is written in **C++17** and designed to run on:
- **Windows** (with Git Bash or WSL)
- **Linux** (native)
- **macOS** (native)

---

## System Requirements

### Mandatory
- **C++17 compatible compiler** (g++, clang, or MSVC)
- **Bash shell** (`build.sh` is bash-based)
- **GNU toolchain**: `g++`, `ar` (archiver)

### Optional
- Boost library (not strictly required; nlohmann/json is already included)

### File Space
- ~50 MB for source code
- ~200 MB for build artifacts
- ~20 MB for GeoJSON data files

---

## Quick Start (3 Steps)

### Step 1: Navigate to Project Directory

```bash
cd /path/to/project  # Or: d:\Classess\PDC\Project on Windows
```

### Step 2: Build the Project

```bash
bash build.sh
```

**Expected output:**
```
[*] Compiling core library...
[*] Creating static library...
[*] Compiling unit tests...
[*] Compiling benchmark...
[*] Build completed successfully!
```

### Step 3: Run Benchmark

```bash
bash -lc "./build/benchmark_m1"
```

Or run unit tests first:

```bash
bash -lc "./build/test_ray_casting"
```

---

## Platform-Specific Instructions

### Windows (Git Bash or WSL)

If using **Git Bash**:
```bash
# Git Bash (use forward slashes)
cd d:/Classess/PDC/Project
bash build.sh
bash -lc "./build/benchmark_m1"
```

If using **WSL (Windows Subsystem for Linux)**:
```bash
# WSL (Linux path conventions)
cd /mnt/d/Classess/PDC/Project
bash build.sh
bash -lc "./build/benchmark_m1"
```

**Important Windows Note:**  
Do **NOT** use PowerShell's native `.exe` files. The binary is named `benchmark_m1` (no extension) and must be executed via `bash -lc` to ensure correct execution.

### Linux / macOS (Native)

```bash
cd /path/to/project
bash build.sh
bash -lc "./build/benchmark_m1"
```

---

## Understanding the Build Output

When `build.sh` runs, it:

1. **Compiles source objects:**
   - Geometry layer (point, polygon, ray-casting)
   - Indexing layer (bbox_filter, quadtree, strip_index, geojson_loader)
   - Generator layer (uniform_distribution, clustered_distribution, polygon_loader)

2. **Creates a static library:** `build/libpip_core.a`
   - Links all compiled object files
   - Used by both tests and benchmark executable

3. **Compiles executables:**
   - `build/test_ray_casting` — Unit tests for geometric classification
   - `build/benchmark_m1` — Main benchmark program

**Typical build time:** 2–5 seconds on modern hardware

---

## Running the Benchmark

### Standard Mode (100K–1M Points)

```bash
bash -lc "./build/benchmark_m1"
```

**Duration:** ~60–120 seconds  
**Output includes:**
- Synthetic data benchmarks (uniform and clustered distributions)
- Real-world data benchmarks (Pakistan administrative polygons/centroids)
- Speedup comparisons between indexing strategies

### Large-Scale Mode (10M–100M Points)

```bash
bash -lc "./build/benchmark_m1 --large"
```

or

```bash
bash -lc "./build/benchmark_m1 --large-scale"
```

**Duration:** 5–10 minutes  
**Features:**
- Batch processing to manage memory
- Skips brute-force stage (expensive at scale)
- Compares Quadtree vs Strip Index only

---

## Running Unit Tests

```bash
bash -lc "./build/test_ray_casting"
```

**Expected output:**
```
=== Ray-Casting Unit Tests ===
Test: Simple square...
  PASSED
Test: Polygon with hole...
  PASSED
Test: Circle polygon...
  PASSED
Test: Edge cases...
  PASSED

All tests PASSED!
```

---

## Output Explained

### Benchmark Output Structure

```
=== Milestone 1: Sequential Baseline with Spatial Indexing ===
Creating polygon grid (100x100)...
  Polygons: 10000

=== uniform distribution ===
Dataset: 100000 points
  Stage 1 (Brute force + BBox): 39049.34 pts/sec (2560.86 ms)
  Stage 2 (Quadtree index): 1135370.32 pts/sec (88.08 ms) [build: 4.41 ms]
    Speedup: 29.08x
  Stage 3 (Strip Index): 1239960.32 pts/sec (80.65 ms) [build: 1.65 ms]
    Speedup: 31.75x
✓ Results validated: all queries match.
```

**Key metrics:**
- **pts/sec**: Throughput (points classified per second)
- **ms**: Total execution time in milliseconds
- **[build: X ms]**: Time to construct the index
- **Speedup**: Ratio of Stage 1 time to indexed stage time

### Real-World Data Output

```
=== REAL-WORLD DATA BENCHMARK ===
Attempting to load real-world data...
✓ Loaded 204 polygons from pak_admin2.geojson
✓ Loaded 745 centroids from pak_admincentroids.geojson
✓ Successfully loaded real data
  Polygons: 204
  Points: 745
```

The project includes `pak_admin2.geojson` and `pak_admincentroids.geojson` (Pakistan administrative data).

---


## Interpreting Benchmark Results

### Expected Performance (on modern hardware)

**Synthetic data (100K–1M points, 10,000 polygons):**
- Brute force: 30–50K points/sec
- Quadtree: 1M–2M points/sec (25–43x speedup)
- Strip Index: 1M–1.5M points/sec (27–38x speedup)

**Real-world data (745 points, 204 polygons):**
- Benefit is smaller due to low query volume
- Speedup typically 1–2x (overhead of indexing on tiny datasets)

**Validation:**
- All stages should report ✓ Results validated: all queries match
- Any mismatch indicates a correctness issue

---




## Project Structure for Reference

```
.
├── build.sh                           # Build script (use this)
├── CMakeLists.txt                     # CMake config (optional)
├── include/
│   ├── geometry/          # Point, Polygon, Ray-casting
│   ├── index/             # BBoxFilter, Quadtree, StripIndex, GeoJSONLoader
│   ├── generator/         # Distribution, PolygonLoader
│   └── nlohmann/          # JSON parsing library
├── src/
│   ├── benchmark_m1.cpp   # Main benchmark program
│   ├── geometry/          # Implementation files
│   ├── index/
│   └── generator/
├── tests/
│   └── test_ray_casting.cpp
├── pak_admin2.geojson     # Real data: Pakistan polygons
├── pak_admincentroids.geojson  # Real data: centroid points
├── README.md              # Project overview
└── SUMMARY.md             # Week 1 implementation details
```

---

## Next Steps

1. **Run the benchmark:** Follow Quick Start above
2. **Review results:** Compare speedup numbers
3. **Read documentation:** See `README.md` and `SUMMARY.md` for context
4. **Modify and experiment:** Edit parameters in `src/benchmark_m1.cpp` for different tests

---
