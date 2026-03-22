# Week 1 Implementation Summary (Detailed)

## 1. Purpose of Week 1

Week 1 establishes the sequential baseline for point-in-polygon classification before parallelization milestones.

Primary goals completed:

1. Build a correct geometric classifier (ray-casting with edge-case handling).
2. Build spatial pruning/indexing structures for faster candidate lookup.
3. Integrate real-world GeoJSON polygon + point datasets.
4. Benchmark baseline and indexed approaches on synthetic + real data.
5. Validate that optimized pipelines match baseline classification results.

## 2. Final Week 1 Pipeline

Implemented in `src/benchmark_m1.cpp`:

1. Stage 1: Brute force + bounding-box filter
2. Stage 2: Quadtree index + ray-casting verification
3. Stage 3: Strip Index + ray-casting verification
4. Stage 4: Real-world benchmark using GeoJSON

Spatial Hash path was removed from active code during Week 1 cleanup and replaced with Strip Index.

## 3. Code Components Implemented and Used

### 3.1 Geometry Layer

Key files:

1. `include/geometry/point.hpp`
2. `include/geometry/polygon.hpp`
3. `include/geometry/ray_casting.hpp`
4. `src/geometry/ray_casting.cpp`

What it does:

1. Defines `Point`, `BBox`, `Polygon`, and `MultiPolygon` structures.
2. Computes polygon bounding boxes.
3. Performs robust point-in-polygon classification via ray-casting.
4. Handles boundary conditions (edge/vertex cases) and holes.

### 3.2 Candidate Pruning / Indexing Layer

Key files:

1. `include/index/bbox_filter.hpp`, `src/index/bbox_filter.cpp`
2. `include/index/quadtree.hpp`, `src/index/quadtree.cpp`
3. `include/index/strip_index.hpp`, `src/index/strip_index.cpp`

What each index does:

1. BBox filter:
1. Linear scan over all polygon bboxes.
2. Used as baseline pruning in Stage 1.

2. Quadtree:
1. Builds a spatial hierarchy over polygon bbox extents.
2. Splits nodes recursively when leaf threshold is exceeded.
3. Queries candidate polygons by descending relevant quadrants.
4. Deduplicates candidates using set-based accumulation.

3. Strip Index:
1. Computes global `y_min`, `y_max` from polygons.
2. Chooses strip count (default `sqrt(num_polygons)` when unspecified).
3. Assigns each polygon id to all strips overlapped by its bbox.
4. For query point `p`, maps `p.y` to strip index and returns that strip�s candidate list.

### 3.3 Synthetic Data Generation Layer

Key files:

1. `include/generator/distribution.hpp`
2. `src/generator/uniform_distribution.cpp`
3. `src/generator/clustered_distribution.cpp`
4. `include/generator/polygon_loader.hpp`
5. `src/generator/polygon_loader.cpp`

What it does:

1. Generates uniform and clustered point sets.
2. Builds synthetic polygon grid (100 x 100 -> 10,000 polygons).

### 3.4 Real-World GeoJSON Layer

Key files:

1. `include/index/geojson_loader.hpp`
2. `src/index/geojson_loader.cpp`
3. `include/nlohmann/json.hpp`

What it does:

1. Loads Polygon and MultiPolygon geometries from GeoJSON.
2. Expands MultiPolygon into individual `Polygon` objects.
3. Extracts centroid points from multiple schemas:
1. `center_lon` / `center_lat`
2. `x_coord` / `y_coord`
3. Point geometry coordinates fallback
4. Applies robust numeric parsing for numeric/string coordinate values.

## 4. Core Classification Logic in Benchmark

Main execution flow in `src/benchmark_m1.cpp`:

1. Build synthetic polygon grid (`PolygonLoader::create_grid`).
2. For each distribution (`uniform`, `clustered`) and dataset size (`100K`, `1M`):
1. Generate points.
2. Stage 1:
1. `BBoxFilter::get_candidates(point, polygons)`
2. Ray-cast candidates via `classify_point_from_candidates`
3. Stage 2:
1. Build `QuadTreeIndex`
2. Query candidates per point, classify via ray-casting
4. Stage 3:
1. Build `StripIndex`
2. Query strip candidates per point, classify via ray-casting
5. Validate Stage 2 and Stage 3 outputs against Stage 1.
3. Run Stage 4 real-world benchmark:
1. Load polygons from `pak_admin2.geojson`
2. Load points from `pak_admincentroids.geojson`
3. Run Stage 1 and Stage 2 on real data
4. Validate Stage 2 against Stage 1

Helper function:

1. `classify_point_from_candidates(...)`:
1. Iterates candidate polygon ids.
2. Uses `RayCaster::point_in_polygon`.
3. Returns first containing polygon id or `UINT64_MAX` if no match.

## 5. Additional Large-Scale Mode (Code Path)

`benchmark_m1.cpp` includes `--large` / `--large-scale` mode:

1. Point counts switch to 10M and 100M.
2. Processing is batched to keep memory bounded.
3. Brute-force stage is skipped in this mode.
4. Compares Quadtree and Strip Index directly.
5. Tracks mismatches between indexed methods.

Note: Week 1 standard reporting remains focused on 100K/1M and real-data correctness.

## 6. Real-World Data Integrated in Week 1

Active files used by Stage 4:

1. `pak_admin2.geojson` (polygons)
2. `pak_admincentroids.geojson` (points)

Observed load counts in current run set:

1. 204 polygons (after MultiPolygon expansion)
2. 745 centroid points

## 7. Build, Test, and Run Status

Build command:

```bash
bash build.sh
```

Benchmark command:

```bash
bash -lc "./build/benchmark_m1"
```

Validation/test status completed in Week 1:

1. Build script compiles core objects and benchmark successfully.
2. Ray-casting unit tests run and pass (`./build/test_ray_casting`).
3. Benchmark completes through synthetic + real-world sections.

## 8. Latest Week 1 Benchmark Snapshot

Source: `output_strip_index_clean.txt`

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

### Real-World Benchmark

1. Stage 1 (Brute force + BBox): 21.36 ms
2. Stage 2 (Quadtree): 19.74 ms
3. Speedup: 1.08x

