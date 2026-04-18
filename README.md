# Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data

This project classifies large GPS-like point sets against polygon regions using sequential spatial indexing, shared-memory parallelism, and a batch-based master/worker execution model.

## Status

Complete through Milestone 3.

Canonical benchmark output files:

1. `milestone_1.txt` - raw output from `build\benchmark_m1.exe`
2. `milestone_2.txt` - raw output from `build\benchmark_m2.exe`
3. `milestone_3.txt` - raw output from `build\benchmark_m3.exe --full`

Use those three files as the ground truth for timings. Older report files may contain historical runs from earlier sessions.

## Milestone 1

Sequential baseline with spatial indexing.

Implemented:

1. Ray-casting point-in-polygon classifier.
2. Edge and vertex boundary handling.
3. Holes and MultiPolygon support.
4. Bounding-box filtering.
5. Quadtree spatial index.
6. Strip index.
7. Uniform and clustered synthetic point generators.
8. Real-world Pakistan GeoJSON dataset.

Polygon creation:

1. Synthetic polygons are created with `PolygonLoader::create_grid`.
2. The default benchmark grid is 100 x 100 = 10,000 square polygons.
3. Real polygons are loaded from `pak_admin2.geojson`.

Canonical M1 highlights from `milestone_1.txt`:

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Best Indexed Speedup |
|---|---:|---:|---:|---:|
| 100K uniform | 2405.80 ms | 90.45 ms | 87.62 ms | 27.46x |
| 1M uniform | 16500.25 ms | 787.09 ms | 876.24 ms | 20.96x |
| 100K clustered | 1568.85 ms | 50.07 ms | 73.75 ms | 31.33x |
| 1M clustered | 16553.33 ms | 520.58 ms | 711.06 ms | 31.80x |

Real-world data:

1. `pak_admin2.geojson`: 204 loaded polygons.
2. `pak_admincentroids.geojson`: 745 loaded points.
3. Results validate against the brute-force baseline.

## Milestone 2

Parallel point classification and load balancing.

Implemented:

1. Sequential quadtree baseline.
2. Static OpenMP.
3. Dynamic OpenMP.
4. Tiled + Morton/Z-order cache-locality strategy.
5. True work-stealing with per-thread deques.
6. Hybrid static + dynamic scheduling.
7. Thread-scaling tables.

Current machine note:

```text
Available threads: 4
```

There is no valid 8-thread result in the current canonical benchmark output.

Canonical M2 highlights from `milestone_2.txt`:

| Dataset | Sequential | Best Strategy | Best Time | Speedup |
|---|---:|---|---:|---:|
| 100K uniform | 91.75 ms | Hybrid | 36.79 ms | 2.49x |
| 100K clustered | 53.73 ms | Hybrid | 41.01 ms | 1.31x |
| 1M uniform | 845.84 ms | Hybrid | 390.97 ms | 2.16x |
| 1M clustered | 546.53 ms | Hybrid | 302.38 ms | 1.81x |

Thread-scaling interpretation:

1. Scaling is sub-linear at 4 threads.
2. The clustered 1M run barely improves from 2 to 4 threads.
3. Root cause reported by the benchmark: quadtree lookup is memory-bound and random-access heavy.

SIMD note:

SIMD vectorization was optional in the assignment and was not implemented.

## Milestone 3

Scalable batch processing, in-memory master/worker execution, and Windows file-based multi-process IPC.

Implemented:

1. Batch processing of large point sets.
2. Spatial point partitioning across workers.
3. Worker-local quadtree indices.
4. Polygon replication mode.
5. Spatial polygon sharding mode.
6. Compact result aggregation using counts/checksums.
7. Strong scaling.
8. Weak scaling.
9. 1M, 10M, and 100M point benchmarks.
10. Uniform and clustered distributions.
11. Separate `worker.exe` process path using binary input/result files and `CreateProcess`.

MPI / multi-process note:

The assignment says "MPI or multi-process." MPI is not used because the current native Windows/MSYS2 environment does not have MPI configured. Instead, Milestone 3 now includes an actual single-machine multi-process implementation:

1. `benchmark_m3.exe` acts as the master process.
2. The master writes binary IPC files under `ipc/`.
3. The master launches `build/worker.exe` child processes with `CreateProcess`.
4. Each worker reads its point partition and polygon file, builds its own quadtree, classifies points, and writes a result file.
5. The master waits for all workers and aggregates count/checksum results.

