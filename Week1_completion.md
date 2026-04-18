# Week 1 Completion Report

Note: This file is a historical completion report. For the canonical benchmark output used for submission, see `milestone_1.txt`.

## Status

Week 1 baseline is complete.

Implemented pipeline in `src/benchmark_m1.cpp`:

1. Stage 1: Brute force + bounding-box filtering
2. Stage 2: Quadtree index
3. Stage 3: Strip Index
4. Stage 4: Real-world GeoJSON benchmark



## Week 1 Scope

Week 1 focuses on a sequential, correctness-first baseline with spatial indexing.

## What Was Implemented

### 1. Geometry and Classification

1. Ray-casting point-in-polygon with boundary handling
2. Polygon support with holes
3. MultiPolygon support by expansion during GeoJSON loading
4. Bounding-box utilities and filtering

Key files:

1. `include/geometry/point.hpp`
2. `include/geometry/polygon.hpp`
3. `include/geometry/ray_casting.hpp`
4. `src/geometry/ray_casting.cpp`

### 2. Spatial Indexing

1. Quadtree candidate index
2. Strip Index based on horizontal strips

Key files:

1. `include/index/quadtree.hpp`
2. `src/index/quadtree.cpp`
3. `include/index/strip_index.hpp`
4. `src/index/strip_index.cpp`
5. `include/index/bbox_filter.hpp`
6. `src/index/bbox_filter.cpp`

### 3. Data Pipelines

1. Synthetic points (uniform and clustered)
2. Real-world polygons and centroids from GeoJSON

Synthetic generation files:

1. `include/generator/distribution.hpp`
2. `src/generator/uniform_distribution.cpp`
3. `src/generator/clustered_distribution.cpp`
4. `include/generator/polygon_loader.hpp`
5. `src/generator/polygon_loader.cpp`

Real-data loader files:

1. `include/index/geojson_loader.hpp`
2. `src/index/geojson_loader.cpp`

## Data Used in Week 1

Synthetic benchmark data:

1. Uniform point distribution
2. Clustered point distribution
3. Point counts: 100,000 and 1,000,000

Real-world benchmark data:

1. Polygons: `pak_admin2.geojson`
2. Points: `pak_admincentroids.geojson`

## Project Structure (Week 1 Relevant)

```text
.
|-- build.sh
|-- README.md
|-- Week1_completion.md
|-- pak_admin2.geojson
|-- pak_admincentroids.geojson
|-- include/
|   |-- geometry/
|   |-- generator/
|   |-- index/
|   `-- nlohmann/
|-- src/
|   |-- benchmark_m1.cpp
|   |-- geometry/
|   |-- generator/
|   `-- index/
`-- tests/
    `-- test_ray_casting.cpp
```

## Build and Run (Week 1)

```bash
bash build.sh
bash -lc "./build/benchmark_m1"
```

## Benchmark Flow

1. Build synthetic polygon grid (100x100)
2. Generate points for each distribution and size
3. Run Stage 1, Stage 2, Stage 3 and validate identical results
4. Load real polygons and centroids for Stage 4
5. Run real-data benchmark and validate results

## Real-World Data Integration

Benchmark Stage 4 uses:

1. `pak_admin2.geojson` for polygons
2. `pak_admincentroids.geojson` for points

Current loader behavior:

1. Polygon and MultiPolygon geometries are supported
2. Centroid coordinates can be read from:
1. `center_lon` and `center_lat`
2. `x_coord` and `y_coord`
3. Point geometry coordinates fallback

Observed dataset counts in current runs:

1. 204 polygons loaded from `pak_admin2.geojson` (after MultiPolygon expansion)
2. 745 centroid points loaded from `pak_admincentroids.geojson`

## Validation

Every optimized stage is checked against Stage 1 (brute-force baseline) for correctness.

## Benchmark Configuration (Week 1)

1. Synthetic polygon grid: 100 x 100 (10,000 polygons)
2. Point counts: 100,000 and 1,000,000
3. Distributions: uniform and clustered
4. Validation: optimized stages checked against Stage 1 results

## Latest Benchmark Snapshot

Source: Latest run (April 11, 2026)

### Uniform Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 1849.77 ms | 91.77 ms | 96.83 ms | 20.16x | 19.10x |
| 1M points | 19147.03 ms | 617.48 ms | 703.29 ms | 31.01x | 27.23x |

### Clustered Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 2123.59 ms | 63.67 ms | 187.06 ms | 33.35x | 11.35x |
| 1M points | 16063.64 ms | 289.97 ms | 514.42 ms | 55.40x | 31.23x |

### Real-World Data (745 Points)

1. Stage 1 (Brute Force + BBox): 13.06 ms
2. Stage 2 (Quadtree): 18.25 ms
3. Speedup: 0.72x

## Notes

1. Week 1 establishes correctness and sequential indexing baseline.
2. The 10M-100M scale target is addressed in later milestone benchmarking, typically via larger synthetic datasets.
3. This repository currently keeps the benchmark focused on Week 1 sizes and real-data correctness checks.

## Completion Summary

1. Correct geometric classification is in place.
2. Quadtree and Strip Index are integrated and benchmarked.
3. Real-world GeoJSON loading and classification are integrated.
4. Week 1 baseline is ready for parallelization work in the next milestone.
