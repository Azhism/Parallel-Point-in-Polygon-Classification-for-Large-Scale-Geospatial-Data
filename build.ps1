$ErrorActionPreference = "Stop"
$OUTDIR = "build"
New-Item -ItemType Directory -Force -Path $OUTDIR | Out-Null

# ============================================================
# Compiler detection: MinGW g++ (preferred) or fallback
# ============================================================
$CXX = "g++"
$CXXFLAGS = @("-O3", "-std=c++17", "-I./include", "-Wall", "-Wextra", "-fopenmp")
$LDFLAGS = @("-fopenmp")
$STACK_FLAGS = @("-Wl,--stack,67108864")

# Verify g++ exists and supports OpenMP
try {
    $null = Get-Command $CXX -ErrorAction Stop
    Write-Host "[*] Using compiler: $CXX ($(& $CXX --version 2>&1 | Select-Object -First 1))"
} catch {
    Write-Host "[ERROR] g++ not found. Install MinGW-w64:"
    Write-Host "  Option 1: winget install -e --id MSYS2.MSYS2 (then pacman -S mingw-w64-x86_64-gcc)"
    Write-Host "  Option 2: Download from https://www.mingw-w64.org/"
    Write-Host "  Make sure g++ is in your PATH."
    exit 1
}

Write-Host "[*] Compiling core library..."
& $CXX @CXXFLAGS -c src/geometry/point.cpp -o $OUTDIR/point.o
& $CXX @CXXFLAGS -c src/geometry/polygon.cpp -o $OUTDIR/polygon.o
& $CXX @CXXFLAGS -c src/geometry/ray_casting.cpp -o $OUTDIR/ray_casting.o
& $CXX @CXXFLAGS -c src/index/bbox_filter.cpp -o $OUTDIR/bbox_filter.o
& $CXX @CXXFLAGS -c src/index/quadtree.cpp -o $OUTDIR/quadtree.o
& $CXX @CXXFLAGS -c src/index/strip_index.cpp -o $OUTDIR/strip_index.o
& $CXX @CXXFLAGS -c src/index/geojson_loader.cpp -o $OUTDIR/geojson_loader.o
& $CXX @CXXFLAGS -c src/generator/uniform_distribution.cpp -o $OUTDIR/uniform_distribution.o
& $CXX @CXXFLAGS -c src/generator/clustered_distribution.cpp -o $OUTDIR/clustered_distribution.o
& $CXX @CXXFLAGS -c src/generator/polygon_loader.cpp -o $OUTDIR/polygon_loader.o

Write-Host "[*] Compiling parallel library..."
& $CXX @CXXFLAGS -c src/parallel/parallel_classifier.cpp -o $OUTDIR/parallel_classifier.o
& $CXX @CXXFLAGS -c src/parallel/work_stealing_classifier.cpp -o $OUTDIR/work_stealing_classifier.o

Write-Host "[*] Creating static library..."
ar rcs $OUTDIR/libpip_core.a $OUTDIR/point.o $OUTDIR/polygon.o $OUTDIR/ray_casting.o $OUTDIR/bbox_filter.o $OUTDIR/quadtree.o $OUTDIR/strip_index.o $OUTDIR/geojson_loader.o $OUTDIR/uniform_distribution.o $OUTDIR/clustered_distribution.o $OUTDIR/polygon_loader.o $OUTDIR/parallel_classifier.o $OUTDIR/work_stealing_classifier.o

Write-Host "[*] Compiling unit tests..."
& $CXX @CXXFLAGS tests/test_ray_casting.cpp $OUTDIR/libpip_core.a @LDFLAGS -o $OUTDIR/test_ray_casting.exe

Write-Host "[*] Compiling benchmarks..."
& $CXX @CXXFLAGS src/benchmark_m1.cpp $OUTDIR/libpip_core.a @LDFLAGS @STACK_FLAGS -o $OUTDIR/benchmark_m1.exe
& $CXX @CXXFLAGS src/benchmark_m2.cpp $OUTDIR/libpip_core.a @LDFLAGS @STACK_FLAGS -o $OUTDIR/benchmark_m2.exe

Write-Host "[*] Build completed successfully!"
Write-Host "Run: .\$OUTDIR\test_ray_casting.exe"
Write-Host "Run: .\$OUTDIR\benchmark_m1.exe"
Write-Host "Run: .\$OUTDIR\benchmark_m2.exe"

# ============================================================
# Milestone 3: MPI distributed targets (conditional)
# ============================================================
$mpiFound = $false
try {
    $null = Get-Command mpicxx -ErrorAction Stop
    $mpiFound = $true
} catch {}

if ($mpiFound) {
    Write-Host ""
    Write-Host "[*] MPI detected (mpicxx) — compiling distributed targets..."
    $MPICXXFLAGS = @("-O3", "-std=c++17", "-I./include", "-Wall", "-Wextra", "-fopenmp")

    mpicxx @MPICXXFLAGS -c src/distributed/mpi_types.cpp -o $OUTDIR/mpi_types.o
    mpicxx @MPICXXFLAGS -c src/distributed/mpi_classifier.cpp -o $OUTDIR/mpi_classifier.o
    mpicxx @MPICXXFLAGS -c src/distributed/spatial_partitioner.cpp -o $OUTDIR/spatial_partitioner.o

    Write-Host "[*] Compiling MPI test..."
    mpicxx @MPICXXFLAGS tests/test_mpi_classifier.cpp $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o $OUTDIR/libpip_core.a -fopenmp -o $OUTDIR/test_mpi_classifier.exe

    Write-Host "[*] Compiling MPI benchmark..."
    mpicxx @MPICXXFLAGS src/benchmark_m3.cpp $OUTDIR/mpi_types.o $OUTDIR/mpi_classifier.o $OUTDIR/spatial_partitioner.o $OUTDIR/libpip_core.a -fopenmp -o $OUTDIR/benchmark_m3.exe

    Write-Host "[*] MPI targets built!"
    Write-Host "Run: mpiexec -np 4 .\$OUTDIR\benchmark_m3.exe"
} else {
    Write-Host ""
    Write-Host "[!] mpicxx not found — skipping MPI targets (M1 and M2 still work)."
    Write-Host "    Install MS-MPI: https://learn.microsoft.com/en-us/message-passing-interface/microsoft-mpi"
    Write-Host "    Or install OpenMPI via MSYS2: pacman -S mingw-w64-x86_64-openmpi"
}
