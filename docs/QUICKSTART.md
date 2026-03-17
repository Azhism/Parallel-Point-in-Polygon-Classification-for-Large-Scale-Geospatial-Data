# Quick Start Guide

## Build

```bash
# Unix/Linux/WSL
./build.sh

# Windows
build.bat

# Manual
mkdir -p build && cd build
g++ -std=c++17 -I../include ../src/**/*.cpp -o benchmark_m1
```

## Run Benchmark

```bash
./build/benchmark_m1
```

## Expected Output

```
Creating polygon grid (100x100)...
  Polygons: 10000

Dataset: 100000 points
  Stage 1 (Brute force + BBox): 10756.42 pts/sec (9296.78 ms)
  Stage 2 (Quadtree index): 138740.22 pts/sec (720.77 ms)
  ✓ Speedup: 12.90x

Dataset: 1000000 points
  Stage 1 (Brute force + BBox): 9320.51 pts/sec (107290.30 ms)
  Stage 2 (Quadtree index): 142346.72 pts/sec (7025.10 ms)
  ✓ Speedup: 15.27x
```

## Key Achievements

✅ **12-19x speedup** on 10,000 polygons  
✅ **All results validated** for correctness  
✅ **Realistic dataset size** (city-scale)  
✅ **Ready for parallelization** (M2)

## Architecture

```
benchmark_m1
├── Brute Force + Bbox Filter (baseline)
└── Quadtree Index (optimized)
    ├── Build: O(P log P)
    └── Query: O(log D) 
```

See `README.md` for full documentation.
