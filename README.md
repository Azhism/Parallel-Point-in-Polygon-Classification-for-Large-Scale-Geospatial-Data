# Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data

A comprehensive exploration of spatial indexing and parallel-oriented design for rapid geospatial classification at scale.

## Project Overview

This project addresses a critical geospatial systems problem: classifying very large point sets (GPS-like events) against polygon regions efficiently and correctly.

Real-world applications include:

1. City boundary and administrative zone classification
2. Postal zone assignment
3. Service area determination (delivery, emergency response)
4. Geofence-based filtering

## The Challenge

Classifying 1 million points against 10,000 polygons with naive ray-casting is expensive:

1. Massive geometric work
2. Linear polygon scanning per point
3. Heavy sensitivity to spatial skew and polygon complexity

Spatial indexing is required to keep candidate checks small and maintain high throughput.

## Results (Milestone 1)

Benchmark: 100 x 100 polygon grid (10,000 polygons)

### Uniform Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 2560.86 ms | 88.08 ms | 80.65 ms | 29.08x | 31.75x |
| 1M points | 20064.42 ms | 773.39 ms | 672.40 ms | 25.94x | 29.84x |

### Clustered Distribution

| Dataset | Brute Force + BBox | Quadtree | Strip Index | Quadtree Speedup | Strip Speedup |
|---|---:|---:|---:|---:|---:|
| 100K points | 1948.59 ms | 45.20 ms | 51.60 ms | 43.11x | 37.77x |
| 1M points | 20937.39 ms | 666.83 ms | 765.08 ms | 31.40x | 27.37x |

### Real-World Data Benchmark

Inputs:

1. Polygons from `pak_admin2.geojson`
2. Points from `pak_admincentroids.geojson`

Loaded counts:

1. 204 polygons (after MultiPolygon expansion)
2. 745 centroid points

Runtime (real data):

1. Stage 1 (Brute Force + BBox): 21.36 ms
2. Stage 2 (Quadtree): 19.74 ms
3. Speedup: 1.08x

## Key Insights

1. Spatial indexing delivers strong gains on synthetic scale tests (10K polygons, up to 1M points).
2. Quadtree and Strip Index both provide major acceleration over brute-force baseline (roughly 27x to 43x in this run).
3. Real-data stage validates correctness and GeoJSON integration; speedup is smaller due to low query volume (745 points).
4. All stages are validated against baseline results.

## Project Structure

```text
.
|-- README.md
|-- Week1_completion.md
|-- build.sh
|-- CMakeLists.txt
|-- pak_admin2.geojson
|-- pak_admincentroids.geojson
|-- include/
|   |-- geometry/
|   |   |-- point.hpp
|   |   |-- polygon.hpp
|   |   `-- ray_casting.hpp
|   |-- generator/
|   |   |-- distribution.hpp
|   |   `-- polygon_loader.hpp
|   |-- index/
|   |   |-- bbox_filter.hpp
|   |   |-- geojson_loader.hpp
|   |   |-- quadtree.hpp
|   |   `-- strip_index.hpp
|   `-- nlohmann/
|       `-- json.hpp
|-- src/
|   |-- benchmark_m1.cpp
|   |-- geometry/
|   |   |-- point.cpp
|   |   |-- polygon.cpp
|   |   `-- ray_casting.cpp
|   |-- generator/
|   |   |-- uniform_distribution.cpp
|   |   |-- clustered_distribution.cpp
|   |   `-- polygon_loader.cpp
|   `-- index/
|       |-- bbox_filter.cpp
|       |-- geojson_loader.cpp
|       |-- quadtree.cpp
|       `-- strip_index.cpp
|-- tests/
|   `-- test_ray_casting.cpp
`-- docs/
```

## Architecture

### 1. Geometry Layer

1. Point, Polygon, BBox primitives
2. Ray-casting classifier with boundary handling
3. Support for holes and MultiPolygon-expanded components

### 2. Indexing Layer

1. BBox linear filter baseline
2. Quadtree spatial index
3. Strip Index (horizontal strip partitioning)

### 3. Data Layer

1. Synthetic generators (uniform and clustered)
2. GeoJSON polygon and centroid loaders

## Benchmarking Pipeline

Stage flow in `src/benchmark_m1.cpp`:

1. Stage 1: Brute force + bbox filter
2. Stage 2: Quadtree candidate query + ray-casting
3. Stage 3: Strip Index candidate query + ray-casting
4. Stage 4: Real-world polygons and centroid points

Validation compares optimized stages to Stage 1 to ensure correctness.

## Build and Run

Prerequisites:

1. C++17 compiler
2. Bash-compatible shell for `build.sh`

Quick start:

```bash
bash build.sh
bash -lc "./build/benchmark_m1"
```

## Design Decisions

1. Ray-casting selected for robust point-in-polygon correctness.
2. Quadtree and Strip Index included to compare two different pruning strategies.
3. Real-world GeoJSON stage included for data realism and integration validation.
4. Milestone 1 remains sequential by design to establish baseline before parallel work.

## Experimental Methodology

Configuration:

1. Polygon grid: 100 x 100 (10,000 polygons)
2. Point datasets: 100K and 1M
3. Distributions: uniform and clustered
4. Metrics: throughput, latency, speedup, build cost, correctness

## Troubleshooting

1. If build fails, verify compiler/toolchain availability and C++17 support.
2. If results look unexpectedly slow, confirm optimization flags are active in `build.sh`.
3. If real-data stage does not run, verify `pak_admin2.geojson` and `pak_admincentroids.geojson` are present in project root.

## Documentation

Detailed Week 1 implementation notes are available in:

1. `Week1_completion.md`
