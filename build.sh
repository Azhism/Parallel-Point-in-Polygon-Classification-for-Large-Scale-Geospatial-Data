#!/bin/bash
# Build script for Point-in-Polygon Classification with Parallel Support
# Supports: Linux (GCC), macOS (Homebrew GCC), WSL

set -e

# ============================================================
# Compiler detection
# ============================================================
# On macOS: Apple Clang lacks -fopenmp, so we need Homebrew GCC (g++-15/14/13)
# On Linux/WSL: plain g++ IS GCC and supports -fopenmp natively

detect_compiler() {
    # Try versioned GCC first (needed on macOS)
    for candidate in g++-15 g++-14 g++-13 g++-12; do
        if command -v "$candidate" &> /dev/null; then
            echo "$candidate"
            return
        fi
    done

    # Check if plain g++ supports OpenMP (true on Linux, false on macOS Apple Clang)
    if command -v g++ &> /dev/null; then
        if g++ -fopenmp -x c++ -E - < /dev/null &> /dev/null; then
            echo "g++"
            return
        fi
    fi

    echo ""
}

CXX=$(detect_compiler)

if [[ -z "$CXX" ]]; then
    echo "[ERROR] No C++ compiler with OpenMP support found."
    echo "  macOS: brew install gcc"
    echo "  Linux: sudo apt install g++    (or yum install gcc-c++)"
    echo "  WSL:   sudo apt install g++"
    exit 1
fi

echo "[*] Using compiler: $CXX ($($CXX --version | head -1))"

CXXFLAGS="-O3 -std=c++17 -I./include -Wall -Wextra -fopenmp"
LDFLAGS="-fopenmp"
OUTDIR="build"

# Stack size flag: GNU ld on Linux supports --stack, macOS does not
STACK_FLAGS=""
if [[ "$(uname)" == "Linux" ]]; then
    # Check if linker supports --stack (GNU ld does, lld may not)
    if $CXX -Wl,--stack,67108864 -x c++ -o /dev/null - <<< "int main(){}" &> /dev/null; then
        STACK_FLAGS="-Wl,--stack,67108864"
    fi
fi

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
if command -v mpicxx &> /dev/null; then
    echo ""
    echo "[*] MPI detected"

    # Get MPI compile/link flags
    # Open MPI uses --showme, MPICH uses -show
    MPI_CFLAGS=""
    MPI_LDFLAGS=""
    if mpicxx --showme:compile &> /dev/null; then
        # Open MPI
        MPI_CFLAGS=$(mpicxx --showme:compile 2>/dev/null)
        MPI_LDFLAGS=$(mpicxx --showme:link 2>/dev/null)
    elif mpicxx -show &> /dev/null; then
        # MPICH — parse flags from full compile command
        MPI_SHOW=$(mpicxx -show 2>/dev/null)
        MPI_CFLAGS=$(echo "$MPI_SHOW" | grep -oP '(-I\S+)' | tr '\n' ' ')
        MPI_LDFLAGS=$(echo "$MPI_SHOW" | grep -oP '(-L\S+|-l\S+|-Wl,\S+)' | tr '\n' ' ')
    fi

    # If we got MPI flags, use our GCC compiler with them (ensures OpenMP works)
    # If not, fall back to mpicxx directly
    if [[ -n "$MPI_CFLAGS" || -n "$MPI_LDFLAGS" ]]; then
        MPICXX="$CXX"
        MPICXXFLAGS="$CXXFLAGS $MPI_CFLAGS"
        MPI_LINK="$LDFLAGS $MPI_LDFLAGS"
    else
        MPICXX="mpicxx"
        MPICXXFLAGS="$CXXFLAGS"
        MPI_LINK="$LDFLAGS"
    fi

    echo "[*] Compiling MPI distributed library..."
    $MPICXX $MPICXXFLAGS -c src/distributed/mpi_types.cpp -o $OUTDIR/mpi_types.o
    $MPICXX $MPICXXFLAGS -c src/distributed/mpi_classifier.cpp -o $OUTDIR/mpi_classifier.o
    $MPICXX $MPICXXFLAGS -c src/distributed/spatial_partitioner.cpp -o $OUTDIR/spatial_partitioner.o

    echo "[*] Compiling MPI test..."
    $MPICXX $MPICXXFLAGS tests/test_mpi_classifier.cpp \
        $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o \
        $OUTDIR/libpip_core.a \
        $MPI_LINK -o $OUTDIR/test_mpi_classifier

    echo "[*] Compiling MPI benchmark..."
    $MPICXX $MPICXXFLAGS src/benchmark_m3.cpp \
        $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o \
        $OUTDIR/libpip_core.a \
        $MPI_LINK -o $OUTDIR/benchmark_m3

    echo "[*] MPI targets built successfully!"
    echo "Run: mpirun -np 2 $OUTDIR/test_mpi_classifier"
    echo "Run: mpirun -np 4 $OUTDIR/benchmark_m3"
else
    echo ""
    echo "[!] mpicxx not found — skipping MPI targets (M1 and M2 still work)."
    echo "    Install MPI:"
    echo "      macOS:  brew install open-mpi"
    echo "      Ubuntu: sudo apt install libopenmpi-dev openmpi-bin"
    echo "      Fedora: sudo dnf install openmpi-devel"
    echo "      WSL:    sudo apt install libopenmpi-dev openmpi-bin"
fi
