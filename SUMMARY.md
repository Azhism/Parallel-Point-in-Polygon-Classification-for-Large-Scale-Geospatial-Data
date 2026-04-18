# Project Summary: Parallel Point-in-Polygon Classification

**Last Updated:** April 18, 2026  
**Canonical benchmark outputs:** `milestone_1.txt`, `milestone_2.txt`, `milestone_3.txt`  
**Author:** Azhab Babar (Azhism)

---

## Status

The project is complete through Milestone 3.

Milestone outputs are stored as raw executable output:

1. `milestone_1.txt` - output from `build\benchmark_m1.exe`
2. `milestone_2.txt` - output from `build\benchmark_m2.exe`
3. `milestone_3.txt` - output from `build\benchmark_m3.exe --full`

These files are the ground truth for benchmark numbers. Older timing tables in previous reports may differ because benchmarks were rerun on different days and under different machine load.

---

## Milestone 1: Sequential Baseline

Milestone 1 requirements are satisfied.

Implemented:

1. Ray-casting point-in-polygon classifier.
2. Boundary handling for points on edges and vertices.
3. Polygon holes.
4. MultiPolygon expansion while loading GeoJSON.
5. Bounding-box filtering.
6. Quadtree spatial index.
7. Strip index.
8. Uniform synthetic point generation.
9. Clustered synthetic point generation.
10. Real-world Pakistan administrative GeoJSON dataset.

Polygon creation:

1. Synthetic polygons are created with `PolygonLoader::create_grid`.
2. The default synthetic benchmark uses a 100 x 100 grid = 10,000 square polygons.
3. Real polygons are loaded from `pak_admin2.geojson`.

Canonical Milestone 1 benchmark highlights from `milestone_1.txt`:

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Best Indexed Speedup |
|---|---:|---:|---:|---:|
| 100K uniform | 2405.80 ms | 90.45 ms | 87.62 ms | 27.46x |
| 1M uniform | 16500.25 ms | 787.09 ms | 876.24 ms | 20.96x |
| 100K clustered | 1568.85 ms | 50.07 ms | 73.75 ms | 31.33x |
| 1M clustered | 16553.33 ms | 520.58 ms | 711.06 ms | 31.80x |

Real-world data:

1. Loaded 204 polygons from `pak_admin2.geojson`.
2. Loaded 745 centroid points from `pak_admincentroids.geojson`.
3. Real-world results validated against the brute-force baseline.

---

## Milestone 2: Parallel Point Classification

Milestone 2 requirements are satisfied.

Implemented:

1. OpenMP static scheduling.
2. OpenMP dynamic scheduling.
3. Tiled + Morton/Z-order spatial locality optimization.
4. True work-stealing with per-thread deques.
5. Hybrid static + dynamic scheduling as the optional bonus.
6. Thread-scaling analysis.
7. Real-world validation.

Machine note:

`milestone_2.txt` reports:

```text
Available threads: 4
```

Therefore, all current scaling claims should be interpreted for a 4-thread machine. There is no valid 8-thread result in the current canonical benchmark output.

Canonical Milestone 2 benchmark highlights from `milestone_2.txt`:

| Dataset | Sequential | Best Parallel Strategy | Best Parallel Time | Speedup |
|---|---:|---|---:|---:|
| 100K uniform | 91.75 ms | Hybrid | 36.79 ms | 2.49x |
| 100K clustered | 53.73 ms | Hybrid | 41.01 ms | 1.31x |
| 1M uniform | 845.84 ms | Hybrid | 390.97 ms | 2.16x |
| 1M clustered | 546.53 ms | Hybrid | 302.38 ms | 1.81x |

Thread scaling from `milestone_2.txt`:

1M uniform, Dynamic OMP:

| Threads | Time | Speedup | Efficiency |
|---:|---:|---:|---:|
| 1 | 849.11 ms | 1.00x | 100.0% |
| 2 | 508.47 ms | 1.67x | 83.5% |
| 4 | 396.82 ms | 2.14x | 53.5% |

1M clustered, Dynamic OMP:

| Threads | Time | Speedup | Efficiency |
|---:|---:|---:|---:|
| 1 | 549.23 ms | 1.00x | 100.0% |
| 2 | 343.36 ms | 1.60x | 80.0% |
| 4 | 335.71 ms | 1.64x | 40.9% |

Interpretation:

1. The parallel strategies are correct because every strategy validates against the sequential baseline.
2. Scaling is sub-linear.
3. Clustered 2-to-4-thread scaling is especially weak because quadtree lookup is memory-bound and extra threads increase random memory-access contention.
4. Tiled+Morton improves locality in some cases, but end-to-end time includes sorting cost, which can reduce the speedup.

---

## Milestone 3: Batch Processing and Multi-Process Execution

Milestone 3 requirements are satisfied.

Implemented:

1. Batch-based processing for large point sets.
2. Spatial point partitioning across workers.
3. Worker-local quadtree indices.
4. Polygon replication mode.
5. Spatial polygon sharding mode.
6. Compact result aggregation using counts/checksums.
7. Strong scaling.
8. Weak scaling.
9. 1M, 10M, and 100M point benchmarks.
10. Uniform and clustered distributions.
11. File-based IPC with a real `worker.exe` child process executable.

Distributed execution note:

The assignment says "MPI or multi-process." MPI is not used because the current native Windows/MSYS2 setup does not have MPI configured. The project now uses the allowed multi-process route for Milestone 3:

1. `benchmark_m3.exe` writes worker input files under `ipc/`.
2. It launches `build/worker.exe` child processes with Windows `CreateProcess`.
3. Each worker reads its point partition and polygon file, builds a local quadtree, classifies points, and writes a result file.
4. The master waits for all worker processes and aggregates their results.
5. The original in-memory master/worker path remains for 100M-scale throughput and scaling analysis.

