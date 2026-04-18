$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
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
g++ -O3 -std=c++17 -I./include -Wall -Wextra -fopenmp src/benchmark_m3.cpp $OUTDIR/libpip_core.a -fopenmp $STACK_FLAGS -o $OUTDIR/benchmark_m3
g++ -O3 -std=c++17 -I./include -Wall -Wextra src/worker_main.cpp $OUTDIR/libpip_core.a -o $OUTDIR/worker.exe

Write-Host "[*] Build completed successfully!"
