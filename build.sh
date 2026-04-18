#!/bin/bash
# Build script for Point-in-Polygon Classification with Parallel Support

set -e
cd "$(dirname "$0")"

CXXFLAGS="-O3 -std=c++17 -I./include -Wall -Wextra -fopenmp"
LDFLAGS="-fopenmp"
STACK_FLAGS="-Wl,--stack,67108864"
OUTDIR="build"

mkdir -p $OUTDIR

echo "[*] Compiling core library..."

g++ $CXXFLAGS -c src/geometry/point.cpp -o $OUTDIR/point.o
g++ $CXXFLAGS -c src/geometry/polygon.cpp -o $OUTDIR/polygon.o
g++ $CXXFLAGS -c src/geometry/ray_casting.cpp -o $OUTDIR/ray_casting.o
g++ $CXXFLAGS -c src/index/bbox_filter.cpp -o $OUTDIR/bbox_filter.o
g++ $CXXFLAGS -c src/index/quadtree.cpp -o $OUTDIR/quadtree.o
g++ $CXXFLAGS -c src/index/strip_index.cpp -o $OUTDIR/strip_index.o
g++ $CXXFLAGS -c src/index/geojson_loader.cpp -o $OUTDIR/geojson_loader.o
g++ $CXXFLAGS -c src/generator/uniform_distribution.cpp -o $OUTDIR/uniform_distribution.o
g++ $CXXFLAGS -c src/generator/clustered_distribution.cpp -o $OUTDIR/clustered_distribution.o
g++ $CXXFLAGS -c src/generator/polygon_loader.cpp -o $OUTDIR/polygon_loader.o

echo "[*] Compiling parallel library..."
g++ $CXXFLAGS -c src/parallel/parallel_classifier.cpp -o $OUTDIR/parallel_classifier.o
g++ $CXXFLAGS -c src/parallel/work_stealing_classifier.cpp -o $OUTDIR/work_stealing_classifier.o

echo "[*] Creating static library..."
ar rcs $OUTDIR/libpip_core.a $OUTDIR/point.o $OUTDIR/polygon.o $OUTDIR/ray_casting.o $OUTDIR/bbox_filter.o $OUTDIR/quadtree.o $OUTDIR/strip_index.o $OUTDIR/geojson_loader.o $OUTDIR/uniform_distribution.o $OUTDIR/clustered_distribution.o $OUTDIR/polygon_loader.o $OUTDIR/parallel_classifier.o $OUTDIR/work_stealing_classifier.o

echo "[*] Compiling unit tests..."
g++ $CXXFLAGS tests/test_ray_casting.cpp $OUTDIR/libpip_core.a $LDFLAGS -o $OUTDIR/test_ray_casting

echo "[*] Compiling benchmarks..."
g++ $CXXFLAGS src/benchmark_m1.cpp $OUTDIR/libpip_core.a $LDFLAGS $STACK_FLAGS -o $OUTDIR/benchmark_m1
g++ $CXXFLAGS src/benchmark_m2.cpp $OUTDIR/libpip_core.a $LDFLAGS $STACK_FLAGS -o $OUTDIR/benchmark_m2
g++ $CXXFLAGS src/benchmark_m3.cpp $OUTDIR/libpip_core.a $LDFLAGS $STACK_FLAGS -o $OUTDIR/benchmark_m3
g++ -O3 -std=c++17 -I./include -Wall -Wextra src/worker_main.cpp $OUTDIR/libpip_core.a -o $OUTDIR/worker

echo "[*] Build completed successfully!"
echo "Run: $OUTDIR/test_ray_casting"
echo "Run: $OUTDIR/benchmark_m1"
echo "Run: $OUTDIR/benchmark_m2"
echo "Run: $OUTDIR/benchmark_m3"
echo "Worker executable: $OUTDIR/worker"
