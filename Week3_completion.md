# Week 3 Completion Report: Scalable Batch Processing and Distributed Execution

Note: For raw canonical benchmark output, see `milestone_3.txt`. This report explains the implementation and summarizes representative results.

## Status

Week 3 scalable batch processing and multi-process execution are complete.

Implemented a dedicated benchmark pipeline in `src/benchmark_m3.cpp` plus a separate IPC worker executable in `src/worker_main.cpp`.

## Week 3 Scope

Week 3 extends the shared-memory parallel classifier into a scalable master/worker-style execution model.

Completed goals:

1. Batch-based processing for large point sets.
2. Spatial worker partitioning across independent worker contexts.
3. Polygon replication mode.
4. Spatial polygon sharding mode.
5. Efficient aggregation without storing per-point result vectors.
6. Strong-scaling and weak-scaling benchmark tables.
7. Large-scale throughput runs at 1M, 10M, and 100M points.
8. File-based IPC with real child worker processes on Windows.

## Implementation

### Batch Processing

`benchmark_m3` streams generated points in fixed-size batches instead of materializing the full dataset in memory.

Default batch size:

```text
250,000 points
```

Each batch is routed into spatial x-stripes. A worker receives only the points for its stripe.

### Distributed Execution Model

The implementation uses independent worker contexts:

1. Each worker owns its own polygon vector.
2. Each worker owns its own quadtree index.
3. The master process partitions point batches by spatial stripe.
4. Workers classify their assigned bucket concurrently.
5. Results are aggregated as counts and checksums only.

This in-memory model is retained for the 100M benchmark and scaling analysis.

Important note: MPI is not used in the current implementation because MPI is not configured in the Windows/MSYS2 environment. The assignment allows "MPI or multi-process," and this project now includes a true multi-process path: `benchmark_m3.exe` writes binary IPC files, launches `worker.exe` processes with `CreateProcess`, waits for them, and aggregates result files. Each worker process maps directly to one MPI rank in a future MPI extension.

### Polygon Placement Modes

Two polygon strategies are benchmarked:

1. Replicated polygons: every worker indexes all polygons.
2. Spatially sharded polygons: each worker indexes only polygons whose bounding box intersects that worker's x-stripe.

The sharded mode reduces indexed polygon copies while preserving correctness for spatially routed points.

### Result Aggregation

Week 3 avoids storing `vector<ClassificationResult>` for huge runs. Each worker returns:

1. Total points processed.
2. Matched and unmatched point counts.
3. Candidate-check count.
4. A deterministic checksum over point IDs and polygon IDs.

Matching checksums between replicated and sharded modes validate that both produce the same classifications.

### Multi-Process IPC

The process-based path writes these files under `ipc/`:

1. `polygons.bin`
2. `worker_N_input.bin`
3. `worker_N_result.bin`

Each `worker.exe` process reads its assigned points, loads polygons, builds a local quadtree, classifies points, and writes compact aggregate results. The saved `milestone_3.txt` output reports write time, worker time, read time, total time, throughput, and checksum for replicated and sharded modes.

## Build and Run

Build:

```powershell
.\build.ps1
```

Run quick validation:

```powershell
.\build\benchmark_m3.exe --quick
```

Run default Week 3 benchmark:

```powershell
.\build\benchmark_m3.exe
```

Run required-scale 100M mode:

```powershell
.\build\benchmark_m3.exe --full
```

Useful options:

```text
--quick                 Run 100K and 1M datasets with smaller scaling tables
--sizes 100000000       Run a specific comma-separated size list
--workers 4             Set worker count
--batch-size 250000     Set streaming batch size
--skip-scaling          Skip strong/weak scaling tables
```

