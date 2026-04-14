$ErrorActionPreference = "Stop"
$OUTDIR = "build"
$STACK_FLAGS = "-Wl,--stack,67108864"
mkdir -Force $OUTDIR | Out-Null

Write-Host "[*] Compiling core library..."
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/geometry/point.cpp -o $OUTDIR/point.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/geometry/polygon.cpp -o $OUTDIR/polygon.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/geometry/ray_casting.cpp -o $OUTDIR/ray_casting.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/index/bbox_filter.cpp -o $OUTDIR/bbox_filter.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/index/quadtree.cpp -o $OUTDIR/quadtree.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/index/strip_index.cpp -o $OUTDIR/strip_index.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/index/geojson_loader.cpp -o $OUTDIR/geojson_loader.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/generator/uniform_distribution.cpp -o $OUTDIR/uniform_distribution.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/generator/clustered_distribution.cpp -o $OUTDIR/clustered_distribution.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/generator/polygon_loader.cpp -o $OUTDIR/polygon_loader.o

Write-Host "[*] Compiling parallel library..."
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/parallel/parallel_classifier.cpp -o $OUTDIR/parallel_classifier.o
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp -c src/parallel/work_stealing_classifier.cpp -o $OUTDIR/work_stealing_classifier.o

Write-Host "[*] Creating static library..."
ar rcs $OUTDIR/libpip_core.a $OUTDIR/point.o $OUTDIR/polygon.o $OUTDIR/ray_casting.o $OUTDIR/bbox_filter.o $OUTDIR/quadtree.o $OUTDIR/strip_index.o $OUTDIR/geojson_loader.o $OUTDIR/uniform_distribution.o $OUTDIR/clustered_distribution.o $OUTDIR/polygon_loader.o $OUTDIR/parallel_classifier.o $OUTDIR/work_stealing_classifier.o

Write-Host "[*] Compiling unit tests..."
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp tests/test_ray_casting.cpp $OUTDIR/libpip_core.a -fopenmp -o $OUTDIR/test_ray_casting

Write-Host "[*] Compiling benchmarks..."
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp src/benchmark_m1.cpp $OUTDIR/libpip_core.a -fopenmp $STACK_FLAGS -o $OUTDIR/benchmark_m1
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp src/benchmark_m2.cpp $OUTDIR/libpip_core.a -fopenmp $STACK_FLAGS -o $OUTDIR/benchmark_m2

Write-Host "[*] Build completed successfully!"

# Milestone 3: MPI distributed targets (conditional)
$mpiFound = $false
try {
    $null = Get-Command mpicxx -ErrorAction Stop
    $mpiFound = $true
} catch {}

if ($mpiFound) {
    Write-Host ""
    Write-Host "[*] MPI detected — compiling distributed targets..."
    $MPICXXFLAGS = "-O3 -std=c++17 -I./include -Wall -Wextra -fopenmp"

    mpicxx $MPICXXFLAGS.Split(" ") -c src/distributed/mpi_types.cpp -o $OUTDIR/mpi_types.o
    mpicxx $MPICXXFLAGS.Split(" ") -c src/distributed/mpi_classifier.cpp -o $OUTDIR/mpi_classifier.o
    mpicxx $MPICXXFLAGS.Split(" ") -c src/distributed/spatial_partitioner.cpp -o $OUTDIR/spatial_partitioner.o

    Write-Host "[*] Compiling MPI test..."
    mpicxx $MPICXXFLAGS.Split(" ") tests/test_mpi_classifier.cpp $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o $OUTDIR/libpip_core.a -fopenmp -o $OUTDIR/test_mpi_classifier

    Write-Host "[*] Compiling MPI benchmark..."
    mpicxx $MPICXXFLAGS.Split(" ") src/benchmark_m3.cpp $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o $OUTDIR/libpip_core.a -fopenmp -o $OUTDIR/benchmark_m3

    Write-Host "[*] MPI targets built! Run: mpirun -np 4 .\build\benchmark_m3"
} else {
    Write-Host ""
    Write-Host "[!] mpicxx not found — skipping MPI targets."
    Write-Host "    Install MS-MPI or OpenMPI for distributed execution."
}
