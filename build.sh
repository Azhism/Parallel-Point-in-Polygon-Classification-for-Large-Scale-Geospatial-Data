#!/bin/bash
# Build script for Milestone 1 - Point-in-Polygon Classification using g++

set -e

CXXFLAGS="-O3 -std=c++17 -I./include -Wall -Wextra"
OUTDIR="build"

mkdir -p $OUTDIR

echo "[*] Compiling core library..."

g++ $CXXFLAGS -c src/geometry/point.cpp -o $OUTDIR/point.o
g++ $CXXFLAGS -c src/geometry/polygon.cpp -o $OUTDIR/polygon.o
g++ $CXXFLAGS -c src/geometry/ray_casting.cpp -o $OUTDIR/ray_casting.o
g++ $CXXFLAGS -c src/index/rtree_index.cpp -o $OUTDIR/rtree_index.o
g++ $CXXFLAGS -c src/index/bbox_filter.cpp -o $OUTDIR/bbox_filter.o
g++ $CXXFLAGS -c src/generator/uniform_distribution.cpp -o $OUTDIR/uniform_distribution.o
g++ $CXXFLAGS -c src/generator/clustered_distribution.cpp -o $OUTDIR/clustered_distribution.o
g++ $CXXFLAGS -c src/generator/polygon_loader.cpp -o $OUTDIR/polygon_loader.o

echo "[*] Creating static library..."
ar rcs $OUTDIR/libpip_core.a $OUTDIR/point.o $OUTDIR/polygon.o $OUTDIR/ray_casting.o $OUTDIR/rtree_index.o $OUTDIR/bbox_filter.o $OUTDIR/uniform_distribution.o $OUTDIR/clustered_distribution.o $OUTDIR/polygon_loader.o

echo "[*] Compiling unit tests..."
g++ $CXXFLAGS tests/test_ray_casting.cpp $OUTDIR/libpip_core.a -o $OUTDIR/test_ray_casting

echo "[*] Compiling simple test..."
g++ $CXXFLAGS tests/simple_test.cpp $OUTDIR/libpip_core.a -o $OUTDIR/simple_test

echo "[*] Compiling benchmark..."
g++ $CXXFLAGS src/benchmark_m1.cpp $OUTDIR/libpip_core.a -o $OUTDIR/benchmark_m1

echo "[*] Build completed successfully!"
echo "Run: $OUTDIR/test_ray_casting"
echo "Run: $OUTDIR/benchmark_m1"
