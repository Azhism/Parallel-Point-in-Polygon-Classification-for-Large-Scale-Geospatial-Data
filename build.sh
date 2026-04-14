#!/bin/bash
# Build script for Point-in-Polygon Classification with Parallel Support

set -e

# Detect compiler: prefer g++-15/14/13 (GCC with native OpenMP) over Apple Clang
CXX="g++"
for candidate in g++-15 g++-14 g++-13; do
    if command -v "$candidate" &> /dev/null; then
        CXX="$candidate"
        break
    fi
done
echo "[*] Using compiler: $CXX ($($CXX --version | head -1))"

CXXFLAGS="-O3 -std=c++17 -I./include -Wall -Wextra -fopenmp"
LDFLAGS="-fopenmp"
# Stack flag only works on Linux linker; skip on macOS
if [[ "$(uname)" == "Darwin" ]]; then
    STACK_FLAGS=""
else
    STACK_FLAGS="-Wl,--stack,67108864"
fi
OUTDIR="build"

mkdir -p $OUTDIR

echo "[*] Compiling core library..."

$CXX $CXXFLAGS -c src/geometry/point.cpp -o $OUTDIR/point.o
$CXX $CXXFLAGS -c src/geometry/polygon.cpp -o $OUTDIR/polygon.o
$CXX $CXXFLAGS -c src/geometry/ray_casting.cpp -o $OUTDIR/ray_casting.o
$CXX $CXXFLAGS -c src/index/bbox_filter.cpp -o $OUTDIR/bbox_filter.o
$CXX $CXXFLAGS -c src/index/quadtree.cpp -o $OUTDIR/quadtree.o
$CXX $CXXFLAGS -c src/index/strip_index.cpp -o $OUTDIR/strip_index.o
$CXX $CXXFLAGS -c src/index/geojson_loader.cpp -o $OUTDIR/geojson_loader.o
$CXX $CXXFLAGS -c src/generator/uniform_distribution.cpp -o $OUTDIR/uniform_distribution.o
$CXX $CXXFLAGS -c src/generator/clustered_distribution.cpp -o $OUTDIR/clustered_distribution.o
$CXX $CXXFLAGS -c src/generator/polygon_loader.cpp -o $OUTDIR/polygon_loader.o

echo "[*] Compiling parallel library..."
$CXX $CXXFLAGS -c src/parallel/parallel_classifier.cpp -o $OUTDIR/parallel_classifier.o
$CXX $CXXFLAGS -c src/parallel/work_stealing_classifier.cpp -o $OUTDIR/work_stealing_classifier.o

echo "[*] Creating static library..."
ar rcs $OUTDIR/libpip_core.a $OUTDIR/point.o $OUTDIR/polygon.o $OUTDIR/ray_casting.o $OUTDIR/bbox_filter.o $OUTDIR/quadtree.o $OUTDIR/strip_index.o $OUTDIR/geojson_loader.o $OUTDIR/uniform_distribution.o $OUTDIR/clustered_distribution.o $OUTDIR/polygon_loader.o $OUTDIR/parallel_classifier.o $OUTDIR/work_stealing_classifier.o

echo "[*] Compiling unit tests..."
$CXX $CXXFLAGS tests/test_ray_casting.cpp $OUTDIR/libpip_core.a $LDFLAGS -o $OUTDIR/test_ray_casting

echo "[*] Compiling benchmarks..."
$CXX $CXXFLAGS src/benchmark_m1.cpp $OUTDIR/libpip_core.a $LDFLAGS $STACK_FLAGS -o $OUTDIR/benchmark_m1
$CXX $CXXFLAGS src/benchmark_m2.cpp $OUTDIR/libpip_core.a $LDFLAGS $STACK_FLAGS -o $OUTDIR/benchmark_m2

echo "[*] Build completed successfully!"
echo "Run: $OUTDIR/test_ray_casting"
echo "Run: $OUTDIR/benchmark_m1"
echo "Run: $OUTDIR/benchmark_m2"

# ============================================================
# Milestone 3: MPI distributed targets (conditional)
# ============================================================
# Detect MPI: get compile/link flags from mpicxx, but use our $CXX compiler
# This ensures OpenMP works (mpicxx wraps Apple Clang which lacks -fopenmp)
if command -v mpicxx &> /dev/null; then
    echo ""
    echo "[*] MPI detected"
    MPI_CFLAGS=$(mpicxx --showme:compile 2>/dev/null)
    MPI_LDFLAGS=$(mpicxx --showme:link 2>/dev/null)
    MPICXXFLAGS="$CXXFLAGS $MPI_CFLAGS"
    MPI_LINK="$LDFLAGS $MPI_LDFLAGS"

    echo "[*] Compiling MPI distributed library..."
    $CXX $MPICXXFLAGS -c src/distributed/mpi_types.cpp -o $OUTDIR/mpi_types.o
    $CXX $MPICXXFLAGS -c src/distributed/mpi_classifier.cpp -o $OUTDIR/mpi_classifier.o
    $CXX $MPICXXFLAGS -c src/distributed/spatial_partitioner.cpp -o $OUTDIR/spatial_partitioner.o

    echo "[*] Compiling MPI test..."
    $CXX $MPICXXFLAGS tests/test_mpi_classifier.cpp \
        $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o \
        $OUTDIR/libpip_core.a \
        $MPI_LINK -o $OUTDIR/test_mpi_classifier

    echo "[*] Compiling MPI benchmark..."
    $CXX $MPICXXFLAGS src/benchmark_m3.cpp \
        $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o \
        $OUTDIR/libpip_core.a \
        $MPI_LINK -o $OUTDIR/benchmark_m3

    echo "[*] MPI targets built successfully!"
    echo "Run: mpirun -np 2 $OUTDIR/test_mpi_classifier"
    echo "Run: mpirun -np 4 $OUTDIR/benchmark_m3"
    echo "Run: OMP_NUM_THREADS=2 mpirun -np 4 $OUTDIR/benchmark_m3"
else
    echo ""
    echo "[!] mpicxx not found — skipping MPI targets."
    echo "    Install: brew install open-mpi (macOS) or apt install libopenmpi-dev (Linux)"
fi
