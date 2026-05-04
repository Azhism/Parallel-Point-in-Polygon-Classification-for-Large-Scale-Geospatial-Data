# Run Instructions

## Requirements

- C++17-capable compiler (g++)
- GNU binutils (ar)
- OpenMP support (`-fopenmp`)
- Windows: MSYS2/UCRT64 toolchain (`C:\msys64\ucrt64\bin`)

Optional: CMake 3.16+

---

## Windows (Recommended)

Ensure MSYS2 DLLs are in PATH first:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
```

Build everything (M1, M2, M3, worker):

```powershell
.\build.ps1
```

Run unit tests:

```powershell
.\build\test_ray_casting.exe
```

---

## Milestone 1 — Sequential Baseline

```powershell
.\build\benchmark_m1.exe
```

What to expect: brute force, quadtree, and strip index stages with speedup comparisons and real-world Pakistan data validation.

---

## Milestone 2 — Parallel Classification

```powershell
.\build\benchmark_m2.exe
```

What to expect: sequential baseline vs static OMP, dynamic OMP, tiled+Morton, work-stealing, and hybrid strategies. Thread-scaling tables at 1/2/4 threads.

---

## Milestone 3 — Scalable Batch and Multi-Process

Quick run (100K + 1M, fast):

```powershell
.\build\benchmark_m3.exe --quick
```

Default run (1M + 10M + scaling tables):

```powershell
.\build\benchmark_m3.exe
```

Full required-scale run (includes 100M):

```powershell
.\build\benchmark_m3.exe --full
```

Useful flags:

```text
--quick                  Run 100K and 1M only
--full                   Include 100M benchmark
--sizes 1000000,10000000 Comma-separated size list
--workers 4              Number of spatial workers
--batch-size 250000      Streaming batch size
--skip-scaling           Skip strong/weak scaling tables
```

What to expect: batched master/worker throughput at 1M/10M/100M points, replication vs sharding comparison with checksums, multi-process IPC timing (write/worker/read), and strong/weak scaling tables.

---

## Linux / macOS

```bash
bash build.sh
./build/benchmark_m1
./build/benchmark_m2
./build/benchmark_m3 --full
```

---

## CMake (Optional)

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

---

## Important Notes

1. Always run binaries from the project root so relative data paths resolve:
   - `pak_admin3.geojson`
   - `pak_admin2.geojson`
   - `pak_admincentroids.geojson`
2. Current benchmark outputs are `bench_m1_live.txt`, `bench_m2_live.txt`, `bench_m3_live.txt` — these are the ground truth for latest timings.
3. If M3 multi-process IPC runs, it writes temporary files under `ipc/` — that directory is created automatically.

To refresh the live benchmark files:

```powershell
.\build\benchmark_m1.exe | Tee-Object -FilePath bench_m1_live.txt
.\build\benchmark_m2.exe | Tee-Object -FilePath bench_m2_live.txt
.\build\benchmark_m3.exe | Tee-Object -FilePath bench_m3_live.txt
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Build fails with OpenMP errors | Verify `-fopenmp` is supported by your toolchain |
| Real-world section fails to load | Confirm GeoJSON files are in project root; run from project root |
| M3 worker.exe not found | Build with `build.ps1` before running `benchmark_m3.exe` |
| Benchmark slower than expected | Close background apps; compare median runs, not single runs |
