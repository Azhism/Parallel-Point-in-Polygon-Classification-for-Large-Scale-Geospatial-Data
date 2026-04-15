# Week 3 Completion: Distributed MPI Execution

**Date:** April 14, 2026
**Milestone:** Milestone 3 (Scalable Batch Processing and Distributed Execution)

---

## Summary

Milestone 3 adds distributed execution via MPI on top of the existing sequential baseline (M1) and OpenMP parallel strategies (M2). The system now supports:

1. **Distributed point classification** across multiple MPI ranks
2. **Two polygon distribution modes**: replication (broadcast all) and spatial partitioning (send subsets)
3. **Batch processing** for 100M+ points without exceeding memory limits
4. **Hybrid MPI+OpenMP** execution: MPI for inter-process distribution, Dynamic OMP within each rank

---

## Architecture

```
                         Rank 0 (Coordinator)
                    ┌─────────────────────────┐
                    │  Generate/Load Points    │
                    │  Broadcast Polygons      │
                    │  Partition & Scatter Pts  │
                    └───────┬─────────────────┘
                            │ MPI_Scatterv
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
         ┌─────────┐  ┌─────────┐  ┌─────────┐
         │ Rank 0  │  │ Rank 1  │  │ Rank N  │
         │ Local   │  │ Local   │  │ Local   │
         │ QuadTree│  │ QuadTree│  │ QuadTree│
         │ + OMP   │  │ + OMP   │  │ + OMP   │
         └────┬────┘  └────┬────┘  └────┬────┘
              │             │             │
              └─────────────┼─────────────┘
                            │ MPI_Gatherv
                    ┌───────▼─────────────┐
                    │  Rank 0: Aggregate   │
                    │  & Reorder Results   │
                    └─────────────────────┘
```

---

## New Components

### Distributed Layer (`include/distributed/`, `src/distributed/`)

| File | Purpose |
|------|---------|
| `mpi_types.hpp` / `mpi_types.cpp` | MPI datatype registration for Point and ClassificationResult; polygon serialization/deserialization |
| `spatial_partitioner.hpp` / `.cpp` | Spatial domain decomposition (1D strips, 2D grid), point partitioning, polygon filtering with overlap buffer |
| `mpi_classifier.hpp` / `.cpp` | Distributed classification engine with replicate/partition modes and batched processing |

### Benchmark and Tests

| File | Purpose |
|------|---------|
| `src/benchmark_m3.cpp` | Full distributed benchmark: 1M/10M/100M x uniform/clustered x replicate/partition |
| `tests/test_mpi_classifier.cpp` | Correctness validation: distributed results vs sequential reference |

---

## Polygon Distribution Modes

### Replication Mode
- All polygons broadcast to every rank via `MPI_Bcast`
- Each rank builds identical QuadTreeIndex
- Simpler, no boundary issues
- Better for small polygon counts (10K polygons = ~8MB serialized)

### Spatial Partition Mode
- Rank 0 filters polygons per spatial region and sends subsets via `MPI_Send`
- Each rank builds QuadTreeIndex over its local polygon subset only
- 5% overlap buffer on region boundaries to handle boundary polygons
- Lower memory per rank, more complex

---

## Batch Processing

For 100M points (2.4GB raw):
- Processed in batches of 10M points
- Each batch: generate -> scatter -> classify -> gather
- Peak memory: ~60MB per rank per batch (at 4 ranks)
- Deterministic seeding per batch for reproducibility

---

## Build and Run

### Prerequisites
- C++17 compiler with OpenMP support (GCC recommended)
- Open MPI (`brew install open-mpi` on macOS)

### Build
```bash
bash build.sh
```

### Run Tests
```bash
mpirun -np 2 --oversubscribe ./build/test_mpi_classifier
mpirun -np 4 --oversubscribe ./build/test_mpi_classifier
```

### Run Benchmark
```bash
# 2 ranks x 4 threads (1M and 10M points)
OMP_NUM_THREADS=4 mpirun -np 2 --oversubscribe ./build/benchmark_m3

# 4 ranks x 2 threads
OMP_NUM_THREADS=2 mpirun -np 4 --oversubscribe ./build/benchmark_m3

# Include 100M points
OMP_NUM_THREADS=2 mpirun -np 4 --oversubscribe ./build/benchmark_m3 --100m
```

---

## Correctness Validation

All tests pass with 2 and 4 MPI ranks:
- Replication mode: 100K uniform [PASS]
- Spatial partition mode: 100K uniform [PASS]
- Replication mode: 100K clustered [PASS]
- Batched mode: 50K uniform, 2 batches [PASS]

---

## Metrics Collected

| Metric | Method |
|--------|--------|
| Total wall-clock time | `MPI_Wtime()` end-to-end |
| Scatter time | `MPI_Wtime()` around `MPI_Scatterv` |
| Compute time | `MPI_Wtime()` around local `classify()` |
| Gather time | `MPI_Wtime()` around `MPI_Gatherv` |
| Throughput | `total_points / total_time` |
| Communication overhead | `(scatter + gather) / total * 100%` |
| Load balance ratio | `max_rank_compute / avg_rank_compute` via `MPI_Reduce` |

---

## Known Limitations

1. All MPI ranks run on the same physical machine (shared memory bus) — true distributed scaling requires multiple nodes
2. Spatial strip partitioning causes load imbalance with clustered data (most points in one strip)
3. Polygon serialization overhead is non-trivial for large polygon counts
4. 100M point benchmarks are memory-intensive on single machine

---

## Trade-off Analysis

### Polygon Replication vs Spatial Partitioning
- **Replication** is faster for small polygon counts (10K) — zero filtering overhead, no boundary complexity
- **Partitioning** saves memory per rank and would be advantageous with millions of polygons

### Communication vs Computation
- At 1M points: communication overhead is 20-40% of total time
- At 10M+ points: computation dominates, communication drops below 10%
- Point scatter is the dominant communication cost (larger than result gather)
