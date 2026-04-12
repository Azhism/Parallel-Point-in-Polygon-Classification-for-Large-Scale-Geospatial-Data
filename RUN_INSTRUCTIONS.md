# Run Instructions

## Overview

This project provides two benchmark executables:

1. Milestone 1: sequential baseline with spatial indexing
2. Milestone 2: parallel classification strategies with OpenMP

Use these instructions from the project root directory.

---

## Requirements

1. C++17-capable compiler (g++)
2. GNU binutils (ar)
3. OpenMP support enabled in compiler toolchain

Optional:

1. CMake 3.16+ (if you want CMake workflow)

---

## Windows (Recommended)

Build with PowerShell script:

```powershell
Set-Location D:\Classess\PDC\Project\Parallel-Point-in-Polygon-Classification-for-Large-Scale-Geospatial-Data
.\build.ps1
```

Run unit test:

```powershell
.\build\test_ray_casting.exe
```

Run Milestone 1 benchmark:

```powershell
.\build\benchmark_m1.exe
```

Run Milestone 2 benchmark:

```powershell
.\build\benchmark_m2.exe
```

Important:

1. Run binaries from project root so relative data paths resolve correctly.
2. If benchmark stops at real-world loading, confirm the data files exist in project root:
   - pak_admin2.geojson
   - pak_admincentroids.geojson

---

## Linux and macOS

Build:

```bash
cd /path/to/Parallel-Point-in-Polygon-Classification-for-Large-Scale-Geospatial-Data
bash build.sh
```

Run unit test:

```bash
./build/test_ray_casting
```

Run Milestone 1 benchmark:

```bash
./build/benchmark_m1
```

Run Milestone 2 benchmark:

```bash
./build/benchmark_m2
```

---

## CMake Workflow (Optional)

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Then run benchmark binaries from the produced build output folder.

---

## What to Expect

1. benchmark_m1 prints sequential/indexed synthetic benchmark stages and validation checks.
2. benchmark_m2 prints sequential and parallel strategies with speedups and thread-scaling summaries.
3. Validation lines should report matching results against baseline.

---

## Troubleshooting

1. Build fails with OpenMP errors:
   - Verify toolchain supports -fopenmp.

2. Real-world section fails to load:
   - Ensure pak_admin2.geojson and pak_admincentroids.geojson are present in project root.
   - Run executable from project root, not from a different working directory.

3. Benchmark is slower than expected:
   - Close background heavy applications.
   - Re-run and compare medians instead of single-run numbers.