The in-memory worker model is still retained for the large 100M benchmark path and for strong/weak scaling tables. The process path makes communication cost explicit through write, worker, read, and total timing.

The design explores the distributed setting:

1. Points are partitioned spatially across workers.
2. Polygons are either replicated or sharded.
3. Each worker owns an independent polygon/index context.
4. Results are aggregated centrally.
5. The same layout can still map directly to MPI ranks in a future extension.

Canonical M3 large-scale results from `milestone_3.txt`:

| Dataset | Distribution | Workers | Class Time | Class Throughput | Total Time |
|---:|---|---:|---:|---:|---:|
| 1M | uniform | 4 | 518.11 ms | 1,930,081 pts/sec | 587.91 ms |
| 1M | clustered | 4 | 358.84 ms | 2,786,737 pts/sec | 481.86 ms |
| 10M | uniform | 4 | 5298.39 ms | 1,887,365 pts/sec | 5713.48 ms |
| 10M | clustered | 4 | 3255.79 ms | 3,071,455 pts/sec | 4151.00 ms |
| 100M | uniform | 4 | 46222.22 ms | 2,163,462 pts/sec | 49846.42 ms |
| 100M | clustered | 4 | 34277.60 ms | 2,917,357 pts/sec | 43051.56 ms |

Replication vs sharding from `milestone_3.txt`:

| Distribution | Mode | Indexed Polygon Copies | Checksum |
|---|---|---:|---|
| uniform | replicated | 40,000 | `e38e4d8a13e3441d` |
| uniform | sharded | 10,600 | `e38e4d8a13e3441d` |
| clustered | replicated | 40,000 | `c98c4f58897e6cf9` |
| clustered | sharded | 10,600 | `c98c4f58897e6cf9` |

Matching checksums show that sharding preserved classification results.

Multi-process IPC results from `milestone_3.txt`:

| Distribution | Mode | Write Time | Worker Time | Read Time | Total Time | Checksum |
|---|---|---:|---:|---:|---:|---|
| uniform | replicated | 494.25 ms | 1137.89 ms | 0.72 ms | 1633.86 ms | `e38e4d8a13e3441d` |
| uniform | sharded | 746.46 ms | 1076.23 ms | 1.80 ms | 1824.56 ms | `e38e4d8a13e3441d` |
| clustered | replicated | 492.64 ms | 1097.83 ms | 0.63 ms | 1591.21 ms | `c98c4f58897e6cf9` |
| clustered | sharded | 542.19 ms | 1067.45 ms | 0.60 ms | 1610.47 ms | `c98c4f58897e6cf9` |

The process checksums match the in-memory benchmark checksums for the same 1M datasets, so the IPC worker path preserves correctness while exposing communication overhead.

Weak-scaling note:

Weak scaling is not perfectly flat. The latest canonical run improves at 4 workers versus 2 workers, but the 2-worker point is slower than ideal. This should still be discussed as real overhead from scheduling, replicated indexing, memory bandwidth pressure during quadtree traversal, and batch partition/aggregation.

## Build And Run

On Windows, use the MSYS2 runtime first in `PATH`:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
.\build.ps1
.\build\benchmark_m1.exe
.\build\benchmark_m2.exe
.\build\benchmark_m3.exe --full
```

On Linux/macOS-style shells:

```bash
bash build.sh
./build/benchmark_m1
./build/benchmark_m2
./build/benchmark_m3 --full
```

## Key Files

```text
include/geometry/      Geometry primitives and ray-casting API
include/index/         BBox, quadtree, strip index, GeoJSON loader
include/generator/     Synthetic point and polygon generation
include/parallel/      Parallel classifier APIs
include/ipc/           Binary IPC serialization helpers
src/benchmark_m1.cpp   Milestone 1 benchmark
src/benchmark_m2.cpp   Milestone 2 benchmark
src/benchmark_m3.cpp   Milestone 3 benchmark
src/worker_main.cpp    Multi-process IPC worker executable
tests/                 Ray-casting unit tests
```

## Final Notes

The project satisfies the assignment requirements. Milestone 3 does not use MPI, but it now includes a true separate-process worker path on Windows in addition to the in-memory master/worker benchmark.
