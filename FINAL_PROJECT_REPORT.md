# Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data
### PDC Final Project Report — Azhab Babar

---

## Table of Contents

1. [The Problem](#1-the-problem)
2. [Why It Is Hard](#2-why-it-is-hard)
3. [Our Solution — Big Picture](#3-our-solution--big-picture)
4. [How We Divided the Work — Three Milestones](#4-how-we-divided-the-work--three-milestones)
5. [How We Generated the Data](#5-how-we-generated-the-data)
6. [Milestone 1 — Sequential Baseline with Spatial Indexing](#6-milestone-1--sequential-baseline-with-spatial-indexing)
7. [Milestone 2 — Parallel Classification and Load Balancing](#7-milestone-2--parallel-classification-and-load-balancing)
8. [Milestone 3 — Scalable Batch and Distributed Execution](#8-milestone-3--scalable-batch-and-distributed-execution)
9. [Benchmark Results](#9-benchmark-results)
10. [Key Findings](#10-key-findings)
11. [Problems We Faced](#11-problems-we-faced)

---

## 1. The Problem

### What Is Point-in-Polygon (PiP)?

Imagine a map of Pakistan divided into hundreds of administrative tehsils or districts — each one is a **polygon** on the map, a shape with many boundary edges. Now imagine you have **millions of GPS coordinates** — locations of delivery trucks, phone signals, emergency calls, or ride-share cars. The question is:

> **For each GPS point, which district does it fall inside?**

This is the **Point-in-Polygon (PiP) classification** problem. It sounds simple for one point but becomes a massive computational challenge at scale.

### Real-World Examples

| Application | Points | Polygons |
|---|---|---|
| Ride-sharing (Uber / Careem) | Millions of trip GPS pings | City zones for surge pricing |
| Telecom | Cell tower coverage queries | Network signal zones |
| Government | Census classification | Administrative boundaries (districts, tehsils) |
| Logistics | Delivery fleet tracking | Delivery zones |
| Emergency services | Incident dispatch | Jurisdiction boundaries |

### The Scale Challenge

| Scale | Points | Brute-Force Time (single core) |
|---|---|---|
| Small | 1,000 | < 1 second — fine |
| Medium | 1,000,000 (1M) | ~14 seconds — too slow for real-time |
| Large | 100,000,000 (100M) | ~23 minutes — completely unacceptable |

Our project brings the 1M case from 14 seconds down to **under 500ms** and the 10M case down to **under 4 seconds** using parallel computing and spatial indexing.

---

## 2. Why It Is Hard

### Problem 1 — Polygon Complexity

A polygon is not a simple rectangle. Pakistan's district boundaries have **hundreds of vertices**, concave shapes, holes (enclaves), and multi-part geometries (a region may physically consist of disconnected pieces). Testing a single point against one complex polygon requires real computation.

```
Simple Rectangle:  4 vertices  →  trivial test
Pakistan Tehsil:   200-800 vertices, concave edges, possible holes
```

On top of that, there are tricky **edge cases** that must be handled correctly:
- What if the point lands exactly on the polygon boundary?
- What if the point lands exactly on a vertex where two edges meet?
- What if a polygon edge is perfectly horizontal?
- What if the polygon has a hole (like a lake inside a national park)?
- What if a region is made of multiple disconnected pieces (MultiPolygon)?

### Problem 2 — Scale

With 10,000 polygons and 1 million points, a naive approach does:

```
1,000,000 points × 10,000 polygons = 10,000,000,000 tests
```

At ~68,000 tests/second (our measured brute-force rate) → **41 hours**. Completely impractical.

### Problem 3 — Spatial Skew (Non-Uniform Distribution)

Real GPS data is not spread evenly. Cities generate thousands of points per square kilometer; rural areas generate almost none. This **spatial skew** means:

- Some regions need 1,000x more work than others
- A naive parallel split (give each thread an equal number of points) still leaves some threads idle while others are overloaded
- This is called **load imbalance** — a core PDC concept we measure and address in Milestones 2 and 3

---

## 3. Our Solution — Big Picture

### General Intuition

Think of it like finding an address in a city. The brute-force approach is walking every single street until you find the right one. The smart approach is: first check which neighborhood (bounding box), then which block (quadtree), then check only the 3-4 houses on that specific spot (ray-casting). You skip the other 9,996 houses entirely.

That is exactly what we do:

```
GPS Point
    │
    ▼
┌─────────────────────────────────┐
│  Step 1: Spatial Index Lookup   │  ← "Which polygons COULD contain this point?"
│  (Quadtree or Strip Index)      │    Returns ~3-4 candidates instead of 10,000
└─────────────────┬───────────────┘
                  │ 3-4 candidates
                  ▼
┌─────────────────────────────────┐
│  Step 2: Ray-Casting Test       │  ← "Does this point actually lie inside?"
│  (Exact geometric test)         │    Runs on each candidate only
└─────────────────┬───────────────┘
                  │
                  ▼
           "Point belongs to District X"
```

### The Three Layers of Optimization

```
Layer 1 (Algorithm):     Spatial indexing → 20-30x speedup on one core
Layer 2 (Parallelism):   Multiple cores (OpenMP) → 2x additional speedup
Layer 3 (Distribution):  Multiple processes + batching → handles 10M+ points
```

---

## 4. How We Divided the Work — Three Milestones

```
Milestone 1 → Get the CORRECT answer fast on ONE core
              (spatial indexing, algorithm design, real-world data)

Milestone 2 → Get the answer fast using MULTIPLE CORES
              (shared-memory parallelism, load balancing strategies)

Milestone 3 → Scale to MASSIVE datasets using MULTIPLE PROCESSES
              (distributed execution, batched processing, IPC)
```

Each milestone builds on the previous one. Milestone 1 gives us a correct, fast sequential baseline. Milestone 2 parallelizes it. Milestone 3 scales it to millions/billions of points.

---

## 5. How We Generated the Data

### Synthetic Polygon Grid (Primary Test Dataset)

For benchmarking, we use a **100×100 grid** of 10,000 perfectly tessellating square polygons covering the coordinate space [0, 100] × [0, 100]. This gives a controlled, reproducible dataset.

```
(0,100) ─────────────────────── (100,100)
   │  [0,0] [0,1] [0,2] ... [0,99] │
   │  [1,0] [1,1] [1,2] ... [1,99] │   Each cell = 1×1 unit polygon
   │  ...                           │   Total: 10,000 polygons
   │  [99,0]          ...  [99,99]  │
(0,0)  ─────────────────────── (100,0)
```

**Why this design?** 10,000 small, non-overlapping polygons stress-tests the spatial index far harder than a handful of large administrative areas. Every point must be classified into exactly one cell.

### Synthetic Point Distributions

We generate points in two modes to simulate different real-world scenarios:

**Uniform Distribution** — points spread randomly across the entire space:
```
Pick random X in [0, 100]
Pick random Y in [0, 100]
→ Every polygon gets roughly equal traffic
→ Simulates: sensors spread across a region
```

**Clustered Distribution** — points grouped around 5 random "city" centers:
```
1. Pick 5 random cluster centers at program startup
2. For each point:
   - Pick a random cluster center
   - Add Gaussian noise: X += normal(mean=0, std_dev=1.5)
   - Add Gaussian noise: Y += normal(mean=0, std_dev=1.5)
   - Clamp to [0, 100] boundaries
→ Dense clusters around 5 hotspots, sparse everywhere else
→ Simulates: urban GPS density — cities generate 100x more data than villages
```

**Example of what clustered distribution looks like:**
```
          Cluster A
          ●●●●●●
         ●●●●●●●●
          ●●●●●●
                           Cluster B
                           ●●●●●
                          ●●●●●●●
                           ●●●●●
  ·  ·    ·   ·   ·   ·   ·   ·   ←  sparse points elsewhere
```

### Real-World Data — Pakistan Administrative Boundaries

For realistic testing we use actual GeoJSON boundary files:

| Milestone | File | Admin Level | Polygons Loaded |
|---|---|---|---|
| M1 | `pak_admin3.geojson` | Level 3 — Tehsils | **608 polygons** |
| M2 | `pak_admin2.geojson` | Level 2 — Districts | **204 polygons** |

- **Level 3 (Tehsils)** — the most granular administrative subdivision, smaller areas, more polygons per region
- **Level 2 (Districts)** — larger administrative areas

For M1, synthetic GPS points are generated within Pakistan's bounding box (longitude 60.87°–77.84°E, latitude 23.63°–37.10°N). For M2, we use actual district centroid coordinates from `pak_admincentroids.geojson` (745 real points).

The GeoJSON loader handles both `Polygon` and `MultiPolygon` geometry types. When a feature is a `MultiPolygon`, each component is flattened into an individual `Polygon` entry in the list.

---

## 6. Milestone 1 — Sequential Baseline with Spatial Indexing

### Goal

Build a **correct, single-threaded** solution that handles all geometric edge cases and is fast enough to serve as the foundation for parallelization.

### File Structure

| Component | File | Purpose |
|---|---|---|
| Ray-Casting Algorithm | `src/geometry/ray_casting.cpp` | Core PiP test |
| Bounding Box Filter | `src/index/bbox_filter.cpp` | Quick pre-filter |
| Quadtree Index | `src/index/quadtree.cpp` | Smart spatial lookup |
| Strip Index | `src/index/strip_index.cpp` | Alternative fast lookup |
| GeoJSON Loader | `src/index/geojson_loader.cpp` | Load real Pakistan data |
| Point/Polygon structs | `include/geometry/point.hpp`, `polygon.hpp` | Data representation |
| Benchmark | `src/benchmark_m1.cpp` | Measure all stages |

---

### How We Represent the Data

**A Point** (defined in `point.hpp`):
```cpp
struct Point {
    double x;       // longitude (or synthetic X coordinate)
    double y;       // latitude  (or synthetic Y coordinate)
    uint64_t id;    // unique identifier
};
```

**A Polygon** (defined in `polygon.hpp`):
```cpp
struct Polygon {
    uint64_t id;
    std::vector<Point> exterior;              // outer ring — list of vertices (CCW order)
    std::vector<std::vector<Point>> holes;    // inner rings (holes in CW order)
    BBox bbox;                                // precomputed bounding box
};
```

The **bounding box** (`BBox`) stores four values:
```
min_x, min_y  ←  bottom-left corner
max_x, max_y  ←  top-right corner
```
For a Pakistan tehsil, these are the westernmost, southernmost, easternmost, and northernmost coordinates of that tehsil's boundary.

**A MultiPolygon** (defined in `polygon.hpp`):
```cpp
struct MultiPolygon {
    uint64_t id;
    std::vector<Polygon> components;   // each disconnected piece is a full Polygon
    BBox bbox;                         // merged bbox covering all components
};
```

---

### Algorithm 1 — Ray-Casting (The Core PiP Test)

**Intuition:**
Stand at your GPS point. Fire an invisible ray horizontally to the right (towards +x infinity). Count how many times it crosses the polygon's boundary edges.

- **Odd crossings** → you are **INSIDE** the polygon
- **Even crossings** (0, 2, 4…) → you are **OUTSIDE** the polygon

This is the **Even-Odd Rule**, a standard algorithm in computational geometry.

**Example:**
```
  Polygon boundary:
  ┌─────────────────┐
  │                 │
  │   * ──────────────────→   ray crosses 1 time = ODD = INSIDE
  │                 │
  └─────────────────┘

  * ──────────────────────→   ray crosses 0 times = EVEN = OUTSIDE
```

**Implementation** (`ray_casting.cpp`, function `ray_cast_ring`):

```
For each edge (a → b) of the polygon ring:

  Step 1: Skip horizontal edges
          if abs(a.y - b.y) < EPSILON → continue
          (horizontal edges are parallel to our ray, no clean intersection)

  Step 2: Check if point is exactly ON this edge
          Use cross product to test collinearity + bounding box check
          if on edge → return ON_BOUNDARY immediately

  Step 3: Check if the edge's Y-range covers the point's Y value
          y_min = min(a.y, b.y),  y_max = max(a.y, b.y)
          if point.y outside [y_min, y_max] → skip this edge

  Step 4: Compute where the ray hits this edge's X position
          t = (point.y - a.y) / (b.y - a.y)          ← parameter along edge
          x_intersect = a.x + t * (b.x - a.x)        ← x at that height

  Step 5: Only count if intersection is to the RIGHT of the point
          if x_intersect > point.x → potential crossing

  Step 6: Vertex rule — avoid double-counting
          Count only if: (a.y > point.y) != (b.y > point.y)
          (one endpoint strictly above ray, the other at-or-below)

Final: if total crossings is ODD → INSIDE, else OUTSIDE
```

**Handling edge cases:**

| Case | How We Handle It |
|---|---|
| Point exactly on boundary | `point_on_segment()` uses cross product test → returns `ON_BOUNDARY` |
| Horizontal polygon edge | Skipped entirely (line 38: `if abs(a.y - b.y) < EPSILON`) |
| Point on a vertex | The two edges meeting at that vertex: only one gets counted by the `(a.y > p.y) != (b.y > p.y)` rule |
| Polygon with holes | After confirming inside exterior, check each hole. If inside a hole → `OUTSIDE` |
| MultiPolygon | Iterate each component; first `INSIDE` or `ON_BOUNDARY` result wins |

**Handling holes (donut shapes):**
```
1. Test against the exterior ring → if crossings is ODD: candidate INSIDE
2. For each hole ring:
   - Run ray-cast against the hole
   - If ODD crossings in hole → point is inside the hole → return OUTSIDE
3. If inside exterior and NOT inside any hole → return INSIDE
```

**Handling MultiPolygons:**
```cpp
// point_in_multipolygon() in ray_casting.cpp
for (const auto& poly : multi.components) {
    Classification result = point_in_polygon(p, poly);
    if (result == INSIDE || result == ON_BOUNDARY)
        return result;   // found in one component → done
}
return OUTSIDE;
```

---

### Algorithm 2 — Bounding Box Pre-Filter

Before the expensive ray-casting test, we first check whether the point is even within the rectangular bounding box of the polygon. This is 4 comparisons and eliminates most polygons instantly.

```
Polygon BBox:  min_x=60, max_x=80, min_y=20, max_y=45

Point at (50, 30):  50 < 60 → outside bbox → skip ray-casting entirely
Point at (70, 30):  60 ≤ 70 ≤ 80 AND 20 ≤ 30 ≤ 45 → inside bbox → run ray-casting
```

Even with just bounding boxes (no tree), this cuts out most of the 10,000 polygons for any given point. The spatial index (quadtree/strip) then reduces candidates further.

---

### Algorithm 3 — Quadtree Index

**The problem with bounding boxes alone:**
With 10,000 polygons, we still check all 10,000 bounding boxes per point. The quadtree solves this by recursively dividing the map space so that for any given point, we only touch a tiny subset of polygons.

**Building the tree** (`quadtree.cpp`):

```
1. Compute the bounding box that covers ALL polygons (the root cell)
   Add 0.1% padding so boundary polygons are not clipped

2. For each polygon, insert it into the tree:
   - Start at the root node
   - If the node has fewer than MAX_POLYGONS_PER_LEAF (= 6) polygons → store here
   - If the node is full → SPLIT it into 4 children:

     Find midpoint: mid_x = (min_x + max_x) / 2
                    mid_y = (min_y + max_y) / 2

     Create 4 children:
       NW child: (min_x, mid_y) → (mid_x, max_y)
       NE child: (mid_x, mid_y) → (max_x, max_y)
       SW child: (min_x, min_y) → (mid_x, mid_y)
       SE child: (mid_x, min_y) → (max_x, mid_y)

     Redistribute polygons: each goes into whichever child its bbox overlaps
     A polygon can appear in multiple children if it straddles a boundary

3. Stop splitting at MAX_DEPTH = 12 levels
```

**Visual:**
```
Level 0:  ┌────────────────────┐
          │  All 10,000 poly   │   (too many → split)
          └────────────────────┘
Level 1:  ┌─────┬─────┐
          │ NW  │ NE  │   ~2,500 polygons each
          ├─────┼─────┤
          │ SW  │ SE  │   (still too many → split each)
          └─────┴─────┘
Level 2:  16 sub-cells, ~625 each
...
Level 7:  ~16,000 tiny cells, ~3–6 polygons each ← query terminates here
```

**Querying a point:**
```
Start at root
  Is point in left half (x < mid_x) or right (x >= mid_x)?
  Is point in bottom half (y < mid_y) or top (y >= mid_y)?
  → Descend into exactly ONE child quadrant
  Repeat until leaf node
  Return the 3–6 polygon IDs stored in that leaf
```

The `strict < / >= ` rule (not `<=`) ensures a point on a boundary goes into exactly ONE quadrant — no duplicates.

**Three bugs fixed during development:**
1. Used `unordered_set` instead of `set` for O(1) average candidate deduplication (vs O(log n))
2. Fixed boundary routing: `p.x < mid_x` (strict) vs `p.x >= mid_x` — originally `<=` caused boundary points to visit multiple children
3. Fixed split trigger: `size < MAX_POLYGONS_PER_LEAF` (strict) not `<=` — originally split happened one slot too late

---

### Algorithm 4 — Strip Index

A simpler alternative to the quadtree: divide the Y-axis into horizontal strips of equal height. Each strip stores IDs of all polygons whose bbox overlaps that strip.

```
Y=100  ┌──────────────────────────┐  Strip 9
Y= 90  ├──────────────────────────┤  Strip 8
       ...
Y= 10  ├──────────────────────────┤  Strip 1
Y=  0  └──────────────────────────┘  Strip 0
```

Query: `strip = floor((point.y - y_min) / strip_height)` → O(1) lookup.

**Comparison:**
- Strip Index builds in **0.44 ms** vs Quadtree's **5.66–8.29 ms** (much simpler)
- Query performance is similar for uniform grids (~3-4 candidates each)
- For highly non-uniform data, quadtree adapts better (strips can't subdivide dense areas)

---

### Milestone 1 Results (from `bench_m1_live.txt`)

**Synthetic data — 10,000-polygon grid:**

| Dataset | Brute Force | Quadtree | Quadtree Speedup | Strip Index | Strip Speedup |
|---|---|---|---|---|---|
| 100K uniform | 67,084 pts/sec | 566,715 pts/sec | **8.45x** | 444,966 pts/sec | 6.63x |
| 1M uniform | 68,624 pts/sec | 1,370,246 pts/sec | **19.97x** | 1,327,123 pts/sec | 19.34x |
| 100K clustered | 70,352 pts/sec | 2,072,608 pts/sec | **29.46x** | 1,571,598 pts/sec | 22.34x |
| 1M clustered | 70,545 pts/sec | 2,072,920 pts/sec | **29.38x** | 1,556,616 pts/sec | 22.07x |

**Real-world data — pak_admin3.geojson (Level 3 Tehsils, 608 polygons, 100K synthetic GPS points):**

| Distribution | Brute Force | Quadtree | Speedup | Strip Index | Speedup |
|---|---|---|---|---|---|
| Uniform | 289,443 pts/sec | 356,915 pts/sec | 1.23x | 366,600 pts/sec | **1.27x** |
| Clustered | 574,964 pts/sec | 758,948 pts/sec | 1.32x | 814,705 pts/sec | **1.42x** |

**All results validated: every query result was verified to match between all three methods.** ✓

**Why is the real-world speedup modest (1.2-1.4x vs 20-30x for synthetic)?**
The real Pakistan ADM3 dataset has 608 polygons, not 10,000. With fewer polygons, the brute-force bounding box scan is already fast at this size, and administrative polygons have large overlapping bboxes that the index cannot resolve as cleanly as the compact grid cells. Indexing pays off proportionally more as polygon count grows.

**Why does clustered data get higher speedup than uniform?**
Clustered points hit the same quadtree cells repeatedly. Those cells remain warm in the CPU's L1/L2 cache, making repeated queries faster. Uniform points scatter across different cells on each query, causing more cache misses.

---

## 7. Milestone 2 — Parallel Classification and Load Balancing

### Goal

Take the working sequential solution and split the work across **4 CPU cores** using OpenMP and custom threading, comparing multiple strategies to understand their tradeoffs.

### Strategies Implemented

| Stage | Strategy | File | Key Idea |
|---|---|---|---|
| 1 | Sequential baseline | `parallel_classifier.cpp` | Reference — no parallelism |
| 2 | Static OpenMP | same | Equal chunks pre-assigned at start |
| 3 | Dynamic OpenMP | same | Small chunks grabbed on demand |
| 4 | Tiled + Morton Z-sort | same | Sort spatially first, then parallel classify |
| 5 | Work-Stealing | `work_stealing_classifier.cpp` | Threads steal from idle neighbors |
| 6 | Hybrid | `parallel_classifier.cpp` | 80% static + 20% dynamic overflow |

---

### Strategy 2 — Static OpenMP

**Idea:** Divide the point array into equal chunks at program start. Each thread gets its fixed chunk.

```
Points: [P0, P1, P2, P3, P4, P5, P6, P7]
4 threads:
  Thread 0: [P0, P1]   Thread 1: [P2, P3]
  Thread 2: [P4, P5]   Thread 3: [P6, P7]
  All 4 run simultaneously
```

```cpp
#pragma omp parallel for schedule(static, chunk_size) if(n > 50000)
for (int i = 0; i < n; ++i) {
    results[i] = classify_one(points[i], polygons, index);
}
```

**Chunk size matters:** The default (n/threads = 250K per thread for 1M points) causes all 4 threads to simultaneously access completely different parts of the quadtree, thrashing the CPU cache. We use `sqrt(n)/4 ≈ 250` points per chunk so threads cycle through small pieces and keep quadtree data warm.

**Limitation:** If some chunks have denser clusters (more expensive to classify), those threads finish late while others sit idle.

---

### Strategy 3 — Dynamic OpenMP

**Idea:** Keep a shared task queue. Threads grab the next available chunk whenever they finish.

```cpp
#pragma omp parallel for schedule(dynamic, chunk_size) if(n > 50000)
for (int i = 0; i < n; ++i) {
    results[i] = classify_one(points[i], polygons, index);
}
```

Only one word changes (`dynamic` vs `static`), but OpenMP internally uses an atomic counter so threads pull work on demand. A thread that processes a cheap (sparse) chunk immediately grabs another, helping to balance load.

**Better for clustered data** where some point batches take much longer than others.

---

### Strategy 4 — Tiled + Morton Z-Sort (Cache Locality)

**The cache problem:** Points arrive in arbitrary order. Point 0 might be in Pakistan's north, point 1 in the south. Every query is likely a cache miss because the quadtree node for north is evicted before we need it again.

**Solution — Morton (Z-order) Sort:**
Before classification, sort all points by their Z-order curve value. Points that are geographically close end up adjacent in the sorted array, so consecutive queries hit the same quadtree nodes — which are still warm in cache.

**What is Z-order?**
```
Normal grid:   1  2  5  6
               3  4  7  8    ← Z-order visits spatially nearby
               9 10 13 14       cells together
              11 12 15 16
```

**How Morton code is computed:**
```
Normalize X and Y to 16-bit integers (0 to 65535)
Interleave the bits of X and Y:
  X = x15 x14 ... x1 x0
  Y = y15 y14 ... y1 y0
  Morton = y15 x15 y14 x14 ... y1 x1 y0 x0

Two nearby points produce nearby Morton codes → they sort together
```

**Sorting with radix sort (4 passes, O(n) total) instead of comparison-based O(n log n):**
```
Pass 1: sort by bits  0–15
Pass 2: sort by bits 16–31
Pass 3: sort by bits 32–47
Pass 4: sort by bits 48–63
```

**Honesty in benchmarking:** The benchmark reports both:
- `[classify]` time — parallel classification after sorting (looks great)
- `[end-to-end]` time — sort cost + classification (the real wall-clock cost)

Morton sort adds ~157ms of sorting overhead for 1M points, which eats into the classify-only gain.

---

### Strategy 5 — Work-Stealing

**Concept:** Each thread has its own **double-ended queue (deque)** of tasks. Threads work from the front. When a thread's deque is empty, it **steals from the back** of another thread's deque.

```
Initial:
  Thread 0 deque: [T0, T4, T8, T12]  ← works front→back
  Thread 1 deque: [T1, T5, T9, T13]
  Thread 2 deque: [T2, T6, T10, T14]
  Thread 3 deque: [T3, T7, T11, T15]

Thread 0 finishes early (had cheap sparse points):
  Thread 0's deque: []  (empty)
  Thread 0 steals T13 from the BACK of Thread 1
  (Thread 1 is working T1 from the front — stealing from the back avoids contention)
```

**Implementation** (`work_stealing_classifier.cpp`):
```
1. Create num_threads deques, each protected by its own mutex
2. Divide all points into M × num_threads chunks (M=4 for fine granularity)
3. Distribute chunks round-robin: chunk 0 → deque 0, chunk 1 → deque 1, etc.
4. Each thread loops:
   a. Pop from front of own deque → classify those points
   b. If empty: scan other threads' deques, lock, pop from BACK → classify
   c. If all deques empty → exit
```

**Why steal from the back?** The owner works from the front. The thief steals from the back. They are at opposite ends of the deque, minimizing lock contention.

---

### Strategy 6 — Hybrid (Static + Dynamic)

**Problem with static alone:** Wastes time when data is skewed (some threads finish far ahead of others).
**Problem with dynamic alone:** Atomic queue operations add overhead even when the data is uniform.

**Solution: combine both in sequence.**

```
Total work: N points
Phase 1 (Static):  First 80% → divided equally, no atomic overhead
Phase 2 (Dynamic): Remaining 20% → atomic counter, idle threads help finish

Thread 0: ██████████████████████[Phase1]███ → grabs overflow work
Thread 1: ██████████████████████[Phase1]███████ (slower chunk)
Thread 2: ██████████████████████[Phase1]██ → grabs overflow → grabs more
Thread 3: ██████████████████████[Phase1]█ → grabs overflow → grabs more → grabs more
```

```cpp
const int static_total = (n * 4) / 5;  // 80% of work
std::atomic<int> overflow_cursor{static_total};

#pragma omp parallel num_threads(actual_threads)
{
    int tid = omp_get_thread_num();
    // Phase 1: my pre-assigned block (no contention)
    for (int i = tid * static_chunk; i < my_end; ++i)
        classify(points[i]);
    // Phase 2: grab remaining work atomically
    while (true) {
        int i = overflow_cursor.fetch_add(1);
        if (i >= n) break;
        classify(points[i]);
    }
}
```

---

### Milestone 2 Results (from `bench_m2_live.txt`, 4 threads)

**Speedups over sequential:**

| Dataset | Sequential | Static | Dynamic | Tiled e2e | Work-Steal | Hybrid |
|---|---|---|---|---|---|---|
| 100K uniform | 1,377,158 pts/s | 2.27x | 2.34x | 1.95x | 2.34x | **2.44x** |
| 100K clustered | 2,232,851 pts/s | **1.87x** | 1.84x | 1.38x | 1.80x | 1.75x |
| 1M uniform | 1,355,924 pts/s | 2.33x | **2.34x** | 1.77x | 2.32x | 2.33x |
| 1M clustered | 2,157,651 pts/s | 1.55x | **1.59x** | 1.00x | 1.58x | 1.52x |

**Thread-scaling (Dynamic strategy, median of 7 runs, 1M uniform):**

| Threads | Time | Speedup | Parallel Efficiency |
|---|---|---|---|
| 1 | 949.10 ms | 1.00x | 100.0% |
| 2 | 405.68 ms | 2.34x | 117.0% |
| 4 | 387.05 ms | 2.45x | 61.3% |

**Thread-scaling (Dynamic strategy, 1M clustered):**

| Threads | Time | Speedup | Parallel Efficiency |
|---|---|---|---|
| 1 | 473.00 ms | 1.00x | 100.0% |
| 2 | 326.29 ms | 1.45x | 72.5% |
| 4 | 338.85 ms | 1.40x | 34.9% |

**Real-world data — pak_admin2 (Level 2, 204 polygons, 745 centroid points):**

| Strategy | Throughput | Speedup |
|---|---|---|
| Sequential | 69,538 pts/sec | 1.00x |
| Static OMP | 67,692 pts/sec | 0.97x |
| Dynamic OMP | 71,340 pts/sec | 1.03x |
| Tiled+Morton | 72,945 pts/sec | 1.05x |
| **Work-Stealing** | **146,734 pts/sec** | **2.11x** |
| Hybrid | 71,482 pts/sec | 1.03x |

**Why only ~2x speedup with 4 threads instead of 4x?**

Three factors limit speedup:

1. **Memory bandwidth bottleneck** — Quadtree traversal = random memory accesses. All 4 threads simultaneously access the same quadtree structure in RAM, saturating the memory bus. Adding more cores does not add more memory bandwidth.

2. **Amdahl's Law** — The quadtree build, data generation, and result validation run sequentially. Even a small serial fraction caps the theoretical maximum speedup.

3. **Load imbalance** — For clustered data, some threads process dense regions (more ray-casting per point) while others process sparse regions. The entire parallel job waits for the slowest thread.

**Why does the 2-thread run sometimes beat 4-thread (clustered)?**
With 4 threads and clustered data, all threads simultaneously access hot cache lines but compete for the same L3 cache. Two threads sometimes have better cache hit rates than four competing for the same limited cache. This is a hardware-level phenomenon.

**Why is Work-Stealing best for real-world Pakistan data (2.11x) but not for synthetic data?**
Real Pakistan polygons have highly variable complexity (50-800 vertices each). The time to classify a point varies dramatically depending on which polygon it lands in. Work-stealing directly addresses this by dynamically re-balancing when some threads get stuck on expensive polygons.

---

## 8. Milestone 3 — Scalable Batch and Distributed Execution

### Goal

Scale beyond what a single process can handle: process 10M+ points efficiently using multiple independent worker processes (simulating a distributed cluster), compare **polygon replication vs spatial sharding**, and measure **strong and weak scaling**.

### Architecture

```
Master Process (benchmark_m3.exe)
    │
    ├── Creates 100×100 polygon grid (10,000 polygons)
    ├── Divides X-axis into W equal vertical strips (one per worker)
    ├── Generates points in batches of 250,000
    ├── Routes each point to the worker whose X-strip it falls in
    │
    ├──→ Worker 0  handles X ∈ [0, 25)
    ├──→ Worker 1  handles X ∈ [25, 50)
    ├──→ Worker 2  handles X ∈ [50, 75)
    └──→ Worker 3  handles X ∈ [75, 100]

Each worker:
    ├── Has its own polygon set (replicated OR sharded)
    ├── Builds its own Quadtree index independently
    ├── Classifies its assigned points
    └── Returns aggregate counts + checksum (not per-point results)
```

### Key Concept 1 — Spatial Partitioning

Points are routed to workers based on their X coordinate:

```cpp
int worker_for_point(const Point& p, int workers) {
    double normalized = (p.x - X_MIN) / (X_MAX - X_MIN);  // [0, 1]
    int worker = (int)(normalized * workers);               // [0, W-1]
    return worker;
}
```

This is like dividing a map into vertical columns — each worker only sees the points in its column. For uniform data, all workers get equal load. For clustered data, workers whose strips contain cluster centers get far more work.

### Key Concept 2 — Polygon Replication vs Spatial Sharding

**Option A — Replication (every worker gets ALL polygons):**
```
Worker 0: all 10,000 polygons → builds quadtree over all 10,000
Worker 1: all 10,000 polygons → builds quadtree over all 10,000
Worker 2: all 10,000 polygons → builds quadtree over all 10,000
Worker 3: all 10,000 polygons → builds quadtree over all 10,000
Total polygon copies: 4 × 10,000 = 40,000

Pro: Simple — every point can always be classified
Con: Each worker indexes all polygons, including ones in other workers' strips
```

**Option B — Sharding (each worker only gets polygons in its X strip):**
```
Worker 0: only polygons whose bbox overlaps X=[0,25)   → ~2,650 polygons
Worker 1: only polygons whose bbox overlaps X=[25,50)  → ~2,650 polygons
Worker 2: only polygons whose bbox overlaps X=[50,75)  → ~2,650 polygons
Worker 3: only polygons whose bbox overlaps X=[75,100] → ~2,650 polygons
Total polygon copies: ~10,600 (boundary polygons counted in multiple workers)

Pro: Smaller index per worker → faster quadtree queries
Con: Requires knowing which polygons overlap each strip at setup time
```

From `bench_m3_live.txt`:
- Sharding: 10,600 indexed copies (vs 40,000 for replication) — 3.8x smaller index

### Key Concept 3 — Batched Processing

We cannot load 100M points into memory at once (~2.4 GB just for coordinates). Instead, we process in batches of 250,000 points, keeping memory usage constant:

```
Total: 10,000,000 points
Batch 0:  generate 250,000 → partition → classify → aggregate → discard
Batch 1:  generate 250,000 → partition → classify → aggregate → discard
...
Batch 39: generate 250,000 → partition → classify → aggregate → discard
Final:    combine all aggregates → done
```

Workers use futures (`std::async`) so all worker threads run in parallel per batch:
```cpp
for (int w = 0; w < workers; ++w) {
    futures.push_back(std::async(std::launch::async, [&buckets, &contexts, w]() {
        return classify_bucket(buckets[w], contexts[w]);
    }));
}
// Wait for all workers, merge results
```

### Key Concept 4 — Strong Scaling vs Weak Scaling

These are standard PDC performance measurements:

**Strong Scaling:** Fix the total problem size, increase number of workers. Ideal: time drops proportionally.
```
Fixed: 1,000,000 points
1 worker:  788.19ms classify (baseline)
2 workers: 541.43ms classify  → speedup = 1.46x  (ideal = 2.00x)
4 workers: 400.95ms classify  → speedup = 1.97x  (ideal = 4.00x)
```

**Weak Scaling:** Increase both problem size AND workers proportionally. Ideal: time stays flat.
```
1 worker,  250K points: 193.39ms classify
2 workers, 500K points: 269.24ms classify  → 1.39x the time  (ideal = 1.00x)
4 workers,  1M points:  403.77ms classify  → 2.09x the time  (ideal = 1.00x)
```
Time grows because each worker also builds its own full polygon index (fixed cost per worker regardless of point count), so index build time adds up as workers increase.

### Key Concept 5 — Multi-Process IPC (Inter-Process Communication)

Beyond using async threads within the same process, Milestone 3 implements a true **multi-process** model using Windows `CreateProcess`. Workers are completely separate executables with no shared memory.

```
Master process (benchmark_m3.exe):
  1. Generate and partition all points
  2. Write binary files to disk:
       ipc/polygons.bin           ← serialized polygon data (shared)
       ipc/worker_0_input.bin     ← point batch for worker 0 + config header
       ipc/worker_1_input.bin     ← point batch for worker 1
       ipc/worker_2_input.bin     ← point batch for worker 2
       ipc/worker_3_input.bin     ← point batch for worker 3
  3. Launch all 4 worker.exe processes simultaneously (CreateProcess)
  4. Wait for all 4 to finish (WaitForMultipleObjects)
  5. Read result files:
       ipc/worker_0_result.bin    ← aggregate counts + checksum from worker 0
       ipc/worker_1_result.bin
       ...
  6. Combine all results

Each worker.exe (separate process):
  1. Read its input file (points + config header: worker_id, stripe bounds, polygon_mode)
  2. Read shared polygon file
  3. Filter/prepare its polygon set (replicated or sharded)
  4. Build its own Quadtree index
  5. Classify all assigned points
  6. Write result file (6 numbers: matched, unmatched, candidate_checks, checksum)
  7. Exit
```

**The binary IPC format** (`ipc.hpp`): All data is serialized as raw binary (not JSON), using direct `write`/`read` of `double` and `uint64_t` values for maximum speed. A polygon is stored as: its ID, then the exterior ring (vertex count + all vertices), then hole count, then each hole ring.

**Why store only aggregates, not per-point results?**
At 10M points, storing per-point polygon IDs would require `10M × 8 bytes = 80MB` of result files. Instead, workers compute a running XOR checksum:

```cpp
checksum ^= mix_u64((point.id << 32) ^ polygon_id);
```

This 64-bit fingerprint verifies that all results are identical across different runs, strategies, and worker counts — without storing 80MB of output.

---

### Milestone 3 Results (from `bench_m3_live.txt`)

**Large-Scale Batched Throughput (4 workers, replicated polygons):**

| Points | Distribution | Class Time | Throughput | Avg Candidates |
|---|---|---|---|---|
| 1,000,000 | Uniform | 404.22 ms | **2,473,898 pts/sec** | 3.51 |
| 1,000,000 | Clustered | 296.59 ms | **3,371,715 pts/sec** | 3.48 |
| 10,000,000 | Uniform | 3,743.42 ms | **2,671,352 pts/sec** | 3.50 |
| 10,000,000 | Clustered | 2,729.17 ms | **3,664,123 pts/sec** | 3.48 |

Throughput is consistent from 1M to 10M — confirming the system scales linearly. Avg candidates = 3.5 means the quadtree reduces each query to checking only 3-4 polygons out of 10,000.

**Replication vs Sharding (1M points, 4 workers):**

| Distribution | Mode | Class Time | Throughput | Indexed Copies |
|---|---|---|---|---|
| Uniform | Replicated | 349.75 ms | 2,859,210 pts/sec | 40,000 |
| Uniform | **Sharded** | **337.85 ms** | **2,959,924 pts/sec** | **10,600** |
| Clustered | **Replicated** | **274.00 ms** | **3,649,639 pts/sec** | **40,000** |
| Clustered | Sharded | 302.90 ms | 3,301,429 pts/sec | 10,600 |

For uniform data, sharding wins slightly (smaller index = faster quadtree). For clustered data, replication wins — the clusters in this run fall unevenly across shards, so some shard-workers get much more work than others, causing imbalance.

**Strong Scaling (1M points, replicated, using classify time):**

| Workers | Uniform Time | Uniform Speedup | Clustered Time | Clustered Speedup |
|---|---|---|---|---|
| 1 | 788.19 ms | 1.00x | 459.98 ms | 1.00x |
| 2 | 541.43 ms | **1.46x** | 625.96 ms | **0.73x** (regression!) |
| 4 | 400.95 ms | **1.97x** | 363.22 ms | **1.27x** |

**The 2-worker clustered regression is not a bug — it is a real observation.** The 5 cluster centers happen to concentrate in one of the two X-strips. One worker gets ~65% of the points, the other gets ~35%. The overloaded worker finishes last, delaying the result. At 4 workers, the strips are narrower and the clusters are more evenly distributed. This directly demonstrates the **load imbalance under spatial skew** problem.

**Weak Scaling (250K points per worker, using classify time):**

| Workers | Total Points | Uniform Time | Clustered Time |
|---|---|---|---|
| 1 | 250,000 | 193.39 ms | 114.11 ms |
| 2 | 500,000 | 269.24 ms | 300.47 ms |
| 4 | 1,000,000 | 403.77 ms | 294.19 ms |

Ideally the time stays flat. Time grows because each additional worker also builds a full polygon index (fixed ~200ms cost per worker), so total index-build time scales with worker count.

**Multi-Process IPC Benchmark (1M points, 4 workers):**

| Distribution | Mode | Write | Workers | Read | Total | Throughput |
|---|---|---|---|---|---|---|
| Uniform | Replicated | 311.00 ms | 583.05 ms | 0.74 ms | 897.86 ms | 1,113,757 pts/sec |
| Uniform | Sharded | 299.20 ms | 540.54 ms | 0.57 ms | 840.42 ms | 1,189,886 pts/sec |
| Clustered | Replicated | 378.94 ms | 531.83 ms | 0.57 ms | 911.42 ms | 1,097,185 pts/sec |
| Clustered | Sharded | 349.80 ms | 491.75 ms | 0.95 ms | 842.59 ms | 1,186,813 pts/sec |

**IPC overhead breakdown:**
```
In-process async (same process):   2,473,898 pts/sec
Multi-process IPC (file-based):    1,113,757 pts/sec
Overhead ratio: ~2.2x slower

Cost breakdown (uniform, replicated):
  Write to disk:    311ms  ← serialize 1M points + 10K polygons to binary files
  Workers compute:  583ms  ← actual useful work (4 processes in parallel)
  Read results:       1ms  ← read 4 tiny aggregate result files
```

File-based IPC costs ~300ms of pure write overhead. This is the price of true process isolation. In a real distributed system, this would be the cost of network communication to send data to remote workers.

**Checksums are identical across all runs, strategies, and worker counts** — confirming bit-for-bit identical results between in-process and multi-process execution. ✓

---

## 9. Benchmark Results

### Summary Across All Three Milestones

**Milestone 1 — Sequential Speedup over Brute-Force:**

| Dataset | Quadtree Speedup | Strip Index Speedup |
|---|---|---|
| 100K uniform | 8.45x | 6.63x |
| 1M uniform | 19.97x | 19.34x |
| 100K clustered | 29.46x | 22.34x |
| 1M clustered | 29.38x | 22.07x |
| Pakistan ADM3 uniform (608 poly) | 1.23x | 1.27x |
| Pakistan ADM3 clustered (608 poly) | 1.32x | 1.42x |

**Milestone 2 — Best Parallel Speedup (4 threads) over Sequential:**

| Dataset | Best Strategy | Speedup | Efficiency |
|---|---|---|---|
| 100K uniform | Hybrid | 2.44x | 61% |
| 100K clustered | Static OMP | 1.87x | 47% |
| 1M uniform | Dynamic OMP | 2.34x | 59% |
| 1M clustered | Dynamic OMP | 1.59x | 40% |
| Pakistan ADM2 real (745 pts) | Work-Stealing | 2.11x | 53% |

**Milestone 3 — Distributed Scale:**

| Mode | Points | Throughput |
|---|---|---|
| In-process async, 4 workers | 1M | 2.47M pts/sec |
| In-process async, 4 workers | 10M | 2.67M pts/sec |
| Multi-process IPC, 4 workers | 1M | 1.11–1.19M pts/sec |

---

## 10. Key Findings

### Finding 1 — Spatial Indexing is the Biggest Win

Going from brute-force to Quadtree gives **20-30x speedup on a single core** for synthetic data. This is the single most important optimization in the entire project. No amount of parallel hardware can compensate for a fundamentally bad algorithm — 4 threads × a bad algorithm still loses to 1 thread × a good one.

### Finding 2 — Parallelism Gives Diminishing Returns on Memory-Bound Work

With 4 threads, we get ~2x speedup (not 4x) because quadtree traversal saturates memory bandwidth. The CPU cores are not the bottleneck — the RAM bus is. The thread-scaling table for clustered data (1.40x with 4 threads) demonstrates this starkly: adding the 3rd and 4th threads actually slows things down slightly by creating more cache pressure.

### Finding 3 — Morton Sort Helps Uniform Data, Hurts Clustered Data

Z-order sorting dramatically improves cache locality for randomly distributed points (better quadtree node reuse). But for clustered data, the points are already naturally grouped by location — sorting them again adds overhead without meaningful benefit. End-to-end speedup for 1M clustered data with Morton sort: **1.00x** (no gain at all after accounting for sort cost).

### Finding 4 — Load Imbalance is Visible and Measurable

The M3 strong-scaling results show 2 workers being **slower than 1 worker** (0.73x) for clustered data. This directly demonstrates the load imbalance problem: with only 2 strips, the 5 cluster centers happen to fall unevenly, giving one worker 65% of the work. With 4 workers (narrower strips), the imbalance is reduced and we recover speedup.

### Finding 5 — Work-Stealing Shines on Real-World Data

For the synthetic grid (uniform polygon sizes, predictable computation per point), all strategies perform similarly. But for real Pakistan polygon data with 50–800 vertices per polygon, Work-Stealing achieves 2.11x speedup while Static/Dynamic OMP barely exceed 1.0x. Per-point computation time variance is the key factor that makes stealing worth the overhead.

### Finding 6 — IPC Overhead is ~2x at 1M Scale

File-based multi-process IPC costs ~300ms write overhead for 1M points, roughly doubling total execution time compared to in-process async threads. For a real distributed system, this represents network serialization and transmission latency. The actual worker computation time (583ms) is roughly equal to the in-process time — the IPC overhead is purely in data movement, not computation.

### Finding 7 — All Results Are Deterministically Correct

Every benchmark validates results using either direct comparison (`[PASS] Validated: X matches sequential`) or XOR checksums. Checksums are identical across all runs, distributions, strategies, and worker counts — confirming that parallel implementations produce bit-for-bit identical results to the sequential baseline.

---

## 11. Problems We Faced

### Problem 1 — Three Bugs in the Quadtree

The quadtree had three subtle bugs found through testing:

**Bug 1 — Wrong container for candidates:**
The query function initially used `std::set` for deduplication. `std::set` insertion is O(log n). For a polygon that overlaps multiple quadtree cells, it was inserted into multiple leaf nodes, and deduplication was unnecessarily slow. Fixed by switching to `std::unordered_set` (O(1) average).

**Bug 2 — Boundary points visiting multiple quadrants:**
The original query used `p.x <= mid_x` for the left condition and `p.x >= mid_x` for the right. A point exactly on the boundary satisfied BOTH, visiting 2 or 4 quadrants and producing duplicate candidates. Fixed by using strict `p.x < mid_x` vs `p.x >= mid_x` — each point visits exactly one quadrant.

**Bug 3 — Wrong split threshold:**
The split used `size <= MAX_POLYGONS_PER_LEAF` (split when full). This meant a leaf was only split after it already had MAX+1 polygons, not when it reached MAX. Fixed to `size < MAX_POLYGONS_PER_LEAF` so splitting happens before the limit is exceeded.

### Problem 2 — Edge Cases in Ray-Casting

Getting the ray-casting algorithm correct for all geometric edge cases took significant iteration:
- **Horizontal edges** cause division-by-zero in the parametric intersection formula. Solution: skip them entirely.
- **Vertex double-counting**: a ray passing exactly through a vertex touches two edges at the same point. Without the `(a.y > p.y) != (b.y > p.y)` rule, both edges get counted, flipping inside/outside. The half-open interval rule ensures only one edge counts.
- **Floating-point precision**: using `EPSILON = 1e-10` throughout prevents misclassification for points near edges.

### Problem 3 — Morton Sort Overhead at 1M Scale

Initially, the Tiled+Morton strategy looked great in the "classify-only" benchmark — up to 3.8M pts/sec. But when we added the sort cost, end-to-end performance dropped to below the baseline for clustered data (1.00x speedup). The lesson: always benchmark end-to-end, not just the "interesting" part.

### Problem 4 — Load Imbalance with 2-Worker Clustered Data

The 2-worker clustered result (0.73x — slower than 1 worker!) looked like a bug at first. After investigation, it turned out to be a real physical phenomenon: with only 2 X-strips and 5 cluster centers, the random cluster placement caused severe imbalance. This was not fixed — it's a genuine demonstration of the load imbalance problem that the project aims to expose.

### Problem 5 — Super-Linear Speedup at 2 Threads

The thread-scaling table for 1M uniform data shows 2.34x speedup with 2 threads (efficiency = 117%). Super-linear speedup sounds impossible but happens here due to L3 cache effects: with 1 thread, the quadtree data doesn't fit entirely in L2 cache. With 2 threads, each thread accesses a smaller portion of the quadtree (its own half of the data), which fits better in L2. The result is better cache hit rates per thread, yielding more than 2x total.

### Problem 6 — Real-World GeoJSON Parsing

The Pakistan GeoJSON files contain several geometry quirks:
- Mix of `Polygon` and `MultiPolygon` types in the same file
- `MultiPolygon` components that need flattening into individual `Polygon` objects
- Centroid files where coordinates appear in properties (`x_coord`/`y_coord`) rather than as Point geometries
- Some coordinate values stored as strings rather than numbers in the JSON

The GeoJSON loader was extended with fallback schemas and string-to-double conversion to handle all these cases robustly.

### Problem 7 — IPC Binary Format Correctness

The binary IPC protocol (`ipc.hpp`) had to be designed carefully. Using direct `struct` memcpy is dangerous (padding, endianness). We serialize field-by-field using `write_pod` for each scalar value, ensuring portability. This also means the format is well-defined regardless of compiler padding.

---

## How to Run the Benchmarks

```powershell
# Set up MSYS2 environment (required for runtime DLLs)
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cd "d:\Classess\PDC\Project\Parallel-Point-in-Polygon-Classification-for-Large-Scale-Geospatial-Data"

# Milestone 1 — Sequential baseline with spatial indexing
.\build_new\benchmark_m1.exe

# Milestone 2 — Parallel strategies comparison (4 threads)
.\build_new\benchmark_m2.exe

# Milestone 3 — Default run (1M + 10M points)
.\build_new\benchmark_m3.exe

# Milestone 3 — Full run including 100M points
.\build_new\benchmark_m3.exe --full

# Milestone 3 — Quick smoke test
.\build_new\benchmark_m3.exe --quick
```

---

## Technical Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| Shared-memory parallelism | OpenMP |
| In-process async workers | `std::async` / `std::future` |
| Multi-process execution | Windows `CreateProcess` / `WaitForMultipleObjects` |
| GeoJSON parsing | nlohmann/json (header-only) |
| IPC format | Custom binary (raw `double` + `uint64_t` serialization) |
| Build system | CMake |
| Compiler | MSYS2 / GCC (UCRT64) |

---

*All benchmark numbers are taken directly from `bench_m1_live.txt`, `bench_m2_live.txt`, and `bench_m3_live.txt` — actual runs on the development machine.*