On Windows, ensure MSYS2 runtime DLLs are first in `PATH` before running:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
```

## Verified Results

System used in verification:

```text
OpenMP max threads: 4
Batch size: 250,000
Worker mode: 4 spatial workers
Polygon grid: 100 x 100 = 10,000 polygons
```

### Large-Scale Batched Throughput

| Points | Distribution | Workers | Polygon mode | Class time | Class throughput | Total time | Avg candidates |
|---:|---|---:|---|---:|---:|---:|---:|
| 1M | uniform | 4 | replicated | 518.11 ms | 1,930,081 pts/sec | 587.91 ms | 3.51 |
| 1M | clustered | 4 | replicated | 358.84 ms | 2,786,737 pts/sec | 481.86 ms | 3.48 |
| 10M | uniform | 4 | replicated | 5298.39 ms | 1,887,365 pts/sec | 5713.48 ms | 3.50 |
| 10M | clustered | 4 | replicated | 3255.79 ms | 3,071,455 pts/sec | 4151.00 ms | 3.48 |

### 100M Required-Scale Run

| Points | Distribution | Workers | Polygon mode | Class time | Class throughput | Total time | Avg candidates |
|---:|---|---:|---|---:|---:|---:|---:|
| 100M | uniform | 4 | replicated | 46222.22 ms | 2,163,462 pts/sec | 49846.42 ms | 3.50 |
| 100M | clustered | 4 | replicated | 34277.60 ms | 2,917,357 pts/sec | 43051.56 ms | 3.48 |

### Replication vs Sharding

Default 1M trade-off run:

| Distribution | Mode | Class time | Class throughput | Indexed polygon copies | Checksum |
|---|---|---:|---:|---:|---|
| uniform | replicated | 943.00 ms | 1,060,451 pts/sec | 40,000 | `e38e4d8a13e3441d` |
| uniform | sharded | 964.38 ms | 1,036,938 pts/sec | 10,600 | `e38e4d8a13e3441d` |
| clustered | replicated | 944.17 ms | 1,059,134 pts/sec | 40,000 | `c98c4f58897e6cf9` |
| clustered | sharded | 700.53 ms | 1,427,482 pts/sec | 10,600 | `c98c4f58897e6cf9` |

The matching checksums show that sharding preserved classification results while indexing far fewer polygon copies.

### Multi-Process IPC Results

Default process run: 1,000,000 points, 4 child workers.

| Distribution | Mode | Write time | Worker time | Read time | Total time | Checksum |
|---|---|---:|---:|---:|---:|---|
| uniform | replicated | 494.25 ms | 1137.89 ms | 0.72 ms | 1633.86 ms | `e38e4d8a13e3441d` |
| uniform | sharded | 746.46 ms | 1076.23 ms | 1.80 ms | 1824.56 ms | `e38e4d8a13e3441d` |
| clustered | replicated | 492.64 ms | 1097.83 ms | 0.63 ms | 1591.21 ms | `c98c4f58897e6cf9` |
| clustered | sharded | 542.19 ms | 1067.45 ms | 0.60 ms | 1610.47 ms | `c98c4f58897e6cf9` |

The process checksums match the in-memory 1M checksums, which verifies the IPC path. The process run is slower because it includes binary file writes, process startup/waiting, and result reads.

### Strong Scaling

Fixed dataset: 1,000,000 points.

| Distribution | Workers | Class time | Class throughput |
|---|---:|---:|---:|
| uniform | 1 | 2156.56 ms | 463,702 pts/sec |
| uniform | 2 | 1253.42 ms | 797,817 pts/sec |
| uniform | 4 | 1221.88 ms | 818,412 pts/sec |
| clustered | 1 | 1510.87 ms | 661,870 pts/sec |
| clustered | 2 | 1134.80 ms | 881,211 pts/sec |
| clustered | 4 | 927.70 ms | 1,077,931 pts/sec |

### Canonical Weak Scaling Observation

The full canonical run in `milestone_3.txt` shows weak scaling is not perfectly flat:

| Distribution | Workers | Total points | Class time |
|---|---:|---:|---:|
| uniform | 1 | 250K | 538.08 ms |
| uniform | 2 | 500K | 600.99 ms |
| uniform | 4 | 1M | 482.81 ms |
| clustered | 1 | 250K | 330.20 ms |
| clustered | 2 | 500K | 429.32 ms |
| clustered | 4 | 1M | 325.01 ms |

Interpretation: weak scaling is not ideal on this 4-thread machine. The 4-worker run recovers in the latest run, but the 2-worker point is slower than ideal, likely because workers compete for memory bandwidth during quadtree traversal and because batch partitioning, aggregation, and worker scheduling overhead are visible.

## Correctness Notes

1. Boundary points are now classified consistently as hits across M1, M2, and M3.
2. Replicated and sharded Week 3 modes produced identical checksums for both uniform and clustered data.

## Summary

Milestone 3 is complete.

The project now supports:

1. Sequential indexed classification.
2. Shared-memory parallel classification.
3. Scalable batched master/worker classification.
4. File-based multi-process IPC classification.
5. Strong and weak scaling analysis.
6. Large-scale 1M, 10M, and 100M point throughput benchmarks.