The model still explores the distributed design:

1. Points are partitioned spatially across workers.
2. Polygons are either replicated or sharded.
3. Each worker owns an independent polygon/index context.
4. Results are aggregated centrally.
5. The same design can be mapped to MPI ranks in a future implementation.

Canonical Milestone 3 large-scale results from `milestone_3.txt`:

| Dataset | Distribution | Workers | Class Time | Class Throughput | Total Time |
|---:|---|---:|---:|---:|---:|
| 1M | uniform | 4 | 518.11 ms | 1,930,081 pts/sec | 587.91 ms |
| 1M | clustered | 4 | 358.84 ms | 2,786,737 pts/sec | 481.86 ms |
| 10M | uniform | 4 | 5298.39 ms | 1,887,365 pts/sec | 5713.48 ms |
| 10M | clustered | 4 | 3255.79 ms | 3,071,455 pts/sec | 4151.00 ms |
| 100M | uniform | 4 | 46222.22 ms | 2,163,462 pts/sec | 49846.42 ms |
| 100M | clustered | 4 | 34277.60 ms | 2,917,357 pts/sec | 43051.56 ms |

Replication vs sharding from `milestone_3.txt`:

| Distribution | Mode | Class Time | Class Throughput | Indexed Polygon Copies | Checksum |
|---|---|---:|---:|---:|---|
| uniform | replicated | 943.00 ms | 1,060,451 pts/sec | 40,000 | `e38e4d8a13e3441d` |
| uniform | sharded | 964.38 ms | 1,036,938 pts/sec | 10,600 | `e38e4d8a13e3441d` |
| clustered | replicated | 944.17 ms | 1,059,134 pts/sec | 40,000 | `c98c4f58897e6cf9` |
| clustered | sharded | 700.53 ms | 1,427,482 pts/sec | 10,600 | `c98c4f58897e6cf9` |

Interpretation:

1. Matching checksums prove replicated and sharded modes classify the same points the same way.
2. Sharding reduces indexed polygon copies from 40,000 to 10,600.
3. Sharding helps clustered data in this run but is slightly slower on uniform data, showing the trade-off is workload-dependent.

Multi-process IPC results from `milestone_3.txt`:

| Distribution | Mode | Write Time | Worker Time | Read Time | Total Time | Checksum |
|---|---|---:|---:|---:|---:|---|
| uniform | replicated | 494.25 ms | 1137.89 ms | 0.72 ms | 1633.86 ms | `e38e4d8a13e3441d` |
| uniform | sharded | 746.46 ms | 1076.23 ms | 1.80 ms | 1824.56 ms | `e38e4d8a13e3441d` |
| clustered | replicated | 492.64 ms | 1097.83 ms | 0.63 ms | 1591.21 ms | `c98c4f58897e6cf9` |
| clustered | sharded | 542.19 ms | 1067.45 ms | 0.60 ms | 1610.47 ms | `c98c4f58897e6cf9` |

The process checksums match the in-memory 1M checksums, so the separate worker executable preserves classification correctness. The extra time is the measured communication and process-management overhead.

Weak scaling from `milestone_3.txt`:

| Distribution | Workers | Points | Class Time |
|---|---:|---:|---:|
| uniform | 1 | 250K | 538.08 ms |
| uniform | 2 | 500K | 600.99 ms |
| uniform | 4 | 1M | 482.81 ms |
| clustered | 1 | 250K | 330.20 ms |
| clustered | 2 | 500K | 429.32 ms |
| clustered | 4 | 1M | 325.01 ms |

Weak-scaling interpretation:

1. Weak scaling is not ideal.
2. The 4-worker point recovers in the latest run, but the 2-worker point is slower than ideal.
3. This is real overhead, not a result to hide.
4. Likely causes are worker/thread scheduling overhead on a 4-thread machine, repeated replicated indexing overhead, memory bandwidth pressure during quadtree traversal, and batch partition/aggregation overhead.
5. The project documents this as an observed limitation.

---

## Instructor Milestone 0 Feedback Coverage

1. "How are polygons created?"
   - Synthetic polygons are created as grid cells using `PolygonLoader::create_grid`.
   - Real polygons are loaded from GeoJSON.

2. "Try to find a real world dataset"
   - Pakistan administrative polygon and centroid GeoJSON files are included and benchmarked.

3. "Explore different strategies/algorithm"
   - M1: brute force + bbox, quadtree, strip index.
   - M2: static OpenMP, dynamic OpenMP, tiled Morton, work stealing, hybrid.
   - M3: replicated polygons vs sharded polygons, batched master/worker execution, file-based multi-process IPC.

4. "Ensure all aspects including communication, computation, data sharing, etc are optimized"
   - Computation: spatial indexing, OpenMP classification, work stealing.
   - Data sharing: read-only shared geometry/index in M2; worker-local contexts in M3.
   - Communication/aggregation: compact checksum/count aggregation in M3.

5. "Explore distributed setting"
   - Explored through both an in-memory single-machine master/worker model and real child worker processes.
   - MPI is not used; the multi-process implementation is explicitly documented.

---

## Final Verdict

The project meets the milestone requirements. The main submission cautions are:

1. Treat `milestone_1.txt`, `milestone_2.txt`, and `milestone_3.txt` as canonical benchmark output.
2. Do not claim 8-thread scaling from the current machine; the actual output shows 4 available threads.
3. Be honest that Week 3 is multi-process on one machine, not MPI.
4. Be honest that Week 3 weak scaling is not perfectly flat.
